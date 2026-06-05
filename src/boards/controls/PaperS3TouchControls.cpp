#include "PaperS3TouchControls.h"

#include "Renderer/Renderer.h"
#include "EpubList/State.h"
#include "../../ui/UIState.h"
#include "../../ui/HomeLayout.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include <driver/i2c.h>
#include <esp_rom_sys.h>

// Paper S3 GT911 touch is configured as a 540x960 coordinate space (x,y). We
// always interpret touches in this space and map them onto the 540x960 logical
// page used by the rotated 960x540 e-paper panel.

static const char *TAG = "PaperS3Touch";

// UIState comes from ui/UIState.h, shared with main.cpp. (Previously this file
// declared its own enum with different ordering, which mis-aligned the integer
// values and made the touch layer mis-identify the active screen.) Screens
// without a tap branch below (HOME_MENU, TASKS, SETTINGS, KEYBOARD) fall
// through to NONE until D2 wires their region hit-testing.
extern UIState ui_state;
extern EpubListState epub_list_state;
extern EpubTocState epub_index_state;
extern int reader_menu_selected;
extern bool invert_tap_zones;
extern bool reader_menu_advanced;
extern int home_selected; // 0 = Tasks, 1 = Books, 2 = Settings (home menu hit-test)

// Logical-page coordinates of the most recent raw tap. Published here just
// before a TAP action is queued so coordinate-driven screens (Settings,
// keyboard) can hit-test without a richer event payload.
extern volatile int g_tap_x;
extern volatile int g_tap_y;

// GT911 wiring. Defaults are the M5 Paper S3 pins (from M5GFX / M5PaperS3
// config). Override per board via build_flags, e.g. for the LilyGo T5 4.7" S3
// whose GT911 GPIOs MUST be confirmed against that board's schematic:
//   -D GT911_SDA_GPIO=GPIO_NUM_xx  -D GT911_SCL_GPIO=GPIO_NUM_xx
//   -D GT911_INT_GPIO=GPIO_NUM_xx
#ifndef GT911_SDA_GPIO
#define GT911_SDA_GPIO GPIO_NUM_41
#endif
#ifndef GT911_SCL_GPIO
#define GT911_SCL_GPIO GPIO_NUM_42
#endif
#ifndef GT911_INT_GPIO
#define GT911_INT_GPIO GPIO_NUM_48
#endif
// GT911 RST. Required: the controller stays held in reset (and never drives
// I2C) until RST is taken high, and it latches its I2C address from the INT
// level on RST's rising edge. -1 disables the reset sequence (M5 default,
// where RST is not wired to a GPIO).
#ifndef GT911_RST_GPIO
#define GT911_RST_GPIO (-1)
#endif
static const gpio_num_t PAPERS3_GT911_SDA_GPIO = GT911_SDA_GPIO;
static const gpio_num_t PAPERS3_GT911_SCL_GPIO = GT911_SCL_GPIO;
static const gpio_num_t PAPERS3_GT911_INT_GPIO = GT911_INT_GPIO;
static const int PAPERS3_GT911_RST_GPIO = GT911_RST_GPIO;
static const i2c_port_t PAPERS3_GT911_I2C_PORT = I2C_NUM_0;

// Drive the GT911 reset/address-select sequence. INT is held low across the
// RST rising edge to select I2C address 0x5D (the LilyGo default), then both
// lines are released so INT can serve as the touch interrupt input.
static void gt911_reset_sequence()
{
  if (PAPERS3_GT911_RST_GPIO < 0)
  {
    return; // no RST pin wired; nothing to do
  }
  gpio_num_t rst = (gpio_num_t)PAPERS3_GT911_RST_GPIO;
  gpio_num_t intp = PAPERS3_GT911_INT_GPIO;

  gpio_config_t io = {};
  io.pin_bit_mask = (1ULL << rst) | (1ULL << intp);
  io.mode = GPIO_MODE_OUTPUT;
  io.pull_up_en = GPIO_PULLUP_DISABLE;
  io.pull_down_en = GPIO_PULLDOWN_DISABLE;
  io.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&io);

  gpio_set_level(rst, 0);    // assert reset
  gpio_set_level(intp, 0);   // INT low -> address 0x5D
  vTaskDelay(pdMS_TO_TICKS(10));
  gpio_set_level(intp, 0);   // hold address selection
  esp_rom_delay_us(120);
  gpio_set_level(rst, 1);    // release reset; address latched on this edge
  vTaskDelay(pdMS_TO_TICKS(8));
  gpio_set_level(intp, 0);
  vTaskDelay(pdMS_TO_TICKS(50));

  // Release INT to a floating input so the controller can pulse it.
  gpio_set_direction(intp, GPIO_MODE_INPUT);
  gpio_set_pull_mode(intp, GPIO_FLOATING);
  vTaskDelay(pdMS_TO_TICKS(50));
}

// Gesture sensitivity profile (updated via set_gesture_profile).
static uint16_t s_swipe_threshold = 100;
static uint16_t s_longpress_move_threshold = 30;
static uint32_t s_longpress_ms = 600;

