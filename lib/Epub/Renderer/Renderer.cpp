#include "Renderer.h"
#include "JPEGHelper.h"
#include "PNGHelper.h"
#ifndef UNIT_TEST
#include <esp_log.h>
#else
#define vTaskDelay(t)
#define ESP_LOGE(args...)
#define ESP_LOGI(args...)
#endif

Renderer::~Renderer()
{
  delete png_helper;
  delete jpeg_helper;
}

ImageHelper *Renderer::get_image_helper(const std::string &filename, const uint8_t *data, size_t data_size)
{
  // Prefer magic-byte detection over extension to handle mislabelled
  // resources inside EPUB containers.
  bool looks_jpeg = (data_size > 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF);
  bool looks_png = (data_size > 4 && data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G');

  if (looks_jpeg)
  {
    if (!jpeg_helper)
    {
      jpeg_helper = new JPEGHelper();
    }
    return jpeg_helper;
  }
  if (looks_png)
  {
    if (!png_helper)
    {
      png_helper = new PNGHelper();
    }
    return png_helper;
  }

  // Fallback to file extension if magic bytes are inconclusive. The
  // comparison is done case-insensitively so that resources such as
  // sleep images from "/fs/Pics" still decode correctly even when
  // their extensions are upper-case or mixed-case.
  std::string lower = filename;
  for (char &c : lower)
  {
    if (c >= 'A' && c <= 'Z')
    {
      c = static_cast<char>(c - 'A' + 'a');
    }
  }

  if (lower.find(".jpg") != std::string::npos ||
      lower.find(".jpeg") != std::string::npos)
  {
    if (!jpeg_helper)
    {
      jpeg_helper = new JPEGHelper();
    }
    return jpeg_helper;
  }
  if (lower.find(".png") != std::string::npos)
  {
    if (!png_helper)
    {
      png_helper = new PNGHelper();
    }
    return png_helper;
  }
  return nullptr;
}

namespace
{
// 8x8 Bayer ordered-dither threshold matrix (0..63).
const uint8_t kBayer8[8][8] = {
    {0, 32, 8, 40, 2, 34, 10, 42},
    {48, 16, 56, 24, 50, 18, 58, 26},
    {12, 44, 4, 36, 14, 46, 6, 38},
    {60, 28, 52, 20, 62, 30, 54, 22},
    {3, 35, 11, 43, 1, 33, 9, 41},
    {51, 19, 59, 27, 49, 17, 57, 25},
    {15, 47, 7, 39, 13, 45, 5, 37},
    {63, 31, 55, 23, 61, 29, 53, 21}};
// Contrast-stretch window: map [BLACK_PT..WHITE_PT] -> [0..255] so midtone-heavy
// photos reach true black/white instead of flat grey.
const int BLACK_PT = 30;
const int WHITE_PT = 220;
} // namespace

void Renderer::draw_image_pixel(int x, int y, uint8_t gray)
{
  // Capture mode: store the raw gray into the thumbnail buffer instead of
  // touching the panel (used to bake cover thumbnails for the library cache).
  if (capture_buf)
  {
    if (x >= 0 && y >= 0 && x < capture_w && y < capture_h)
    {
      capture_buf[(long)y * capture_w + x] = gray;
    }
    return;
  }
  if (!image_enhance)
  {
    draw_pixel(x, y, gray);
    return;
  }
  int c = ((int)gray - BLACK_PT) * 255 / (WHITE_PT - BLACK_PT);
  if (c < 0) c = 0;
  if (c > 255) c = 255;
  int t = (int)kBayer8[y & 7][x & 7] - 32; // -32..+31
  int v = c + t * 17 / 32;                 // +-~16 (one 16-level step ~17)
  if (v < 0) v = 0;
  if (v > 255) v = 255;
  int level = (v * 15 + 127) / 255; // 0..15
  draw_pixel_raw(x, y, (uint8_t)(level * 17));
}

void Renderer::draw_image(const std::string &filename, const uint8_t *data, size_t data_size, int x, int y, int width, int height)
{
  ImageHelper *helper = get_image_helper(filename, data, data_size);
  if (!helper ||
      !helper->render(data, data_size, this, x, y, width, height))
  {
    // If an image cannot be decoded or has an unknown type, do not draw
    // any generic cover-style placeholder. Callers that need a fallback
    // (such as the library views) are responsible for drawing their own
    // title cards or other UI elements in the target region.
    (void)image_placeholder_enabled;
    return;
  }
}

bool Renderer::get_image_size(const std::string &filename, const uint8_t *data, size_t data_size, int *width, int *height)
{
  ImageHelper *helper = get_image_helper(filename, data, data_size);
  if (helper && helper->get_size(data, data_size, width, height))
  {
    return true;
  }
  // just provide a dummy height and width so we can do a placeholder
  // for this unknown image typew
  *width = std::min(get_page_width(), get_page_height());
  *height = *width;
  return false;
}

void Renderer::draw_text_box(const std::string &text, int x, int y, int width, int height, bool bold, bool italic)
{
  int length = text.length();
  // fit the text into the box
  int start = 0;
  int end = 1;
  int ypos = 0;
  while (start < length && ypos + get_line_height() < height)
  {
    while (end < length && get_text_width(text.substr(start, end - start).c_str(), bold, italic) < width)
    {
      end++;
    }
    if (get_text_width(text.substr(start, end - start).c_str(), bold, italic) > width)
    {
      end--;
    }
    draw_text(x, y + ypos, text.substr(start, end - start).c_str(), bold, italic);
    ypos += get_line_height();
    start = end;
    end = start + 1;
  }
}
