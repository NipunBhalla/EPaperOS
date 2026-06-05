#pragma once
#include <esp_log.h>

#if defined(BOARD_TYPE_PAPER_S3)
// Paper S3 uses a 4.7" 960x540 panel driven by epdiy; epdiy coordinates are
// 960x540 (width x height) before we apply an inverted portrait rotation.
#include <epdiy.h>
#ifndef EPD_WIDTH
#define EPD_WIDTH 960
#endif
#ifndef EPD_HEIGHT
#define EPD_HEIGHT 540
#endif
#else
#include <epd_driver.h>
#endif

#include <math.h>
#include "Renderer.h"
#include "miniz.h"

#ifdef USE_FREETYPE
#include "FreeTypeFont.h"
#endif

#define GAMMA_VALUE (1.0f / 0.8f)

class EpdiyFrameBufferRenderer : public Renderer
{
protected:
  const EpdFont *m_regular_font;
  const EpdFont *m_bold_font;
  const EpdFont *m_italic_font;
  const EpdFont *m_bold_italic_font;
  const uint8_t *m_busy_image;
  int m_busy_image_width;
  int m_busy_image_height;
  uint8_t *m_frame_buffer;
  EpdFontProperties m_font_props;
  uint8_t gamma_curve[256] = {0};
  bool needs_gray_flush = false;
  bool m_text_inverted = false; // draw glyphs white instead of black

#ifdef USE_FREETYPE
  FreeTypeFont *m_freetype_font = nullptr;
  bool m_freetype_enabled = false;
#endif

  const EpdFont *get_font(bool is_bold, bool is_italic)
  {
    if (is_bold && is_italic)
    {
      return m_bold_italic_font;
    }
    if (is_bold)
    {
      return m_bold_font;
    }
    if (is_italic)
    {
      return m_italic_font;
    }
    return m_regular_font;
  }

public:
  EpdiyFrameBufferRenderer(
      const EpdFont *regular_font,
      const EpdFont *bold_font,
      const EpdFont *italic_font,
      const EpdFont *bold_italic_font,
      const uint8_t *busy_icon,
      int busy_icon_width,
      int busy_icon_height)
      : m_regular_font(regular_font), m_bold_font(bold_font), m_italic_font(italic_font), m_bold_italic_font(bold_italic_font),
        m_busy_image(busy_icon), m_busy_image_width(busy_icon_width), m_busy_image_height(busy_icon_height)
  {
    m_font_props = epd_font_properties_default();
    // fallback to a question mark for character not available in the font
    m_font_props.fallback_glyph = '?';
    // For Paper S3 we always render in inverted portrait orientation so that
    // logical coordinates are (page_width = 540, page_height = 960).
    epd_set_rotation(EPD_ROT_INVERTED_PORTRAIT);

    for (int gray_value = 0; gray_value < 256; gray_value++)
    {
      gamma_curve[gray_value] = round(255 * pow(gray_value / 255.0, GAMMA_VALUE));
    }
  }
  virtual ~EpdiyFrameBufferRenderer()
  {
  }
  void show_busy()
  {
    int x = (EPD_HEIGHT - m_busy_image_width) / 2;
    int y = (EPD_WIDTH - m_busy_image_height) / 2;
    int width = m_busy_image_width;
    int height = m_busy_image_height;
    EpdRect image_area = {.x = x,
                          .y = y,
                          // don't forget we're rotated...
                          .width = width,
                          .height = height};
    epd_draw_rotated_transparent_image(
        image_area,
        m_busy_image, m_frame_buffer,
        0xE0);
    needs_gray_flush = true;
    flush_area(x, y, width, height);
  }

  void show_img(int x, int y, int width, int height, const uint8_t *img_buffer)
  {
    EpdRect image_area = {.x = x,
                          .y = y,
                          .width = width,
                          .height = height};
    epd_draw_rotated_transparent_image(
        image_area,
        img_buffer, m_frame_buffer,
        0xE0);
  }

  void needs_gray(uint8_t color)
  {
    if (color != 0 && color != 255)
    {
      needs_gray_flush = true;
    }
  }

  bool has_gray() {
    return needs_gray_flush;
  }