static esp_err_t gt911_write_reg(uint8_t addr, uint16_t reg, const uint8_t *data, size_t len)
{
  i2c_cmd_handle_t cmd = i2c_cmd_link_create();
  if (!cmd)
  {
    return ESP_FAIL;
  }

  uint8_t reg_hi = reg >> 8;
  uint8_t reg_lo = reg & 0xFF;

  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
  i2c_master_write_byte(cmd, reg_hi, true);
  i2c_master_write_byte(cmd, reg_lo, true);
  if (data && len)
  {
    i2c_master_write(cmd, (uint8_t *)data, len, true);
  }
  i2c_master_stop(cmd);

  esp_err_t ret = i2c_master_cmd_begin(PAPERS3_GT911_I2C_PORT, cmd, pdMS_TO_TICKS(100));
  i2c_cmd_link_delete(cmd);
  return ret;
}

static esp_err_t gt911_read_reg(uint8_t addr, uint16_t reg, uint8_t *data, size_t len)
{
  if (!data || !len)
  {
    return ESP_ERR_INVALID_ARG;
  }

  i2c_cmd_handle_t cmd = i2c_cmd_link_create();
  if (!cmd)
  {
    return ESP_FAIL;
  }

  uint8_t reg_hi = reg >> 8;
  uint8_t reg_lo = reg & 0xFF;

  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
  i2c_master_write_byte(cmd, reg_hi, true);
  i2c_master_write_byte(cmd, reg_lo, true);

  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_READ, true);

  if (len > 1)
  {
    i2c_master_read(cmd, data, len - 1, I2C_MASTER_ACK);
  }
  i2c_master_read_byte(cmd, data + len - 1, I2C_MASTER_NACK);
  i2c_master_stop(cmd);

  esp_err_t ret = i2c_master_cmd_begin(PAPERS3_GT911_I2C_PORT, cmd, pdMS_TO_TICKS(100));
  i2c_cmd_link_delete(cmd);
  return ret;
}
PaperS3TouchControls::PaperS3TouchControls(Renderer *renderer, ActionCallback_t on_action)
    : on_action(on_action), renderer(renderer)
{
  // 1. Keep this here! The reset sequence configures the hardware lines.
  gt911_reset_sequence();

// 2. Wrap the configuration and installation blocks in a conditional check
#if !defined(USE_LILYGO_S3_BOARD) && !defined(LILYGO_T5S3)
  // Configure I2C using the legacy API to avoid conflicts with driver_ng.
  i2c_config_t conf = {};
  conf.mode = I2C_MODE_MASTER;
  conf.sda_io_num = PAPERS3_GT911_SDA_GPIO;
  conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
  conf.scl_io_num = PAPERS3_GT911_SCL_GPIO;
  conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
  conf.master.clk_speed = 400000;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 3, 0)
  conf.clk_flags = 0;
#endif

  esp_err_t err = i2c_param_config(PAPERS3_GT911_I2C_PORT, &conf);
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "i2c_param_config failed: %d", err);
    return;
  }

  err = i2c_driver_install(PAPERS3_GT911_I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
  {
    ESP_LOGE(TAG, "i2c_driver_install failed: %d", err);
    return;
  }
#else
  ESP_LOGI(TAG, "Skipping I2C peripheral installation. Bus already initialized by epdiy.");
#endif

  // 3. Keep the address detection untouched so it runs on the shared bus!
  uint8_t buf = 0;
  if (gt911_read_reg(0x14, 0x8140, &buf, 1) == ESP_OK)
  {
    i2c_addr = 0x14;
    driver_ok = true;
  }
  else if (gt911_read_reg(0x5D, 0x8140, &buf, 1) == ESP_OK)
  {
    i2c_addr = 0x5D;
    driver_ok = true;
  }
  else
  {
    ESP_LOGE(TAG, "GT911 not found on I2C bus");
    driver_ok = false;
    return;
  }

  ESP_LOGI(TAG, "GT911 detected at 0x%02X", i2c_addr);

  // Create a polling task to read touch events.
  if (xTaskCreatePinnedToCore(&PaperS3TouchControls::touchTask, "papers3_touch", 4096, this, 1, nullptr, 1) != pdPASS)
  {
    ESP_LOGE(TAG, "Failed to create touch task");
    driver_ok = false;
  }
}


void PaperS3TouchControls::set_gesture_profile(int profile_index)
{
  switch (profile_index)
  {
  case 0: // low sensitivity: require larger movement/press
    s_swipe_threshold = 120;
    s_longpress_move_threshold = 40;
    s_longpress_ms = 800;
    break;
  case 2: // high sensitivity
    s_swipe_threshold = 70;
    s_longpress_move_threshold = 20;
    s_longpress_ms = 500;
    break;
  case 1:
  default: // medium (default)
    s_swipe_threshold = 100;
    s_longpress_move_threshold = 30;
    s_longpress_ms = 600;
    break;
  }
}

void PaperS3TouchControls::render(Renderer *renderer)
{
  (void)renderer;
}

void PaperS3TouchControls::renderPressedState(Renderer *renderer, UIAction action, bool state)
{
  (void)renderer;
  (void)action;
  (void)state;
}

void PaperS3TouchControls::prepare_for_deep_sleep()
{
  if (!driver_ok)
  {
    return;
  }
  // Stop the touch polling task so it can't wake the GT911 back up after we
  // send the sleep command. Two poll cycles (2×30ms) is enough for the task
  // to see the flag and exit its loop.
  m_stop_polling = true;
  vTaskDelay(pdMS_TO_TICKS(70));
  // GT911 command register 0x8040: value 0x05 = enter sleep mode (~100µA vs ~20mA active).
  uint8_t sleep_cmd = 0x05;
  gt911_write_reg(i2c_addr, 0x8040, &sleep_cmd, 1);
}

void PaperS3TouchControls::touchTask(void *param)
{
  auto *self = static_cast<PaperS3TouchControls *>(param);
  self->loop();
  vTaskDelete(NULL);
}

