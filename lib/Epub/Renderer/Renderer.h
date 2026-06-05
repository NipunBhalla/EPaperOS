#pragma once

#include <string>

class ImageHelper;

#ifdef USE_FREETYPE
class FreeTypeFont;
#endif

#define MAX_WORD_LENGTH 100

class Renderer
{
private:
  ImageHelper *png_helper = nullptr;
  ImageHelper *jpeg_helper = nullptr;

  ImageHelper *get_image_helper(const std::string &filename, const uint8_t *data, size_t data_size);

  // Optional capture target. When set (begin_capture), draw_image_pixel writes
  // the raw 8-bit gray into this buffer (row-major, capture_w*capture_h) instead
  // of the panel. Used to decode a cover JPEG once into a small thumbnail buffer
  // that the library then caches to SD and blits back cheaply on later page
  // turns (no zip read / JPEG decode per turn).
  uint8_t *capture_buf = nullptr;
  int capture_w = 0;
  int capture_h = 0;

protected:
  int margin_top = 0;
  int margin_bottom = 0;
  int margin_left = 0;
  int margin_right = 0;
  bool image_placeholder_enabled = true;
  // When true, photographic images (covers / sleep image) are contrast-stretched
  // and ordered-dithered for punchier e-ink output. Off for in-book images/text.
  bool image_enhance = false;

public:
  virtual ~Renderer();
  void set_image_placeholder_enabled(bool enabled) { image_placeholder_enabled = enabled; }
  void set_image_enhance(bool enabled) { image_enhance = enabled; }
  bool get_image_enhance() const { return image_enhance; }
  virtual void draw_image(const std::string &filename, const uint8_t *data, size_t data_size, int x, int y, int width, int height);
  virtual bool get_image_size(const std::string &filename, const uint8_t *data, size_t data_size, int *width, int *height);
  virtual void draw_pixel(int x, int y, uint8_t color) = 0;
  // Like draw_pixel but writes the gray level straight to the framebuffer with
  // no gamma curve applied (the enhanced image path does its own tone mapping).
  // Default falls back to draw_pixel for renderers that don't override it.
  virtual void draw_pixel_raw(int x, int y, uint8_t color) { draw_pixel(x, y, color); }
  // Plot one decoded image pixel (8-bit gray). When image_enhance is on, applies
  // a contrast stretch + 8x8 Bayer ordered dither into the panel's 16 levels;
  // otherwise a plain gamma-corrected draw_pixel. Shared by JPEG and PNG helpers.
  void draw_image_pixel(int x, int y, uint8_t gray);
  // Redirect subsequent draw_image_pixel writes into buf (capture) / restore
  // normal panel drawing (end). Caller owns buf; it must be w*h bytes.
  void begin_capture(uint8_t *buf, int w, int h)
  {
    capture_buf = buf;
    capture_w = w;
    capture_h = h;
  }
  void end_capture()
  {
    capture_buf = nullptr;
    capture_w = 0;
    capture_h = 0;
  }
  // Blit a row-major 8-bit gray bitmap to the panel at logical (x,y). Goes
  // through draw_image_pixel so tone mapping matches the live JPEG path.
  void draw_gray_bitmap(int x, int y, int w, int h, const uint8_t *buf)
  {
    for (int r = 0; r < h; ++r)
    {
      const uint8_t *src = buf + (long)r * w;
      for (int c = 0; c < w; ++c)
      {
        draw_image_pixel(x + c, y + r, src[c]);
      }
    }
  }
  // Blit an 8-bit alpha glyph bitmap (row-major, w*h, top-left at logical x,y)
  // in one call. Lets the font wrapper hand a whole glyph to the renderer so
  // contrast/gamma + the gray-flush decision happen WITHOUT a virtual call per
  // pixel (the per-page text-render hot path). Default = per-pixel fallback.
  virtual void draw_glyph_alpha(int x, int y, int w, int h, const unsigned char *alpha)
  {
    for (int r = 0; r < h; ++r)
    {
      const unsigned char *src = alpha + (long)r * w;
      for (int c = 0; c < w; ++c)
      {
        unsigned char a = src[c];
        if (a)
        {
          draw_pixel(x + c, y + r, (uint8_t)(255 - a));
        }
      }
    }
  }
  virtual int get_text_width(const char *text, bool bold = false, bool italic = false) = 0;
  virtual void draw_text(int x, int y, const char *text, bool bold = false, bool italic = false) = 0;
  // When set, subsequent draw_text() renders glyphs in white (for inverted
  // highlight strips over a black fill). Default no-op; only the framebuffer
  // renderer honours it. Callers must reset it to false afterwards.
  virtual void set_text_inverted(bool /*inverted*/) {}
  virtual void draw_text_box(const std::string &text, int x, int y, int width, int height, bool bold = false, bool italic = false);
  virtual void draw_rect(int x, int y, int width, int height, uint8_t color = 0) = 0;
  virtual void draw_triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint8_t color) = 0;
  virtual void draw_circle(int x, int y, int r, uint8_t color = 0) = 0;

  virtual void fill_triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint8_t color) = 0;
  virtual void fill_rect(int x, int y, int width, int height, uint8_t color = 0) = 0;
  virtual void fill_circle(int x, int y, int r, uint8_t color = 0) = 0;
  virtual void needs_gray(uint8_t color) = 0;
  virtual bool has_gray() = 0;
  virtual void show_busy() = 0;
  virtual void show_img(int x, int y, int width, int height, const uint8_t *img_buffer) = 0;
  virtual void clear_screen() = 0;
  virtual void flush_display(){};
  virtual void flush_area(int x, int y, int width, int height){};

#ifdef USE_FREETYPE
  // Optional hooks for FreeType-backed rendering. Default
  // implementations are no-ops so non-FreeType renderers or builds
  // continue to behave as before.
  virtual void set_freetype_font_for_reading(FreeTypeFont *font) {}
  virtual void set_freetype_enabled(bool enabled) {}

  // Optional accessors for the FreeType-backed reading font size so
  // that callers (e.g. TOC or menus) can temporarily adjust it.
  virtual int get_reading_font_pixel_height() const { return 0; }
  virtual bool set_reading_font_pixel_height(int /*pixel_height*/) { return false; }
#endif

  virtual int get_page_width() = 0;
  virtual int get_page_height() = 0;
  virtual int get_space_width() = 0;
  virtual int get_line_height() = 0;
  // set margins
  void set_margin_top(int margin_top) { this->margin_top = margin_top; }
  void set_margin_bottom(int margin_bottom) { this->margin_bottom = margin_bottom; }
  void set_margin_left(int margin_left) { this->margin_left = margin_left; }
  void set_margin_right(int margin_right) { this->margin_right = margin_right; }
  // read margins (drawing primitives add these; flush_area needs absolute px)
  int get_margin_top() const { return margin_top; }
  int get_margin_left() const { return margin_left; }
  int get_margin_right() const { return margin_right; }
  int get_margin_bottom() const { return margin_bottom; }
  // deep sleep helper - persist any state to disk that may be needed on wake
  virtual bool dehydrate() { return false; };
  // deep sleep helper - retrieve any state from disk after wake
  virtual bool hydrate() { return false; };
  // really really clear the screen
  virtual void reset(){};

  // Default operating temperature (deg C) used by epdiy waveform selection on
  // Paper S3. This can be overridden by board-specific code if needed.
  uint8_t temperature = 20;
};