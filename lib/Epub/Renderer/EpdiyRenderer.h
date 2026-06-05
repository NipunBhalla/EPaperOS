#pragma once
#include <esp_log.h>

#if defined(BOARD_TYPE_PAPER_S3)
#include <epdiy.h>
#include <epd_highlevel.h>
#include <epd_board.h>
#if !defined(USE_LILYGO_S3_BOARD)
// Custom epdiy board definition for M5Stack Paper S3
extern const EpdBoardDefinition paper_s3_board;
#endif
#else
#include <epd_driver.h>
#include <epd_highlevel.h>
#endif

#include <math.h>
#include "EpdiyFrameBufferRenderer.h"
#include "miniz.h"

class EpdiyRenderer : public EpdiyFrameBufferRenderer
{
private:
  EpdiyHighlevelState m_hl;

  // Shared ghosting budget across ALL partial (DU) refresh sources: page turns,
  // keyboard keypresses, task-line toggles, status bar. After this many DU
  // updates without a full grayscale pass, force one GC16 flash to clear the
  // accumulated ghost. Not per-widget — one counter for the whole panel.
  static const int PARTIAL_REFRESH_LIMIT = 12;
  int m_partial_updates = 0;

public:
  EpdiyRenderer(
      const EpdFont *regular_font,
      const EpdFont *bold_font,
      const EpdFont *italic_font,
      const EpdFont *bold_italic_font,
      const uint8_t *busy_icon,
      int busy_icon_width,
      int busy_icon_height)
      : EpdiyFrameBufferRenderer(regular_font, bold_font, italic_font, bold_italic_font, busy_icon, busy_icon_width, busy_icon_height)
  {
    // start up the EPD
#if defined(BOARD_TYPE_PAPER_S3)
    // For Paper S3 we use the new epdiy API with a custom board definition
#if defined(USE_LILYGO_S3_BOARD)
    // LilyGo T5 4.7" S3 Pro: PCA9555 IO-expander + TPS65185 PMIC, EPD bus per
    // components/epdiy/src/board/lilygo_board_s3.c (data 5,6,7,15,16,17,18,8;
    // CKV48 STH41 LEH42 STV45 CKH4; I2C SDA39/SCL40).
    epd_set_board(&lilygo_board_s3);
#else
    epd_set_board(&paper_s3_board);
#endif
    epd_init(epd_current_board(), &ED047TC2, EPD_OPTIONS_DEFAULT);
    // The C fallback for the ESP32-S3 LUT path is slower than the original
    // vector assembly, so a 20 MHz pixel clock can cause line buffer underruns
    // (EPD_DRAW_EMPTY_LINE_QUEUE). Run the LCD at 5 MHz instead for stability
    // on lower CPU clock configurations.
    epd_set_lcd_pixel_clock_MHz(5);
#else
    // Legacy epdiy API used by ESP32-based boards
    epd_init(EPD_OPTIONS_DEFAULT);
#endif

    m_hl = epd_hl_init(EPD_BUILTIN_WAVEFORM);
    // first set full screen to white
    epd_hl_set_all_white(&m_hl);
    m_frame_buffer = epd_hl_get_framebuffer(&m_hl);

#if !defined(CONFIG_EPD_BOARD_REVISION_LILYGO_T5_47) || defined(BOARD_TYPE_PAPER_S3)
    epd_poweron();
#endif
  }
  ~EpdiyRenderer()
  {
    epd_deinit();
  }
  void flush_display()
  {
#if defined(BOARD_TYPE_PAPER_S3)
    clear_deadzone(); // blank the bezel-occluded edge pixels before pushing
#endif
    // GC16 when content explicitly needs grayscale, OR when the partial-update
    // budget is spent (de-ghost). A DU pass adds to the budget; a GC16 clears it.
    bool gray = needs_gray_flush || m_partial_updates >= PARTIAL_REFRESH_LIMIT;
    epd_hl_update_screen(&m_hl, gray ? MODE_GC16 : MODE_DU, temperature);
    needs_gray_flush = false;
    if (gray)
    {
      m_partial_updates = 0;
    }
    else
    {
      m_partial_updates++;
    }
  }
  void flush_area(int x, int y, int width, int height)
  {
#if defined(BOARD_TYPE_PAPER_S3)
    clear_deadzone(); // blank the bezel-occluded edge pixels before pushing
#endif
    // Partial updates accumulate ghosting; once the budget is spent, promote to
    // a full GC16 clear instead of yet another DU area update.
    if (++m_partial_updates >= PARTIAL_REFRESH_LIMIT)
    {
      m_partial_updates = 0;
      epd_hl_update_screen(&m_hl, MODE_GC16, temperature);
      return;
    }
    epd_hl_update_area(&m_hl, MODE_DU, temperature, {.x = x, .y = y, .width = width, .height = height});
  }
  virtual void reset()
  {
    ESP_LOGI("EPD", "Full clear");
    epd_fullclear(&m_hl, temperature);
    m_partial_updates = 0; // full clear removes all ghosting
  };
  // deep sleep helper - retrieve any state from disk after wake
  virtual bool hydrate()
  {
    ESP_LOGI("EPD", "Hydrating EPD");
    if (EpdiyFrameBufferRenderer::hydrate())
    {
      // just memcopy the front buffer to the back buffer - they should be exactly the same
      memcpy(m_hl.back_fb, m_frame_buffer, EPD_WIDTH * EPD_HEIGHT / 2);
      ESP_LOGI("EPD", "Hydrated EPD");
      return true;
    }
    else
    {
      ESP_LOGI("EPD", "Hydrate EPD failed");
      reset();
      return false;
    }
  };
};