bool PaperS3TouchControls::readTouchPoint(uint16_t *x, uint16_t *y, uint8_t *points, bool *home_key)
{
  if (home_key)
  {
    *home_key = false;
  }
  if (!driver_ok || !x || !y)
  {
    return false;
  }

  // GT911 status register at 0x814E. Bit 7 (0x80) = buffer ready, bit 4 (0x10)
  // = capacitive home key pressed (LilyGo T5 4.7" S3 GT911 reports the dedicated
  // home button here, in the same byte as the touch-point count in bits 0-3).
  uint8_t status = 0;
  if (gt911_read_reg(i2c_addr, 0x814E, &status, 1) != ESP_OK)
  {
    return false;
  }

  // Capture the home-key bit FIRST, before the buffer-ready gate. The GT911
  // keeps bit 0x10 asserted for as long as the capacitive home key is held, but
  // it does NOT re-set the buffer-ready bit (0x80) every refresh cycle. Gating
  // the key read behind 0x80 would only catch the initial press moment, making a
  // sustained hold look momentary and breaking long-press detection.
  if (home_key)
  {
    *home_key = (status & 0x10) != 0;
  }

  if (!(status & 0x80))
  {
    // No new coordinate data this cycle (key state already captured above).
    return false;
  }

  uint8_t count = status & 0x0F;
  if (count == 0)
  {
    // Key-only event. The home-key bit (0x10) is latched and the GT911 is slow
    // (~120 ms) to re-assert it after a clear, so we must NOT clear it on every
    // ~20 ms poll: doing so means a genuine hold never re-shows the bit and the
    // long-press never accumulates. Leave it latched while the key reads as held
    // (so the duration can build up); the long-press fire path itself does a
    // single clear-and-reconfirm to reject a lingering bit left by a quick tap.
    // Once the long-press has fired we resume clearing so a stuck bit after the
    // full-refresh blackout can't wedge all touch input.
    if ((status & 0x10) && !home_key_longfired)
    {
      return false;
    }
    uint8_t zero = 0;
    gt911_write_reg(i2c_addr, 0x814E, &zero, 1);
    return false;
  }

  if (points)
  {
    *points = count;
  }

  // Read the first touch point from 0x8150 (X, Y, etc.).
  uint8_t data[4] = {0};
  if (gt911_read_reg(i2c_addr, 0x8150, data, sizeof(data)) != ESP_OK)
  {
    return false;
  }

  *x = (uint16_t)(data[1] << 8 | data[0]);
  *y = (uint16_t)(data[3] << 8 | data[2]);

  // Clear the status flag so the controller can update.
  uint8_t zero = 0;
  gt911_write_reg(i2c_addr, 0x814E, &zero, 1);

  return true;
}

