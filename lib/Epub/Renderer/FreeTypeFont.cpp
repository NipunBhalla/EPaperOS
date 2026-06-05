#include "FreeTypeFont.h"

#ifdef USE_FREETYPE

#include "Renderer.h"
#include <string.h>
#include <stdlib.h>
#include <esp_heap_caps.h>

// FreeType allocator callbacks routed to PSRAM. See FreeTypeFont.h for why
// (WiFi permanently owns internal DRAM; FreeType must not compete for it).
static void *ft_spiram_alloc(FT_Memory, long size)
{
  return heap_caps_malloc((size_t)size, MALLOC_CAP_SPIRAM);
}
static void ft_spiram_free(FT_Memory, void *block)
{
  heap_caps_free(block);
}
static void *ft_spiram_realloc(FT_Memory, long, long new_size, void *block)
{
  return heap_caps_realloc(block, (size_t)new_size, MALLOC_CAP_SPIRAM);
}

// Decode the next UTF-8 code point from the string and advance the
// pointer. Returns 0 on error, in which case the caller can skip the
// glyph.
static unsigned int utf8_next_codepoint(const unsigned char *&p)
{
  unsigned char c = *p++;
  // Single-byte (ASCII)
  if (c < 0x80)
  {
    return c;
  }
  // 2-byte sequence
  if ((c & 0xE0) == 0xC0)
  {
    unsigned char c2 = *p;
    if ((c2 & 0xC0) != 0x80)
    {
      return 0;
    }
    ++p;
    return ((c & 0x1F) << 6) | (c2 & 0x3F);
  }
  // 3-byte sequence
  if ((c & 0xF0) == 0xE0)
  {
    unsigned char c2 = p[0];
    unsigned char c3 = p[1];
    if ((c2 & 0xC0) != 0x80 || (c3 & 0xC0) != 0x80)
    {
      return 0;
    }
    p += 2;
    return ((c & 0x0F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);
  }
  // 4-byte sequence
  if ((c & 0xF8) == 0xF0)
  {
    unsigned char c2 = p[0];
    unsigned char c3 = p[1];
    unsigned char c4 = p[2];
    if ((c2 & 0xC0) != 0x80 || (c3 & 0xC0) != 0x80 || (c4 & 0xC0) != 0x80)
    {
      return 0;
    }
    p += 3;
    return ((c & 0x07) << 18) | ((c2 & 0x3F) << 12) | ((c3 & 0x3F) << 6) | (c4 & 0x3F);
  }
  // Invalid leading byte
  return 0;
}

FreeTypeFont::FreeTypeFont() {}

FreeTypeFont::~FreeTypeFont()
{
  clear_glyph_cache();
  if (m_face)
  {
    FT_Done_Face(m_face);
    m_face = nullptr;
  }
  if (m_library)
  {
    FT_Done_Library(m_library);
    m_library = nullptr;
  }
  m_initialized = false;
}

void FreeTypeFont::clear_glyph_cache()
{
  for (auto &kv : m_glyph_cache)
  {
    heap_caps_free(kv.second.bitmap);
  }
  m_glyph_cache.clear();
}

const FreeTypeFont::GlyphCacheEntry *FreeTypeFont::get_glyph(unsigned int codepoint) const
{
  if (!m_face)
  {
    return nullptr;
  }

  // Key combines codepoint and the current pixel height so re-sizing the face
  // does not return stale-size glyphs.
  uint64_t key = (static_cast<uint64_t>(codepoint) << 16) | static_cast<uint16_t>(m_pixel_height);
  auto it = m_glyph_cache.find(key);
  if (it != m_glyph_cache.end())
  {
    return &it->second;
  }

  FT_UInt glyph_index = FT_Get_Char_Index(m_face, codepoint);
  if (FT_Load_Glyph(m_face, glyph_index, FT_LOAD_DEFAULT) != 0)
  {
    return nullptr;
  }
  if (FT_Render_Glyph(m_face->glyph, FT_RENDER_MODE_NORMAL) != 0)
  {
    return nullptr;
  }

  FT_GlyphSlot slot = m_face->glyph;
  FT_Bitmap &bmp = slot->bitmap;

  // Compute advance with the same fallbacks the old code used.
  int advance = static_cast<int>(slot->advance.x >> 6);
  if (advance <= 0)
  {
    int metrics_advance = static_cast<int>(slot->metrics.horiAdvance >> 6);
    if (metrics_advance > 0)
    {
      advance = metrics_advance;
    }
    else if (static_cast<int>(bmp.width) > 0)
    {
      advance = static_cast<int>(bmp.width);
    }
    else
    {
      advance = m_pixel_height > 0 ? m_pixel_height / 2 : 1;
    }
  }

  GlyphCacheEntry entry;
  entry.advance = advance;
  entry.left = slot->bitmap_left;
  entry.top = slot->bitmap_top;
  entry.width = static_cast<int>(bmp.width);
  entry.rows = static_cast<int>(bmp.rows);
  entry.bitmap = nullptr;
  if (entry.width > 0 && entry.rows > 0 && bmp.buffer)
  {
    // Copy the alpha bitmap compactly (width*rows), dropping FreeType's pitch
    // so the cached buffer is exactly width*rows. PSRAM-backed so it survives
    // internal-DRAM exhaustion from the WiFi driver.
    entry.bitmap = static_cast<unsigned char *>(
        heap_caps_malloc(static_cast<size_t>(entry.width) * entry.rows, MALLOC_CAP_SPIRAM));
    if (!entry.bitmap)
    {
      // Allocation failed (transient low memory). Do NOT cache a null-bitmap
      // entry for a glyph that should be visible, or it would stay blank
      // forever. Bail so the next render retries the rasterization.
      return nullptr;
    }
    for (int row = 0; row < entry.rows; ++row)
    {
      memcpy(entry.bitmap + static_cast<size_t>(row) * entry.width,
             bmp.buffer + static_cast<size_t>(row) * bmp.pitch,
             entry.width);
    }
  }

  // Bound memory: if the cache is full, drop everything and start fresh. The
  // per-page working set re-populates in one pass.
  if (m_glyph_cache.size() >= kGlyphCacheMax)
  {
    const_cast<FreeTypeFont *>(this)->clear_glyph_cache();
  }

  auto res = m_glyph_cache.emplace(key, entry);
  return &res.first->second;
}

bool FreeTypeFont::init(const char *font_path, int pixel_height)
{
  if (!font_path || pixel_height <= 0)
  {
    return false;
  }

  // Build the library on a PSRAM allocator instead of FT_Init_FreeType (which
  // hardwires the C heap / internal DRAM). FT_New_Library does not register the
  // font drivers, so add the default modules explicitly afterwards.
  m_ft_memory.user = nullptr;
  m_ft_memory.alloc = ft_spiram_alloc;
  m_ft_memory.free = ft_spiram_free;
  m_ft_memory.realloc = ft_spiram_realloc;

  FT_Error err = FT_New_Library(&m_ft_memory, &m_library);
  if (err != 0)
  {
    return false;
  }
  FT_Add_Default_Modules(m_library);

  err = FT_New_Face(m_library, font_path, 0, &m_face);
  if (err != 0)
  {
    FT_Done_Library(m_library);
    m_library = nullptr;
    return false;
  }

  // Ensure we use the Unicode charmap if available so that ASCII and
  // UTF-8 text map to sensible glyph indices.
  FT_Select_Charmap(m_face, FT_ENCODING_UNICODE);

  err = FT_Set_Pixel_Sizes(m_face, 0, pixel_height);
  if (err != 0)
  {
    FT_Done_Face(m_face);
    FT_Done_Library(m_library);
    m_face = nullptr;
    m_library = nullptr;
    return false;
  }

  m_pixel_height = pixel_height;
  m_initialized = true;
  return true;
}

bool FreeTypeFont::set_pixel_height(int pixel_height)
{
  if (!m_initialized || !m_face || pixel_height <= 0)
  {
    return false;
  }

  FT_Error err = FT_Set_Pixel_Sizes(m_face, 0, pixel_height);
  if (err != 0)
  {
    return false;
  }

  if (pixel_height != m_pixel_height)
  {
    // Different size -> previously cached glyphs are dead weight. Free them.
    clear_glyph_cache();
  }
  m_pixel_height = pixel_height;
  return true;
}

int FreeTypeFont::get_text_width(const char *text) const
{
  if (!m_initialized || !text)
  {
    return 0;
  }

  int width = 0;
  const unsigned char *p = reinterpret_cast<const unsigned char *>(text);

  while (*p)
  {
    unsigned int codepoint = utf8_next_codepoint(p);
    if (codepoint == 0)
    {
      continue;
    }

    const GlyphCacheEntry *g = get_glyph(codepoint);
    if (g)
    {
      width += g->advance;
    }
  }

  return width;
}

int FreeTypeFont::get_line_height() const
{
  if (!m_initialized || !m_face || !m_face->size)
  {
    return m_pixel_height;
  }

  FT_Size_Metrics metrics = m_face->size->metrics;
  if (metrics.height > 0)
  {
    return static_cast<int>(metrics.height >> 6);
  }

  if (metrics.ascender != 0 || metrics.descender != 0)
  {
    // height is typically ascender - descender; both are in 26.6 format
    FT_Pos h = metrics.ascender - metrics.descender;
    if (h > 0)
    {
      return static_cast<int>(h >> 6);
    }
  }

  return m_pixel_height;
}

void FreeTypeFont::draw_text(Renderer *renderer, int x, int y, const char *text) const
{
  if (!m_initialized || !renderer || !text)
  {
    return;
  }

  int pen_x = x;
  // Place baseline roughly at y + pixel_height. This can be refined
  // later using ascender/descender metrics.
  int baseline_y = y + m_pixel_height;

  const unsigned char *p = reinterpret_cast<const unsigned char *>(text);

  while (*p)
  {
    unsigned int codepoint = utf8_next_codepoint(p);
    if (codepoint == 0)
    {
      continue;
    }

    const GlyphCacheEntry *g = get_glyph(codepoint);
    if (!g)
    {
      continue;
    }

    if (g->bitmap && g->width > 0 && g->rows > 0)
    {
      // One blit per glyph. The renderer applies board contrast/gamma and the
      // gray-flush decision once for the whole glyph instead of per pixel via
      // a virtual draw_pixel call (the per-page render hot path).
      renderer->draw_glyph_alpha(pen_x + g->left, baseline_y - g->top, g->width, g->rows, g->bitmap);
    }

    pen_x += g->advance;
  }
}

#endif // USE_FREETYPE
