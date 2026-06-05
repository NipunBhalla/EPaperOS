#pragma once

// Optional FreeType-backed font wrapper used for EPUB reading view.
// All declarations are guarded by USE_FREETYPE so the project can
// still be built without linking against FreeType.

#ifdef USE_FREETYPE

#include <ft2build.h>
#include FT_FREETYPE_H
#include <freetype/ftmodapi.h>
#include <freetype/ftsystem.h>

#include <cstdint>
#include <unordered_map>

class Renderer;

class FreeTypeFont
{
public:
  FreeTypeFont();
  ~FreeTypeFont();

  // Initialize the FreeType face from a font file on the filesystem.
  // Returns true on success.
  bool init(const char *font_path, int pixel_height);

  // Measure the advance width of a UTF-8 string in pixels.
  int get_text_width(const char *text) const;

  // Render a UTF-8 string at the given logical coordinates using the
  // supplied Renderer as a pixel sink. The y coordinate should specify
  // the top of the line; the wrapper will place the baseline
  // appropriately based on the font metrics.
  void draw_text(Renderer *renderer, int x, int y, const char *text) const;

  bool is_valid() const { return m_initialized; }

  // Return the recommended line height in pixels based on the current
  // font metrics.
  int get_line_height() const;

  // Return the current requested pixel height that was configured for
  // this face via FT_Set_Pixel_Sizes.
  int get_pixel_height() const { return m_pixel_height; }

  // Update the pixel height for this face at runtime. Returns true on
  // success and leaves the previous size unchanged on failure.
  bool set_pixel_height(int pixel_height);

private:
  // Cached, already-rasterized glyph: advance + 8-bit alpha bitmap. Layout
  // (get_text_width) and render (draw_text) otherwise re-load and re-render the
  // same glyphs every page, which is the dominant per-page CPU cost. Keyed by
  // (codepoint, pixel_height) so multiple sizes can coexist.
  struct GlyphCacheEntry
  {
    int advance = 0;
    int left = 0;
    int top = 0;
    int width = 0;
    int rows = 0;
    unsigned char *bitmap = nullptr; // width*rows alpha, or null when blank
  };

  // Look up (or rasterize + cache) the glyph for codepoint at the current
  // pixel height. Returns nullptr only if the glyph cannot be loaded at all.
  const GlyphCacheEntry *get_glyph(unsigned int codepoint) const;
  void clear_glyph_cache();

  // Custom allocator so the whole FreeType library + faces + internal buffers
  // live in PSRAM, not internal DRAM. Internal DRAM is permanently consumed by
  // the (never-deinited) WiFi driver after the first Tasks fetch; if FreeType
  // allocates there, post-WiFi glyph rasterization fails silently and text
  // disappears while rect-drawn UI survives. Must outlive m_library.
  FT_MemoryRec_ m_ft_memory = {};

  FT_Library m_library = nullptr;
  FT_Face m_face = nullptr;
  int m_pixel_height = 0;
  bool m_initialized = false;

  // Bounded: cleared wholesale when it grows past the cap to keep RAM in check.
  // The working set for a page of body text is well under this.
  static const size_t kGlyphCacheMax = 1024;
  mutable std::unordered_map<uint64_t, GlyphCacheEntry> m_glyph_cache;
};

#endif // USE_FREETYPE