UIAction PaperS3TouchControls::mapTapToAction(uint16_t x, uint16_t y)
{
  // The GT911 on PaperS3 is configured by M5GFX as 540x960.
  const uint16_t touch_width = 540;
  const uint16_t touch_height = 960;

  // The home button is the GT911's dedicated capacitive key, detected via the
  // status register (bit 0x10) in loop() — NOT an out-of-display coordinate.
  // Taps reported here are always real screen coordinates within 540x960.
  if (y >= touch_height || x >= touch_width)
  {
    return NONE;
  }

  int epd_width = renderer->get_page_width();
  int epd_height = renderer->get_page_height();
  if (epd_width <= 0 || epd_height <= 0)
  {
    return NONE;
  }

  // Map raw touch (full panel) to the active screen's CONTENT space. Drawing
  // primitives place content at (logical + margin), so invert that: scale the
  // raw point to absolute panel pixels, then subtract the current margins. This
  // keeps taps aligned even when the status bar reserves a top margin.
  int margin_left = renderer->get_margin_left();
  int margin_top = renderer->get_margin_top();
  int full_w = epd_width + margin_left + renderer->get_margin_right();
  int full_h = epd_height + margin_top + renderer->get_margin_bottom();
  int logical_x = static_cast<int>(x) * full_w / touch_width - margin_left;
  int logical_y = static_cast<int>(y) * full_h / touch_height - margin_top;

  // Home menu: two stacked tiles -- "Tasks" (0, top) and "Books" (1, bottom) --
  // plus a "Settings" (2) strip along the bottom. handleHome() draws with all
  // margins zeroed, so epd_width/height are the full panel and logical_x/y are
  // absolute panel pixels matching ui::compute_home_layout exactly. A tap inside
  // a tile/strip selects and confirms it (SELECT) so a single tap opens it.
  if (ui_state == HOME_MENU)
  {
    int lh = renderer->get_line_height();
    ui::HomeLayout L = ui::compute_home_layout(epd_width, epd_height, lh);
    if (L.tasks.contains(logical_x, logical_y))
    {
      home_selected = 0;
      return SELECT;
    }
    if (L.books.contains(logical_x, logical_y))
    {
      home_selected = 1;
      return SELECT;
    }
    if (L.settings.contains(logical_x, logical_y))
    {
      home_selected = 2;
      return SELECT;
    }
    return NONE;
  }

  // Settings and Tasks are coordinate-driven (sub-tabs, field rows, task rows,
  // keyboard). Hand the logical tap to the UI layer as a TAP; the active
  // screen hit-tests it.
  if (ui_state == SETTINGS || ui_state == KEYBOARD || ui_state == TASKS)
  {
    g_tap_x = logical_x;
    g_tap_y = logical_y;
    return TAP;
  }

  int bottom_bar_height = EPUB_LIST_BOTTOM_BAR_HEIGHT;
  int bar_top = epd_height - bottom_bar_height;
  if (bar_top < 0)
  {
    bar_top = epd_height;
  }

  // On the EPUB list screen, interpret taps on the content area as
  // selecting a book (grid or list) and taps on the bottom bar as
  // page navigation or view toggle.
  if (ui_state == SELECTING_EPUB && epub_list_state.is_loaded && epub_list_state.num_epubs > 0)
  {
    int content_height = epd_height;
    if (bottom_bar_height > 0 && bottom_bar_height < epd_height)
    {
      content_height = epd_height - bottom_bar_height;
    }

    // Bottom bar: "<<      <      Page X of Y      >      >>" textual
    // navigation. Use the same geometry as the renderer so the touch
    // hitboxes track the visual arrow positions. Double arrows (outer
    // zones) move by a page; single arrows (inner zones) move selection
    // by one item.
    if (bottom_bar_height > 0 && logical_y >= bar_top)
    {
      int items_per_page = epub_list_state.use_grid_view ? (EPUB_GRID_ROWS * EPUB_GRID_COLUMNS) : EPUB_LIST_ITEMS_PER_PAGE;
      if (items_per_page <= 0)
      {
        items_per_page = 1;
      }

      // Match the five equal-width navigation regions rendered in
      // EpubList::render(): [<<] [<] [Page X of Y] [>] [>>].
      int columns = 5;
      int col_width = epd_width / columns;
      if (col_width <= 0)
      {
        col_width = 1;
      }

      int ld_zone_start = 0;
      int ld_zone_end = ld_zone_start + col_width;

      int ls_zone_start = ld_zone_end;
      int ls_zone_end = ls_zone_start + col_width;

      int center_zone_start = ls_zone_end;
      int center_zone_end = center_zone_start + col_width;

      int rs_zone_start = center_zone_end;
      int rs_zone_end = rs_zone_start + col_width;

      int rd_zone_start = rs_zone_end;
      int rd_zone_end = epd_width; // consume any remainder to reach the edge

      if (logical_x < ld_zone_end)
      {
        // "<<" – jump back by one page.
        int new_index = epub_list_state.selected_item - items_per_page;
        if (new_index < 0)
        {
          new_index = 0;
        }
        epub_list_state.selected_item = new_index;
        return LAST_INTERACTION;
      }
      if (logical_x < ls_zone_end)
      {
        // "<" – move selection to previous item.
        int new_index = epub_list_state.selected_item - 1;
        if (new_index < 0)
        {
          new_index = 0;
        }
        epub_list_state.selected_item = new_index;
        return LAST_INTERACTION;
      }
      if (logical_x < center_zone_end)
      {
        // Taps on the "Page X of Y" text do not change selection.
        return NONE;
      }
      if (logical_x < rs_zone_end)
      {
        // ">" – move selection to next item.
        int new_index = epub_list_state.selected_item + 1;
        if (new_index >= epub_list_state.num_epubs)
        {
          new_index = epub_list_state.num_epubs - 1;
        }
        if (new_index < 0)
        {
          new_index = 0;
        }
        epub_list_state.selected_item = new_index;
        return LAST_INTERACTION;
      }
      if (logical_x < rd_zone_end)
      {
        // ">>" – jump forward by one page.
        int new_index = epub_list_state.selected_item + items_per_page;
        if (new_index >= epub_list_state.num_epubs)
        {
          new_index = epub_list_state.num_epubs - 1;
        }
        if (new_index < 0)
        {
          new_index = 0;
        }
        epub_list_state.selected_item = new_index;
        return LAST_INTERACTION;
      }
      return NONE;
    }

    // Content area: tap a book cover or row to open.
    if (logical_y >= content_height)
    {
      return NONE;
    }

    int items_per_page = epub_list_state.use_grid_view ? (EPUB_GRID_ROWS * EPUB_GRID_COLUMNS) : EPUB_LIST_ITEMS_PER_PAGE;
    if (items_per_page <= 0)
    {
      items_per_page = 1;
    }
    int current_page = 0;
    if (epub_list_state.num_epubs > 0)
    {
      current_page = epub_list_state.selected_item / items_per_page;
    }

    if (epub_list_state.use_grid_view)
    {
      int cell_width = epd_width / EPUB_GRID_COLUMNS;
      int cell_height = content_height / EPUB_GRID_ROWS;
      if (cell_width <= 0 || cell_height <= 0)
      {
        return NONE;
      }
      int row = logical_y / cell_height;
      int col = logical_x / cell_width;
      if (row < 0)
      {
        row = 0;
      }
      if (row >= EPUB_GRID_ROWS)
      {
        row = EPUB_GRID_ROWS - 1;
      }
      if (col < 0)
      {
        col = 0;
      }
      if (col >= EPUB_GRID_COLUMNS)
      {
        col = EPUB_GRID_COLUMNS - 1;
      }
      int index_in_page = row * EPUB_GRID_COLUMNS + col;
      if (index_in_page >= items_per_page)
      {
        return NONE;
      }
      int index = current_page * items_per_page + index_in_page;
      if (index >= epub_list_state.num_epubs)
      {
        return NONE;
      }
      epub_list_state.selected_item = index;
      return SELECT;
    }
    else
    {
      if (content_height <= 0)
      {
        return NONE;
      }
      int cell_height = content_height / EPUB_LIST_ITEMS_PER_PAGE;
      if (cell_height <= 0)
      {
        cell_height = 1;
      }
      int row_in_page = logical_y / cell_height;
      if (row_in_page < 0)
      {
        row_in_page = 0;
      }
      if (row_in_page >= EPUB_LIST_ITEMS_PER_PAGE)
      {
        row_in_page = EPUB_LIST_ITEMS_PER_PAGE - 1;
      }

      int index = current_page * EPUB_LIST_ITEMS_PER_PAGE + row_in_page;
      if (index >= epub_list_state.num_epubs)
      {
        index = epub_list_state.num_epubs - 1;
      }
      if (index < 0)
      {
        index = 0;
      }
      epub_list_state.selected_item = index;
      return SELECT;
    }
  }

  // In the table-of-contents view, interpret taps on the content
  // area as selecting the tapped TOC entry (row) and opening that
  // chapter, and taps on the bottom bar as paging the TOC.
  if (ui_state == SELECTING_TABLE_CONTENTS)
  {
    int content_height = epd_height;
    if (bottom_bar_height > 0 && bottom_bar_height < epd_height)
    {
      content_height = epd_height - bottom_bar_height;
    }

    if (bottom_bar_height > 0 && logical_y >= bar_top)
    {
      const int items_per_page = EPUB_TOC_ITEMS_PER_PAGE;
      if (epub_index_state.num_items <= 0)
      {
        return NONE;
      }

      // Match the five equal-width navigation regions rendered in
      // EpubToc::render(): [<<] [<] [Page X of Y] [>] [>>].
      int columns = 5;
      int col_width = epd_width / columns;
      if (col_width <= 0)
      {
        col_width = 1;
      }

      int ld_zone_start = 0;
      int ld_zone_end = ld_zone_start + col_width;

      int ls_zone_start = ld_zone_end;
      int ls_zone_end = ls_zone_start + col_width;

      int center_zone_start = ls_zone_end;
      int center_zone_end = center_zone_start + col_width;

      int rs_zone_start = center_zone_end;
      int rs_zone_end = rs_zone_start + col_width;

      int rd_zone_start = rs_zone_end;
      int rd_zone_end = epd_width; // consume any remainder to reach the edge

      if (logical_x < ld_zone_end)
      {
        // "<<" – jump back by one page.
        int new_index = epub_index_state.selected_item - items_per_page;
        if (new_index < 0)
        {
          new_index = 0;
        }
        epub_index_state.selected_item = new_index;
        return LAST_INTERACTION;
      }
      if (logical_x < ls_zone_end)
      {
        // "<" – move selection to previous TOC item.
        int new_index = epub_index_state.selected_item - 1;
        if (new_index < 0)
        {
          new_index = 0;
        }
        epub_index_state.selected_item = new_index;
        return LAST_INTERACTION;
      }
      if (logical_x < center_zone_end)
      {
        // Taps on the "Page X of Y" text do not change selection.
        return NONE;
      }
      if (logical_x < rs_zone_end)
      {
        // ">" – move selection to next TOC item.
        int new_index = epub_index_state.selected_item + 1;
        if (new_index >= epub_index_state.num_items)
        {
          new_index = epub_index_state.num_items - 1;
        }
        if (new_index < 0)
        {
          new_index = 0;
        }
        epub_index_state.selected_item = new_index;
        return LAST_INTERACTION;
      }
      if (logical_x < rd_zone_end)
      {
        // ">>" – jump forward by one page.
        int new_index = epub_index_state.selected_item + items_per_page;
        if (new_index >= epub_index_state.num_items)
        {
          new_index = epub_index_state.num_items - 1;
        }
        if (new_index < 0)
        {
          new_index = 0;
        }
        epub_index_state.selected_item = new_index;
        return LAST_INTERACTION;
      }
      return NONE;
    }

    if (epub_index_state.num_items <= 0)
    {
      return NONE;
    }

    if (logical_y >= content_height)
    {
      return NONE;
    }

    int cell_height = content_height / EPUB_TOC_ITEMS_PER_PAGE;
    if (cell_height <= 0)
    {
      cell_height = 1;
    }
    int row_in_page = logical_y / cell_height;
    if (row_in_page < 0)
    {
      row_in_page = 0;
    }
    if (row_in_page >= EPUB_TOC_ITEMS_PER_PAGE)
    {
      row_in_page = EPUB_TOC_ITEMS_PER_PAGE - 1;
    }
    int current_page = epub_index_state.selected_item / EPUB_TOC_ITEMS_PER_PAGE;
    int index = current_page * EPUB_TOC_ITEMS_PER_PAGE + row_in_page;
    if (index >= epub_index_state.num_items)
    {
      index = epub_index_state.num_items - 1;
    }
    if (index < 0)
    {
      index = 0;
    }
    epub_index_state.selected_item = index;
    return SELECT;
  }

  // While reading, keep simple left/right tap navigation for
  // page turns, and add a top tap region that opens the in-book
  // menu.
  if (ui_state == READING_EPUB)
  {
    int top_zone = epd_height / 6;
    if (top_zone <= 0)
    {
      top_zone = epd_height / 4;
    }
    if (logical_y < top_zone)
    {
      // A tap at the top of the screen opens the in-book menu.
      return SELECT;
    }

    uint16_t left_zone = touch_width / 3;
    uint16_t right_zone = (touch_width * 2) / 3;

    if (x < left_zone)
    {
      return invert_tap_zones ? DOWN : UP;
    }
    if (x >= right_zone)
    {
      return invert_tap_zones ? UP : DOWN;
    }
    return SELECT;
  }

  if (ui_state == READING_MENU)
  {
    int page_width = renderer->get_page_width();
    int page_height = renderer->get_page_height();
    int line_height = renderer->get_line_height();
    if (line_height <= 0)
    {
      line_height = 20;
    }
    if (page_height <= 0)
    {
      page_height = line_height * 8;
    }
    if (page_width <= 0)
    {
      page_width = 400;
    }

    int bottom_bar_height = EPUB_LIST_BOTTOM_BAR_HEIGHT;
    int bar_top = page_height - bottom_bar_height;
    if (bar_top < 0)
    {
      bar_top = page_height;
    }
    int content_height = page_height;
    if (bottom_bar_height > 0 && bottom_bar_height < page_height)
    {
      content_height = page_height - bottom_bar_height;
    }

    // Mirror the reader menu item counts and paging used in
    // renderReaderMenu(): 6 basic items, 10 advanced items, with a
    // maximum of 6 visible per page (EPUB_TOC_ITEMS_PER_PAGE).
    int items_total = reader_menu_advanced ? 10 : 6;
    if (items_total <= 0)
    {
      return NONE;
    }
    int items_per_page = EPUB_TOC_ITEMS_PER_PAGE;
    if (items_per_page <= 0 || items_per_page > items_total)
    {
      items_per_page = items_total;
    }

    if (reader_menu_selected < 0)
    {
      reader_menu_selected = 0;
    }
    if (reader_menu_selected >= items_total)
    {
      reader_menu_selected = items_total - 1;
    }

    int total_pages = (items_total + items_per_page - 1) / items_per_page;
    if (total_pages < 1)
    {
      total_pages = 1;
    }
    int current_page = reader_menu_selected / items_per_page;
    if (current_page < 0)
    {
      current_page = 0;
    }
    if (current_page >= total_pages)
    {
      current_page = total_pages - 1;
    }

    int start_index = current_page * items_per_page;
    int end_index = start_index + items_per_page;
    if (end_index > items_total)
    {
      end_index = items_total;
    }
    int visible_count = end_index - start_index;
    if (visible_count <= 0)
    {
      return NONE;
    }

    int button_vertical_padding = line_height / 4;
    if (button_vertical_padding < 2)
    {
      button_vertical_padding = 2;
    }
    int button_height = line_height + button_vertical_padding * 2;
    int button_spacing = line_height / 4;
    if (button_spacing < 2)
    {
      button_spacing = 2;
    }

    // Bottom bar navigation: "<<  <  X / Y  >  >>" using the same
    // five equal-width regions as EpubList/EpubToc. Double arrows
    // jump by a page; single arrows move selection by one item.
    if (bottom_bar_height > 0 && logical_y >= bar_top)
    {
      int columns = 5;
      int col_width = page_width / columns;
      if (col_width <= 0)
      {
        col_width = 1;
      }

      int ld_zone_start = 0;
      int ld_zone_end = ld_zone_start + col_width;

      int ls_zone_start = ld_zone_end;
      int ls_zone_end = ls_zone_start + col_width;

      int center_zone_start = ls_zone_end;
      int center_zone_end = center_zone_start + col_width;

      int rs_zone_start = center_zone_end;
      int rs_zone_end = rs_zone_start + col_width;

      int rd_zone_start = rs_zone_end;
      int rd_zone_end = page_width;

      if (logical_x < ld_zone_end)
      {
        // "<<" – in the advanced reader menu, act as a Back
        // control: return to the basic menu. In the basic menu,
        // retain the page-jump semantics even though everything fits
        // on a single page.
        if (reader_menu_advanced)
        {
          reader_menu_advanced = false;
          reader_menu_selected = 0;
          return LAST_INTERACTION;
        }

        int new_index = reader_menu_selected - items_per_page;
        if (new_index < 0)
        {
          new_index = 0;
        }
        reader_menu_selected = new_index;
        return LAST_INTERACTION;
      }
      if (logical_x < ls_zone_end)
      {
        // "<" – move selection to previous item.
        int new_index = reader_menu_selected - 1;
        if (new_index < 0)
        {
          new_index = 0;
        }
        reader_menu_selected = new_index;
        return LAST_INTERACTION;
      }
      if (logical_x < center_zone_end)
      {
        // Taps on the page indicator do not change selection.
        return NONE;
      }
      if (logical_x < rs_zone_end)
      {
        // ">" – move selection to next item.
        int new_index = reader_menu_selected + 1;
        if (new_index >= items_total)
        {
          new_index = items_total - 1;
        }
        if (new_index < 0)
        {
          new_index = 0;
        }
        reader_menu_selected = new_index;
        return LAST_INTERACTION;
      }
      if (logical_x < rd_zone_end)
      {
        // ">>" – jump forward by one page.
        int new_index = reader_menu_selected + items_per_page;
        if (new_index >= items_total)
        {
          new_index = items_total - 1;
        }
        if (new_index < 0)
        {
          new_index = 0;
        }
        reader_menu_selected = new_index;
        return LAST_INTERACTION;
      }
      return NONE;
    }

    // Content area: tap a visible menu button to select it.
    if (logical_y >= content_height)
    {
      return NONE;
    }

    int container_height = visible_count * button_height + (visible_count - 1) * button_spacing;
    int container_y = (content_height - container_height) / 2;
    if (container_y < 0)
    {
      container_y = 0;
    }

    if (logical_y < container_y || logical_y >= container_y + container_height)
    {
      return NONE;
    }

    int offset_y = logical_y - container_y;
    for (int i = 0; i < visible_count; i++)
    {
      int button_top = i * (button_height + button_spacing);
      int button_bottom = button_top + button_height;
      if (offset_y >= button_top && offset_y < button_bottom)
      {
        int item_index = start_index + i;
        if (item_index >= items_total)
        {
          item_index = items_total - 1;
        }
        if (item_index < 0)
        {
          item_index = 0;
        }
        reader_menu_selected = item_index;
        return SELECT;
      }
    }
    return NONE;
  }

  return NONE;
}