  int get_text_width(const char *text, bool bold = false, bool italic = false)
  {
#ifdef USE_FREETYPE
    if (m_freetype_enabled && m_freetype_font && m_freetype_font->is_valid())
    {
      return m_freetype_font->get_text_width(text);
    }
#endif
    if (!m_regular_font)
    {
      // No bitmap font available (e.g. Paper S3 FreeType-only and
      // FreeType failed to initialize). Return a conservative width of
      // zero so callers can handle it gracefully.
      return 0;
    }

    int x = 0, y = 0, x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    epd_get_text_bounds(get_font(bold, italic), text, &x, &y, &x1, &y1, &x2, &y2, &m_font_props);
    return x2 - x1;
  }
  void draw_text(int x, int y, const char *text, bool bold = false, bool italic = false)
  {
    // if using antialised text then set to gray next flush
    // needs_gray_flush = true;
#ifdef USE_FREETYPE
    if (m_freetype_enabled && m_freetype_font && m_freetype_font->is_valid())
    {
      // Pass RAW logical coords: FreeTypeFont::draw_text feeds glyphs to
      // draw_glyph_alpha(), which already adds margin_left/margin_top. Adding
      // them here too double-counted the margins, shifting all freetype text by
      // an extra (margin_left, margin_top) -- invisible when margins are 0 (home
      // menu), but with the status bar's margin_top=35 it dropped task text a
      // full line below its checkbox.
      m_freetype_font->draw_text(this, x, y, text);
      return;
    }
#endif
    if (!m_regular_font)
    {
      // No bitmap font available and FreeType is disabled; nothing to
      // draw.
      return;
    }
    int ypos = y + get_line_height() + margin_top;
    int xpos = x + margin_left;
    epd_write_string(get_font(bold, italic), text, &xpos, &ypos, m_frame_buffer, &m_font_props);
  }
  void set_text_inverted(bool inverted) override { m_text_inverted = inverted; }
  void draw_rect(int x, int y, int width, int height, uint8_t color = 0)
  {
    needs_gray(color);
    epd_draw_rect({.x = x + margin_left, .y = y + margin_top, .width = width, .height = height}, color, m_frame_buffer);
  }
  virtual void fill_rect(int x, int y, int width, int height, uint8_t color = 0)
  {
    needs_gray(color);
    epd_fill_rect({.x = x + margin_left, .y = y + margin_top, .width = width, .height = height}, color, m_frame_buffer);
  }
  virtual void fill_circle(int x, int y, int r, uint8_t color = 0)
  {
    needs_gray(color);
    epd_fill_circle(x, y, r, color, m_frame_buffer);
  }
  virtual void fill_triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint8_t color)
  {
    needs_gray(color);
    epd_fill_triangle(
        x0 + margin_left, y0 + margin_top,
        x1 + margin_left, y1 + margin_top,
        x2 + margin_left, y2 + margin_top,
        color, m_frame_buffer);
  }
  virtual void draw_triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint8_t color)
  {
    needs_gray(color);
    epd_draw_triangle(
        x0 + margin_left, y0 + margin_top,
        x1 + margin_left, y1 + margin_top,
        x2 + margin_left, y2 + margin_top,
        color, m_frame_buffer);
  }
  virtual void draw_pixel(int x, int y, uint8_t color)
  {
    uint8_t corrected_color = gamma_curve[color];
    needs_gray(corrected_color);
    epd_draw_pixel(x + margin_left, y + margin_top, corrected_color, m_frame_buffer);
  }
  // No-gamma pixel write for the enhanced (dithered) image path.
  virtual void draw_pixel_raw(int x, int y, uint8_t color) override
  {
    needs_gray(color);
    epd_draw_pixel(x + margin_left, y + margin_top, color, m_frame_buffer);
  }
  // Fast per-glyph blit: no virtual dispatch per pixel, gray decision hoisted
  // out of the loop. Margins applied exactly as draw_pixel so callers are
  // unchanged. On Paper S3 text is solid black (threshold), so gamma is skipped.
  virtual void draw_glyph_alpha(int x, int y, int w, int h, const unsigned char *alpha) override
  {
    int bx = x + margin_left;
    int by = y + margin_top;
#if defined(BOARD_TYPE_PAPER_S3)
    for (int r = 0; r < h; ++r)
    {
      const unsigned char *src = alpha + (long)r * w;
      int yy = by + r;
      for (int c = 0; c < w; ++c)
      {
        if (src[c] > 64)
        {
          epd_draw_pixel(bx + c, yy, m_text_inverted ? 255 : 0, m_frame_buffer);
        }
      }
    }
#else
    needs_gray_flush = true; // antialiased grays
    for (int r = 0; r < h; ++r)
    {
      const unsigned char *src = alpha + (long)r * w;
      int yy = by + r;
      for (int c = 0; c < w; ++c)
      {
        unsigned char a = src[c];
        if (a)
        {
          epd_draw_pixel(bx + c, yy, gamma_curve[(uint8_t)(255 - a)], m_frame_buffer);
        }
      }
    }
#endif
  }
  virtual void draw_circle(int x, int y, int r, uint8_t color = 0)
  {
    needs_gray(color);
    epd_draw_circle(x, y, r, color, m_frame_buffer);
  }
  virtual void flush_display() = 0;
  virtual void flush_area(int x, int y, int width, int height) = 0;

  virtual void clear_screen()
  {
    memset(m_frame_buffer, 0xFF, EPD_WIDTH * EPD_HEIGHT / 2);
  }

  // Bezel safe-area: the acrylic cover's painted edges physically occlude the
  // outermost pixels, so force them blank on every flush. This is a HARDWARE
  // dead-zone (no pixels), independent of the logical drawing margins. Logical
  // space is rotated to 540 (w) x 960 (h). The painted glass edges are NOT
  // symmetric: top 15, bottom 10, left 15, right 12. Coordinates are absolute
  // (margins are NOT applied here). MUST match the DEADZONE_* constants in
  // main.cpp / HomeLayout.h.
  void clear_deadzone()
  {
    const int dz_top = 15;
    const int dz_bottom = 10;
    const int dz_left = 15;
    const int dz_right = 12;
    const int wl = EPD_HEIGHT; // logical width
    const int hl = EPD_WIDTH;  // logical height
    epd_fill_rect({.x = 0, .y = 0, .width = wl, .height = dz_top}, 255, m_frame_buffer);
    epd_fill_rect({.x = 0, .y = hl - dz_bottom, .width = wl, .height = dz_bottom}, 255, m_frame_buffer);
    epd_fill_rect({.x = 0, .y = 0, .width = dz_left, .height = hl}, 255, m_frame_buffer);
    epd_fill_rect({.x = wl - dz_right, .y = 0, .width = dz_right, .height = hl}, 255, m_frame_buffer);
  }
  virtual int get_page_width()
  {
    // don't forget we are rotated
    return EPD_HEIGHT - (margin_left + margin_right);
  }
  virtual int get_page_height()
  {
    // don't forget we are rotated
    return EPD_WIDTH - (margin_top + margin_bottom);
  }
  virtual int get_space_width()
  {
    // When using FreeType for the reading view, use the FreeType
    // font's measurement for the width of a space so that layout and
    // rendering stay in sync.
#ifdef USE_FREETYPE
    if (m_freetype_enabled && m_freetype_font && m_freetype_font->is_valid())
    {
      return m_freetype_font->get_text_width(" ");
    }
#endif
    if (!m_regular_font)
    {
      // Fallback when no bitmap font is available.
      return 0;
    }
    auto space_glyph = epd_get_glyph(m_regular_font, ' ');
    return space_glyph->advance_x;
  }
  virtual int get_line_height()
  {
  #ifdef USE_FREETYPE
    if (m_freetype_enabled && m_freetype_font && m_freetype_font->is_valid())
    {
      return m_freetype_font->get_line_height();
    }
  #endif
    if (!m_regular_font)
    {
      return 0;
    }
    return m_regular_font->advance_y;
  }

  // dehydate a frame buffer to file
  virtual bool dehydrate()
  {
    // compress the buffer to save space and increase performance - writing data is slow!
    size_t compressed_size = 0;
    void *compressed = tdefl_compress_mem_to_heap(m_frame_buffer, EPD_WIDTH * EPD_HEIGHT / 2, &compressed_size, 0);
    if (compressed)
    {
      ESP_LOGI("EPD", "Buffer compressed size: %d", compressed_size);
      FILE *fp = fopen("/fs/front_buffer.z", "w");
      if (fp)
      {
        size_t written = fwrite(compressed, 1, compressed_size, fp);
        fclose(fp);
        free(compressed);
        if (written != compressed_size)
        {
          ESP_LOGI("EPD", "Failed to write to file");
          remove("/fs/front_buffer.z");
          return false;
        }
        ESP_LOGI("EPD", "Buffer saved %d", written);
        return true;
      }
    }
    return false;
  }

  // hydrate a frame buffer
  virtual bool hydrate()
  {
    // load the two buffers - the front and the back buffers
    FILE *fp = fopen("/fs/front_buffer.z", "r");
    bool success = false;
    if (fp)
    {
      fseek(fp, 0, SEEK_END);
      size_t compressed_size = ftell(fp);
      fseek(fp, 0, SEEK_SET);
      if (compressed_size > 0)
      {
        ESP_LOGI("EPD", "Buffer compressed size: %d", compressed_size);
        void *compressed = malloc(compressed_size);
        if (compressed)
        {
          fread(compressed, 1, compressed_size, fp);
          int result = tinfl_decompress_mem_to_mem(m_frame_buffer, EPD_WIDTH * EPD_HEIGHT / 2, compressed, compressed_size, 0);
          if (result == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED)
          {
            ESP_LOGE("EPD", "Failed to decompress front buffer");
          }
          else
          {
            success = true;
            ESP_LOGI("EPD", "Success decompressing %d bytes", EPD_WIDTH * EPD_HEIGHT / 2);
          }
          free(compressed);
        }
        else
        {
          ESP_LOGE("EPD", "Failed to allocate memory for front buffer");
        }
      }
      else
      {
        ESP_LOGE("EPD", "No data to restore");
      }
      fclose(fp);
    }
    else
    {
      ESP_LOGI("EPD", "No front buffer found");
    }
    return success;
  }
  virtual void reset() = 0;

#ifdef USE_FREETYPE
  virtual void set_freetype_font_for_reading(FreeTypeFont *font) override
  {
    m_freetype_font = font;
  }
  virtual void set_freetype_enabled(bool enabled) override
  {
    m_freetype_enabled = enabled && (m_freetype_font != nullptr && m_freetype_font->is_valid());
  }

  virtual int get_reading_font_pixel_height() const override
  {
    if (m_freetype_font && m_freetype_font->is_valid())
    {
      return m_freetype_font->get_pixel_height();
    }
    return 0;
  }

  virtual bool set_reading_font_pixel_height(int pixel_height) override
  {
    if (!m_freetype_font || !m_freetype_font->is_valid())
    {
      return false;
    }
    return m_freetype_font->set_pixel_height(pixel_height);
  }
#endif
};