UIAction PaperS3TouchControls::mapSwipeUpToAction(uint16_t start_x, uint16_t start_y, uint16_t end_x, uint16_t end_y, uint8_t max_points)
{
  (void)end_x;
  (void)end_y;

  // The GT911 on PaperS3 is configured by M5GFX as 540x960.
  const uint16_t touch_width = 540;
  const uint16_t touch_height = 960;

  if (start_x >= touch_width || start_y >= touch_height)
  {
    return NONE;
  }

  // From the library view (SELECTING_EPUB), treat any two-finger swipe
  // up anywhere on the screen as a request to open the reader menu
  // (advanced settings) without first opening a book.
  if (ui_state == SELECTING_EPUB && max_points >= 2)
  {
    return OPEN_READER_MENU;
  }

  // When reading, interpret a swipe up as a request to open
  // navigation: map it to SELECT, which handleEpub() already
  // interprets as "open the reader menu".
  if (ui_state == READING_EPUB)
  {
    return SELECT;
  }

  return NONE;
}

UIAction PaperS3TouchControls::mapSwipeDownToAction(uint16_t start_x, uint16_t start_y, uint16_t end_x, uint16_t end_y, uint8_t max_points)
{
  (void)end_x;
  (void)end_y;

  // The GT911 on PaperS3 is configured by M5GFX as 540x960.
  const uint16_t touch_width = 540;
  const uint16_t touch_height = 960;

  if (start_x >= touch_width || start_y >= touch_height)
  {
    return NONE;
  }

  // While reading, interpret any two-finger swipe down anywhere on the
  // screen as a request to force a full-page refresh. Single-finger
  // vertical swipes remain unused so they do not interfere with
  // existing page-turn gestures.
  if (ui_state == READING_EPUB && max_points >= 2)
  {
    return REFRESH_PAGE;
  }

  return NONE;
}

void PaperS3TouchControls::emitAction(UIAction action)
{
  if (action == NONE || !on_action)
  {
    return;
  }
  TickType_t now = xTaskGetTickCount();
  uint32_t since_ms = (now - m_last_emit_tick) * portTICK_PERIOD_MS;
  if (since_ms >= EMIT_DEBOUNCE_MS)
  {
    m_last_emit_tick = now;
    on_action(action);
  }
}

void PaperS3TouchControls::loop()
{
  // Threshold in touch-coordinate pixels to distinguish a swipe
  // from a tap. The GT911 reports 540x960.
  const uint16_t swipe_threshold = s_swipe_threshold;
  const uint16_t longpress_move_threshold = s_longpress_move_threshold;
  const uint32_t longpress_ms = s_longpress_ms;

  uint16_t start_x = 0;
  uint16_t start_y = 0;
  uint16_t current_x = 0;
  uint16_t current_y = 0;
  // Track the maximum number of simultaneous touch points seen
  // during the current gesture so we can distinguish two-finger
  // swipes from single-finger swipes.
  uint8_t max_points = 0;

  while (!m_stop_polling)
  {
    uint16_t x = 0;
    uint16_t y = 0;
    uint8_t points = 0;
    bool home_key = false;

    bool have_point = readTouchPoint(&x, &y, &points, &home_key);

    // Capacitive home key (GT911 status bit 0x10): emit GO_HOME on the press
    // edge. Sampled on a slow cadence, clearing the latch each sample so a
    // lingering bit can't double-fire or wedge input. FULL_REFRESH is on the
    // IO48 double-click now (PaperS3.cpp); the readTouchPoint home_key out-param
    // is intentionally unused here.
    {
      TickType_t now = xTaskGetTickCount();
      if ((uint32_t)((now - home_key_sample_tick) * portTICK_PERIOD_MS) >= HOME_SAMPLE_MS)
      {
        home_key_sample_tick = now;
        uint8_t st = 0;
        bool key_set = (gt911_read_reg(i2c_addr, 0x814E, &st, 1) == ESP_OK) && (st & 0x10);
        if (key_set)
        {
          uint8_t zero = 0;
          gt911_write_reg(i2c_addr, 0x814E, &zero, 1); // flush latch
          if (!home_key_down)
          {
            home_key_down = true;
            ESP_LOGI(TAG, "Home key -> GO_HOME");
            emitAction(GO_HOME);
          }
        }
        else
        {
          home_key_down = false;
        }
      }
    }

    if (have_point)
    {
      if (!touch_active)
      {
        // First contact
        touch_active = true;
        touch_start_tick = xTaskGetTickCount();
        start_x = current_x = x;
        start_y = current_y = y;
        max_points = points;
      }
      else
      {
        // Update current position while the finger moves.
        current_x = x;
        current_y = y;
        if (points > max_points)
        {
          max_points = points;
        }
      }
    }
    else
    {
      if (touch_active)
      {
        // Touch has just ended – decide between tap and swipe.
        int dx = static_cast<int>(current_x) - static_cast<int>(start_x);
        int dy = static_cast<int>(start_y) - static_cast<int>(current_y); // positive when moving up
        int abs_dx = dx >= 0 ? dx : -dx;
        int abs_dy = dy >= 0 ? dy : -dy;

        TickType_t end_tick = xTaskGetTickCount();
        uint32_t dt_ms = (end_tick - touch_start_tick) * portTICK_PERIOD_MS;

        UIAction action = NONE;

        // Long-press: minimal movement and held for longpress_ms.
        if (dt_ms >= longpress_ms && abs_dx <= static_cast<int>(longpress_move_threshold) &&
            abs_dy <= static_cast<int>(longpress_move_threshold))
        {
          action = mapLongPressToAction(start_x, start_y);
        }
        // Horizontal swipe: chapter-level navigation while reading.
        else if (abs_dx > abs_dy && abs_dx > static_cast<int>(swipe_threshold))
        {
          if (ui_state == READING_EPUB)
          {
            action = (dx > 0) ? NEXT_SECTION : PREV_SECTION;
          }
        }
        // Vertical swipe up/down.
        else if (dy > static_cast<int>(swipe_threshold))
        {
          action = mapSwipeUpToAction(start_x, start_y, current_x, current_y, max_points);
        }
        else if (dy < -static_cast<int>(swipe_threshold))
        {
          action = mapSwipeDownToAction(start_x, start_y, current_x, current_y, max_points);
        }
        else
        {
          action = mapTapToAction(start_x, start_y);
        }

        touch_active = false;
        max_points = 0;
        last_action = action;
        ESP_LOGD(TAG, "Touch at %u,%u (end %u,%u) -> dy=%d, action %d", start_x, start_y, current_x, current_y, dy, (int)action);
        emitAction(action);
      }
    }

    vTaskDelay(pdMS_TO_TICKS(30));
  }
}

UIAction PaperS3TouchControls::mapLongPressToAction(uint16_t x, uint16_t y)
{
  // The GT911 on PaperS3 is configured by M5GFX as 540x960.
  const uint16_t touch_width = 540;
  const uint16_t touch_height = 960;

  if (x >= touch_width || y >= touch_height)
  {
    return NONE;
  }

  // While reading, long-press bottom-left/right for previous/next
  // section (chapter), KOReader-style.
  if (ui_state == READING_EPUB)
  {
    uint16_t top_zone = touch_height / 3;
    uint16_t bottom_zone = (touch_height * 2) / 3;
    uint16_t left_center = touch_width / 3;
    uint16_t right_center = (touch_width * 2) / 3;

    // Long-press top-center toggles the status bar visibility.
    if (y < top_zone && x >= left_center && x < right_center)
    {
      return TOGGLE_STATUS_BAR;
    }

    if (y >= bottom_zone)
    {
      uint16_t mid_x = touch_width / 2;
      if (x < mid_x)
      {
        return PREV_SECTION;
      }
      else
      {
        return NEXT_SECTION;
      }
    }
    return NONE;
  }

  // In the EPUB list, long-pressing a row behaves like tapping it:
  // select that row and open it.
  if (ui_state == SELECTING_EPUB && epub_list_state.is_loaded && epub_list_state.num_epubs > 0)
  {
    int epd_height = renderer->get_page_height();
    if (epd_height <= 0)
    {
      return NONE;
    }
    int logical_y = static_cast<int>(y) * epd_height / touch_height;
    const int items_per_page = 5;
    int cell_height = epd_height / items_per_page;
    if (cell_height <= 0)
    {
      cell_height = 1;
    }
    int row_in_page = logical_y / cell_height;
    if (row_in_page < 0)
    {
      row_in_page = 0;
    }
    if (row_in_page >= items_per_page)
    {
      row_in_page = items_per_page - 1;
    }

    int current_page = epub_list_state.selected_item / items_per_page;
    int index = current_page * items_per_page + row_in_page;
    if (index >= epub_list_state.num_epubs)
    {
      index = epub_list_state.num_epubs - 1;
    }
    if (index < 0)
    {
      index = 0;
    }
    epub_list_state.selected_item = index;
    return SELECT;
  }

  // In the TOC, long-press a row to open that chapter directly.
  if (ui_state == SELECTING_TABLE_CONTENTS && epub_index_state.num_items > 0)
  {
    int epd_height = renderer->get_page_height();
    if (epd_height <= 0)
    {
      return NONE;
    }
    int logical_y = static_cast<int>(y) * epd_height / touch_height;
    const int items_per_page = 6;
    int cell_height = epd_height / items_per_page;
    if (cell_height <= 0)
    {
      cell_height = 1;
    }
    int row_in_page = logical_y / cell_height;
    if (row_in_page < 0)
    {
      row_in_page = 0;
    }
    if (row_in_page >= items_per_page)
    {
      row_in_page = items_per_page - 1;
    }
    int current_page = epub_index_state.selected_item / items_per_page;
    int index = current_page * items_per_page + row_in_page;
    if (index >= epub_index_state.num_items)
    {
      index = epub_index_state.num_items - 1;
    }
    if (index < 0)
    {
      index = 0;
    }
    epub_index_state.selected_item = index;
    return SELECT;
  }

  // On the home menu there are no row/zone actions, so a long-press is a
  // convenient de-ghost: cycle a full-panel refresh and repaint the menu
  // (same effect as a dedicated home-key long-press).
  if (ui_state == HOME_MENU)
  {
    return FULL_REFRESH;
  }

  return NONE;
}
