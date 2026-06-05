#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <esp_sleep.h>
#include <esp_timer.h>
#include <esp_system.h>
#include <esp_attr.h> // RTC_DATA_ATTR
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <ctype.h>
#include <esp_random.h>
#include "config.h"
#include "EpubList/Epub.h"
#include "EpubList/EpubList.h"
#include "EpubList/EpubReader.h"
#include "EpubList/EpubToc.h"
#include <RubbishHtmlParser/RubbishHtmlParser.h>
#include "boards/Board.h"
#include "boards/battery/Battery.h"
#include "boards/controls/PaperS3TouchControls.h"
#ifdef EPAPEROS_TASKS
#include "tasks/TasksController.h"
#endif
#include "ui/UIState.h"
#include "ui/HomeLayout.h"
#include "settings/SettingsStore.h"
#include "settings/SettingsScreen.h"
#include "rtc/TimeSync.h"
#include "rtc/Pcf8563.h"
#include <nvs_flash.h>
#include <time.h>

#ifdef USE_FREETYPE
#include "Renderer/EpdiyFrameBufferRenderer.h"
#include "Renderer/FreeTypeFont.h"

#if defined(BOARD_TYPE_PAPER_S3)
// Global FreeType font instance used by the Paper S3 renderer.
static FreeTypeFont *g_paper_s3_ft_font = nullptr;

static void init_freetype_for_paper_s3(Renderer *renderer)
{
  if (g_paper_s3_ft_font)
  {
    return;
  }

  auto *epd_renderer = static_cast<EpdiyFrameBufferRenderer *>(renderer);
  if (!epd_renderer)
  {
    return;
  }

  g_paper_s3_ft_font = new FreeTypeFont();
  // Use a fixed pixel height similar to the original bitmap fonts.
  int pixel_height = 22;
  if (!g_paper_s3_ft_font->init("/fs/fonts/reader.ttf", pixel_height))
  {
    delete g_paper_s3_ft_font;
    g_paper_s3_ft_font = nullptr;
    return;
  }

  epd_renderer->set_freetype_font_for_reading(g_paper_s3_ft_font);
  epd_renderer->set_freetype_enabled(true);
}
#endif // BOARD_TYPE_PAPER_S3
#endif // USE_FREETYPE

#ifdef LOG_ENABLED
// Reference: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/log.html
#define LOG_LEVEL ESP_LOG_INFO
#else
#define LOG_LEVEL ESP_LOG_NONE
#endif
#include <esp_log.h>

extern "C"
{
  void app_main();
}

const char *TAG = "main";

// UIState is defined in ui/UIState.h and shared with the touch driver so tap
// hit-testing branches on the same integer values.

// Default UI state. With tasks enabled the device boots to the home menu
// (Books / Tasks / Settings); built as a pure reader (no EPAPEROS_TASKS) it
// boots straight into the library. Per-book reading progress is persisted via
// /fs/Books/BOOKS.IDX; which book was open is kept across sleep in
// g_sleep_book_index (RTC), not on disk.
#ifdef EPAPEROS_TASKS
UIState ui_state = HOME_MENU;
#else
UIState ui_state = SELECTING_EPUB;
#endif
// The screen we were on when we went to deep sleep, retained across the sleep
// in RTC slow memory (reset to -1 on cold boot). On Paper S3 (no nav buttons)
// the wake path uses this to resume the same screen instead of always jumping
// into the last-open book.
RTC_DATA_ATTR static int g_sleep_ui_state = -1;
// The book index (epub_list_state.selected_item) we were reading when we went
// to deep sleep, retained across the sleep in RTC slow memory (-1 on cold boot).
// epub_list_state itself is a plain global that resets on every boot, so without
// this the wake path can't tell which of several in-progress books was open and
// has to guess by progress -- which resumes the wrong book.
RTC_DATA_ATTR static int g_sleep_book_index = -1;
// State data for the EPUB list and reader.
EpubListState epub_list_state = {};
// State data for the EPUB index list.
EpubTocState epub_index_state = {};

// Whether the KOReader-style status bar (title, progress, battery)
// is currently visible. Toggled via a long-press gesture while
// reading.
bool status_bar_visible = true;

// User-configurable behaviour flags.
// When true, selecting a book that has a saved bookmark (any reading
// progress) jumps straight back to that page instead of showing the
// table of contents. Persisted in AppSettings flag bit 0x4.
bool resume_on_select = true;
bool invert_tap_zones = false;
bool justify_paragraphs = false;

#ifdef USE_FREETYPE
// Two independent FreeType sizes sharing one face. The face's pixel height is
// switched to whichever is appropriate for the screen being drawn:
//   - g_reading_font_px: book pages only (reader menu "Font size", AppSettings).
//   - g_ui_font_px: menus, library, tasks, settings (Settings "Font px", NVS).
static int g_reading_font_px = 22;
static int g_ui_font_px = 20;
#endif

const int READER_MENU_BASIC_ITEMS = 6;
const int READER_MENU_ADVANCED_ITEMS = 10;

typedef enum
{
  SLEEP_IMAGE_COVER,
  SLEEP_IMAGE_RANDOM,
  SLEEP_IMAGE_OFF
} SleepImageMode;

SleepImageMode sleep_image_mode = SLEEP_IMAGE_COVER;

typedef enum
{
  IDLE_PROFILE_SHORT = 0,
  IDLE_PROFILE_NORMAL = 1,
  IDLE_PROFILE_LONG = 2
} IdleProfile;

typedef enum
{
  MARGIN_PROFILE_NARROW = 0,
  MARGIN_PROFILE_NORMAL = 1,
  MARGIN_PROFILE_WIDE = 2
} MarginProfile;

typedef enum
{
  GESTURE_SENS_LOW = 0,
  GESTURE_SENS_MEDIUM = 1,
  GESTURE_SENS_HIGH = 2
} GestureSensitivity;

IdleProfile idle_profile = IDLE_PROFILE_NORMAL;
MarginProfile margin_profile = MARGIN_PROFILE_NORMAL;
GestureSensitivity gesture_sensitivity = GESTURE_SENS_MEDIUM;

int64_t idle_timeout_reading_us = 60 * 1000 * 1000;
int64_t idle_timeout_library_us = 60 * 1000 * 1000;

typedef struct
{
  uint8_t version;
  uint8_t flags;
  uint8_t sleep_mode;
  uint8_t reserved;
#ifdef USE_FREETYPE
  int16_t reading_font_px;
  int16_t padding;
#endif
} AppSettings;

static const char *app_settings_path = "/fs/settings.bin";

static void load_app_settings(Renderer *renderer);
static void save_app_settings(Renderer *renderer);
static void apply_idle_profile();
static void apply_page_margins(Renderer *renderer);
static void apply_gesture_profile();
static void show_library_loading(Renderer *renderer);

void handleEpub(Renderer *renderer, UIAction action);
void handleEpubList(Renderer *renderer, UIAction action, bool needs_redraw);
void handleReaderMenu(Renderer *renderer, UIAction action);
#ifdef EPAPEROS_TASKS
void handleHome(Renderer *renderer, UIAction action, bool needs_redraw);
void handleTasks(Renderer *renderer, UIAction action);
#endif
void handleSettings(Renderer *renderer, UIAction action);
static void renderReaderMenu(Renderer *renderer);
static void show_status_bar_toast(Renderer *renderer, const char *text);
void draw_status_bar(Renderer *renderer, float voltage, float percentage);

// Bezel dead-zone kept clear on every screen (no draw elements here). Painted
// glass edges are NOT symmetric: top 15, bottom 10, left 15, right 12. The
// status bar sits inside the top dead-zone; content margins start below it.
// Constant across home / tasks / reader. MUST match clear_deadzone() in
// EpdiyFrameBufferRenderer (the hardware enforcement) and HomeLayout.h.
static const int DEADZONE_TOP = 15;
static const int DEADZONE_BOTTOM = 10;
static const int DEADZONE_LEFT = 15;
static const int DEADZONE_RIGHT = 12;

// Status-bar strip height: top dead-zone + one line of UI text + separator gap.
// Scales with the UI/reader font so the bar grows with the font size.
static int status_bar_height(Renderer *renderer)
{
  int lh = renderer->get_line_height();
  if (lh <= 0)
  {
    lh = 20;
  }
  return DEADZONE_TOP + lh + 8;
}

// Status-bar clock/date strings. Phase 1: placeholders; the RTC fills these.
char g_status_time[16] = "--:--";
char g_status_date[16] = "-- --- ----";

static EpubList *epub_list = nullptr;
static EpubReader *reader = nullptr;
static EpubToc *contents = nullptr;
#ifdef EPAPEROS_TASKS
static TasksController *tasks_controller = nullptr;
#endif
// Defined unconditionally: the touch driver externs it to hit-test the home
// menu. Unused in a pure-reader build (the home menu is never shown).
int home_selected = 0; // 0 = Tasks, 1 = Books, 2 = Settings (touch hit-tests it)

// Persisted config + the Settings UI. g_tap_x/y carry the logical coordinates of
// the latest raw tap for coordinate-driven screens (set by the touch driver,
// consumed on a TAP action).
volatile int g_tap_x = 0;
volatile int g_tap_y = 0;
static settings::SettingsStore g_settings;
static settings::SettingsScreen *settings_screen = nullptr;
static Battery *g_battery = nullptr;
int reader_menu_selected = 0;
bool reader_menu_advanced = false;
static bool g_request_sleep_now = false;

// A book has a "bookmark" once it has any recorded reading progress: a
// non-zero section/page, or a section that has been laid out at least once
// (pages_in_current_section > 0 means the user opened it and we paginated).
static bool book_has_bookmark(const EpubListItem &item)
{
  return item.current_section != 0 || item.current_page != 0 ||
         item.pages_in_current_section > 0;
}

// Flush the in-RAM reading progress to the on-disk index so a hard reboot
// (not just deep sleep) keeps each book's bookmark. Cheap; called when
// leaving the reader.
static void persist_reading_progress()
{
  if (epub_list)
  {
    epub_list->save_index("/fs/Books/BOOKS.IDX");
  }
}

// Switch the shared FreeType face to the book / UI size before drawing the
// corresponding screen. No-ops without FreeType (bitmap fonts are fixed size).
static void apply_reading_font(Renderer *renderer)
{
#ifdef USE_FREETYPE
  renderer->set_reading_font_pixel_height(g_reading_font_px);
#else
  (void)renderer;
#endif
}

static void apply_ui_font(Renderer *renderer)
{
#ifdef USE_FREETYPE
  renderer->set_reading_font_pixel_height(g_ui_font_px);
#else
  (void)renderer;
#endif
}

static int find_last_open_book_index()
{
  int last_index = -1;
  for (int i = 0; i < epub_list_state.num_epubs; i++)
  {
    EpubListItem &item = epub_list_state.epub_list[i];
    if (item.current_section != 0 || item.current_page != 0)
    {
      if (last_index < 0)
      {
        last_index = i;
      }
      else
      {
        EpubListItem &best = epub_list_state.epub_list[last_index];
        if (item.current_section > best.current_section ||
            (item.current_section == best.current_section && item.current_page > best.current_page))
        {
          last_index = i;
        }
      }
    }
  }
  if (last_index >= 0)
  {
    return last_index;
  }

  // Fallback: no book has recorded section/page progress yet. This can
  // happen if the user opened a brand-new book and went to sleep on the
  // very first page (section 0, page 0). In that case, treat any book
  // that has been laid out at least once (pages_in_current_section > 0)
  // as a candidate and pick the first such entry.
  for (int i = 0; i < epub_list_state.num_epubs; i++)
  {
    EpubListItem &item = epub_list_state.epub_list[i];
    if (item.pages_in_current_section > 0)
    {
      return i;
    }
  }

  return -1;
}

void handleEpub(Renderer *renderer, UIAction action)
{
  // Book pages render at the reading size, independent of the UI size.
  apply_reading_font(renderer);
  // Re-assert content margins (status bar + bezel dead-zone). The home screen
  // zeroes margins, so without this a book opened from home paginates full-bleed
  // and its last line is clipped by the bottom dead-zone.
  apply_page_margins(renderer);
  if (!reader)
  {
    reader = new EpubReader(epub_list_state.epub_list[epub_list_state.selected_item], renderer);
    reader->set_justified(justify_paragraphs);
    reader->load();
  }
  switch (action)
  {
  case UP:
    reader->prev();
    break;
  case DOWN:
    reader->next();
    break;
  case PREV_SECTION:
    reader->prev_section();
    break;
  case NEXT_SECTION:
    reader->next_section();
    break;
  case REFRESH_PAGE:
    // Force a full-screen refresh of the current reading page to
    // mitigate ghosting. This mirrors the "[R] Refresh screen"
    // reader-menu action but is triggered via a gesture.
    renderer->reset();
    break;
  case SELECT:
    // switch back to main screen
    ui_state = SELECTING_EPUB;
    // Save the bookmark (current section/page) to disk so it survives a
    // hard reboot, not just a deep-sleep cycle.
    persist_reading_progress();
    renderer->clear_screen();
    // clear the epub reader away
    delete reader;
    reader = nullptr;
    // force a redraw
    if (!epub_list)
    {
      epub_list = new EpubList(renderer, epub_list_state);
    }
    handleEpubList(renderer, NONE, true);
    return;
  case NONE:
  default:
    break;
  }
  reader->render();
}

void handleEpubTableContents(Renderer *renderer, UIAction action, bool needs_redraw)
{
  apply_ui_font(renderer);
  apply_page_margins(renderer); // status bar + bezel dead-zone (home zeroes them)
  if (!contents)
  {
    contents = new EpubToc(epub_list_state.epub_list[epub_list_state.selected_item], epub_index_state, renderer);
    contents->set_needs_redraw();
    contents->load();
  }
  switch (action)
  {
  case UP:
    contents->prev();
    break;
  case DOWN:
    contents->next();
    break;
  case SELECT:
    // setup the reader state
    ui_state = READING_EPUB;
    // Replace any existing reader so we don't leak or reuse stale
    // parser state when jumping via the TOC.
    if (reader)
    {
      delete reader;
      reader = nullptr;
    }
    reader = new EpubReader(epub_list_state.epub_list[epub_list_state.selected_item], renderer);
    reader->set_justified(justify_paragraphs);
    reader->set_state_section(contents->get_selected_toc());
    if (!reader->load())
    {
      ESP_LOGE(TAG, "Failed to load EPUB when opening from TOC selection");
      // Stay in the TOC view; the user can back out to the library or
      // try another entry.
      delete reader;
      reader = nullptr;
      ui_state = SELECTING_TABLE_CONTENTS;
      return;
    }
    // switch to reading the epub
    delete contents;
    contents = nullptr;
    handleEpub(renderer, NONE);
    return;
  case NONE:
  default:
    break;
  }
  contents->render();
}

static void renderReaderMenu(Renderer *renderer)
{
  apply_ui_font(renderer);
  apply_page_margins(renderer); // status bar + bezel dead-zone (home zeroes them)
  const int max_items = 13;
  const char *labels[max_items];
  int items_total = 0;

  char buf_status[32];
  char buf_view[32];
  char buf_startup[40];
  char buf_sleep[40];
  char buf_font[32];
  char buf_align[32];
  char buf_tap[32];
  char buf_idle[32];
  char buf_margin[32];
  char buf_gest[32];

  if (!reader_menu_advanced)
  {
    items_total = READER_MENU_BASIC_ITEMS;
    labels[0] = "Return to book";
    labels[1] = "Table of contents";
    labels[2] = "Back to library";
    labels[3] = "More";
    // Use ASCII-friendly "icon" prefixes so they render on limited fonts.
    labels[4] = "[R] Refresh screen";
    labels[5] = "[Zz] Sleep";
  }
  else
  {
    items_total = READER_MENU_ADVANCED_ITEMS;

    snprintf(buf_status, sizeof(buf_status), "Status bar: %s", status_bar_visible ? "ON" : "OFF");
    labels[0] = buf_status;

    snprintf(buf_view, sizeof(buf_view), "Library view: %s", epub_list_state.use_grid_view ? "Grid" : "List");
    labels[1] = buf_view;

    snprintf(buf_startup, sizeof(buf_startup), "Resume: %s", resume_on_select ? "On" : "Off");
    labels[2] = buf_startup;

    const char *sleep_mode_str = "Cover";
    if (sleep_image_mode == SLEEP_IMAGE_RANDOM)
    {
      sleep_mode_str = "Random";
    }
    else if (sleep_image_mode == SLEEP_IMAGE_OFF)
    {
      sleep_mode_str = "Off";
    }
    snprintf(buf_sleep, sizeof(buf_sleep), "Sleep image: %s", sleep_mode_str);
    labels[3] = buf_sleep;

#ifdef USE_FREETYPE
    int px = g_reading_font_px;
    const char *font_label = "Medium";
    if (px <= 18)
    {
      font_label = "Small";
    }
    else if (px >= 26)
    {
      font_label = "Large";
    }
    snprintf(buf_font, sizeof(buf_font), "Font size: %s", font_label);
#else
    snprintf(buf_font, sizeof(buf_font), "Font size");
#endif
    labels[4] = buf_font;

    snprintf(buf_align, sizeof(buf_align), "Alignment: %s", justify_paragraphs ? "Justified" : "Left");
    labels[5] = buf_align;

    snprintf(buf_tap, sizeof(buf_tap), "Tap zones: %s", invert_tap_zones ? "Inverted" : "Normal");
    labels[6] = buf_tap;

    const char *idle_str = "Normal";
    if (idle_profile == IDLE_PROFILE_SHORT)
    {
      idle_str = "Short";
    }
    else if (idle_profile == IDLE_PROFILE_LONG)
    {
      idle_str = "Long";
    }
    snprintf(buf_idle, sizeof(buf_idle), "Idle: %s", idle_str);
    labels[7] = buf_idle;

    const char *margin_str = "Normal";
    if (margin_profile == MARGIN_PROFILE_NARROW)
    {
      margin_str = "Narrow";
    }
    else if (margin_profile == MARGIN_PROFILE_WIDE)
    {
      margin_str = "Wide";
    }
    snprintf(buf_margin, sizeof(buf_margin), "Margins: %s", margin_str);
    labels[8] = buf_margin;

    const char *gest_str = "Medium";
    if (gesture_sensitivity == GESTURE_SENS_LOW)
    {
      gest_str = "Low";
    }
    else if (gesture_sensitivity == GESTURE_SENS_HIGH)
    {
      gest_str = "High";
    }
    snprintf(buf_gest, sizeof(buf_gest), "Gestures: %s", gest_str);
    labels[9] = buf_gest;
  }

#ifdef USE_FREETYPE
  renderer->set_freetype_enabled(false);
#endif

  renderer->clear_screen();
  int page_width = renderer->get_page_width();
  int page_height = renderer->get_page_height();
  int line_height = renderer->get_line_height();
  if (line_height <= 0)
  {
    line_height = 20;
  }
  if (page_height <= 0)
  {
    page_height = line_height * items_total * 2;
  }

  if (page_width <= 0)
  {
    page_width = 400;
  }

  if (items_total <= 0)
  {
#ifdef USE_FREETYPE
    renderer->set_freetype_enabled(true);
#endif
    return;
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

  int bottom_bar_height = EPUB_LIST_BOTTOM_BAR_HEIGHT;
  int content_height = page_height;
  if (bottom_bar_height > 0 && bottom_bar_height < page_height)
  {
    content_height = page_height - bottom_bar_height;
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

  int max_label_width = 0;
  for (int i = 0; i < items_total; i++)
  {
    int w = renderer->get_text_width(labels[i], false, false);
    if (w > max_label_width)
    {
      max_label_width = w;
    }
  }
  int horizontal_padding = 30;
  int button_width = max_label_width + horizontal_padding * 2;
  if (button_width > page_width - 40)
  {
    button_width = page_width - 40;
  }

  int container_width = button_width;
  int container_height = visible_count * button_height + (visible_count - 1) * button_spacing;
  int container_x = (page_width - container_width) / 2;
  if (container_x < 0)
  {
    container_x = 0;
  }
  int container_y = (content_height - container_height) / 2;
  if (container_y < 0)
  {
    container_y = 0;
  }

  int ypos = container_y;
  for (int i = 0; i < visible_count; i++)
  {
    int item_index = start_index + i;
    const char *label = labels[item_index];

    renderer->fill_rect(container_x, ypos, container_width, button_height, 255);
    renderer->draw_rect(container_x, ypos, container_width, button_height, 0);

    if (item_index == reader_menu_selected)
    {
      for (int line = 0; line < 3; line++)
      {
        renderer->draw_rect(
            container_x + line,
            ypos + line,
            container_width - 2 * line,
            button_height - 2 * line,
            0);
      }
    }

    int label_width = renderer->get_text_width(label, false, false);
    if (label_width < 0)
    {
      label_width = 0;
    }
    int text_x = container_x + (container_width - label_width) / 2;
    int center_y = ypos + (button_height / 2);
    int text_y = center_y - (3 * line_height) / 4;
    renderer->draw_text(text_x, text_y, label, false, false);

    ypos += button_height + button_spacing;
  }

  if (bottom_bar_height > 0 && bottom_bar_height <= page_height)
  {
    int bar_y = page_height - bottom_bar_height;
    renderer->fill_rect(0, bar_y, page_width, bottom_bar_height, 255);
    int center_x = page_width / 2;
    int center_y = bar_y + bottom_bar_height / 2;

    int page_display = current_page + 1;
    if (page_display < 1)
    {
      page_display = 1;
    }
    if (page_display > total_pages)
    {
      page_display = total_pages;
    }

    const char *left_double = "<<";
    const char *left_single = "<";
    const char *right_single = ">";
    const char *right_double = ">>";

    char center[32];
    snprintf(center, sizeof(center), "%d / %d", page_display, total_pages);

    int w_ld = renderer->get_text_width(left_double, true, false);
    int w_ls = renderer->get_text_width(left_single, true, false);
    int w_center = renderer->get_text_width(center, false, false);
    int w_rs = renderer->get_text_width(right_single, true, false);
    int w_rd = renderer->get_text_width(right_double, true, false);
    if (w_ld < 0) w_ld = 0;
    if (w_ls < 0) w_ls = 0;
    if (w_center < 0) w_center = 0;
    if (w_rs < 0) w_rs = 0;
    if (w_rd < 0) w_rd = 0;

    int line_h = renderer->get_line_height();
    if (line_h <= 0)
    {
      line_h = 20;
    }
    int label_y = center_y - (3 * line_h) / 4;

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

    int bar_top = bar_y;
    int bar_bottom = bar_y + bottom_bar_height;
    int bar_height = bar_bottom - bar_top;
    if (bar_height < renderer->get_line_height() + 4)
    {
      bar_height = renderer->get_line_height() + 4;
    }

    int box_y = bar_top + 2;
    int box_h = bar_height - 4;
    if (box_h <= 0)
    {
      box_h = bar_height;
    }

    renderer->draw_rect(ld_zone_start, box_y, ld_zone_end - ld_zone_start, box_h, 0);
    renderer->draw_rect(ls_zone_start, box_y, ls_zone_end - ls_zone_start, box_h, 0);
    renderer->draw_rect(center_zone_start, box_y, center_zone_end - center_zone_start, box_h, 0);
    renderer->draw_rect(rs_zone_start, box_y, rs_zone_end - rs_zone_start, box_h, 0);
    renderer->draw_rect(rd_zone_start, box_y, rd_zone_end - rd_zone_start, box_h, 0);

    auto center_label_x = [](int zone_start, int zone_end, int text_width) {
      int w = zone_end - zone_start;
      int x = zone_start + (w - text_width) / 2;
      if (x < zone_start)
      {
        x = zone_start;
      }
      return x;
    };

    int x_ld = center_label_x(ld_zone_start, ld_zone_end, w_ld);
    int x_ls = center_label_x(ls_zone_start, ls_zone_end, w_ls);
    int x_center = center_label_x(center_zone_start, center_zone_end, w_center);
    int x_rs = center_label_x(rs_zone_start, rs_zone_end, w_rs);
    int x_rd = center_label_x(rd_zone_start, rd_zone_end, w_rd);

    renderer->draw_text(x_ld, label_y, left_double, true, false);
    renderer->draw_text(x_ls, label_y, left_single, true, false);
    renderer->draw_text(x_center, label_y, center, false, false);
    renderer->draw_text(x_rs, label_y, right_single, true, false);
    renderer->draw_text(x_rd, label_y, right_double, true, false);
  }

#ifdef USE_FREETYPE
  renderer->set_freetype_enabled(true);
#endif
}

static void show_status_bar_toast(Renderer *renderer, const char *text)
{
  if (!text)
  {
    return;
  }

  int page_width = renderer->get_page_width();
  int page_height = renderer->get_page_height();
  int line_height = renderer->get_line_height();
  if (page_width <= 0 || page_height <= 0 || line_height <= 0)
  {
    return;
  }

  int padding = 4;
  int box_height = line_height + padding * 2;
  int y = page_height - box_height - 2;
  if (y < 0)
  {
    y = 0;
  }

  // Clear a small strip at the bottom and draw the toast text.
  renderer->fill_rect(0, y, page_width, box_height, 255);
  renderer->draw_rect(0, y, page_width, box_height, 0);
  renderer->draw_text(5, y + padding + line_height / 2, text, false, false);
}

static void load_app_settings(Renderer *renderer)
{
  FILE *fp = fopen(app_settings_path, "rb");
  if (!fp)
  {
    return;
  }
  AppSettings s = {};
  if (fread(&s, sizeof(s), 1, fp) != 1)
  {
    fclose(fp);
    return;
  }
  fclose(fp);
  if (s.version != 1)
  {
    return;
  }
  status_bar_visible = (s.flags & 0x1) != 0;
  epub_list_state.use_grid_view = (s.flags & 0x2) != 0;
  resume_on_select = (s.flags & 0x4) != 0;
  invert_tap_zones = (s.flags & 0x8) != 0;
  uint8_t margin_bits = (s.flags >> 4) & 0x3;
  if (margin_bits <= MARGIN_PROFILE_WIDE)
  {
    margin_profile = (MarginProfile)margin_bits;
  }
  uint8_t idle_bits = (s.flags >> 6) & 0x3;
  if (idle_bits <= IDLE_PROFILE_LONG)
  {
    idle_profile = (IdleProfile)idle_bits;
  }
  if (s.sleep_mode <= SLEEP_IMAGE_OFF)
  {
    sleep_image_mode = (SleepImageMode)s.sleep_mode;
  }
#ifdef USE_FREETYPE
  // Book reading size only; the live face is set per-screen, not here.
  if (s.reading_font_px > 0)
  {
    g_reading_font_px = s.reading_font_px;
  }
#endif
  gesture_sensitivity = (GestureSensitivity)(s.reserved & 0x3);
  // Bit 2 of reserved stores the paragraph alignment preference.
  justify_paragraphs = (s.reserved & 0x4) != 0;
  apply_idle_profile();
  apply_page_margins(renderer);
  apply_gesture_profile();
}

static void save_app_settings(Renderer *renderer)
{
  AppSettings s = {};
  s.version = 1;
  if (status_bar_visible)
  {
    s.flags |= 0x1;
  }
  if (epub_list_state.use_grid_view)
  {
    s.flags |= 0x2;
  }
  if (resume_on_select)
  {
    s.flags |= 0x4;
  }
   if (invert_tap_zones)
  {
    s.flags |= 0x8;
  }
  s.flags |= (((uint8_t)margin_profile) & 0x3) << 4;
  s.flags |= (((uint8_t)idle_profile) & 0x3) << 6;
  s.sleep_mode = (uint8_t)sleep_image_mode;
#ifdef USE_FREETYPE
  // Persist the book size, not whatever the face is currently set to (which
  // may be the UI size if a menu is on screen).
  (void)renderer;
  if (g_reading_font_px > 0)
  {
    s.reading_font_px = (int16_t)g_reading_font_px;
  }
#endif
  s.reserved = (uint8_t)gesture_sensitivity;
  if (justify_paragraphs)
  {
    s.reserved |= 0x4;
  }
  FILE *fp = fopen(app_settings_path, "wb");
  if (!fp)
  {
    return;
  }
  fwrite(&s, sizeof(s), 1, fp);
  fclose(fp);
}

static void apply_idle_profile()
{
  switch (idle_profile)
  {
  case IDLE_PROFILE_SHORT:
    idle_timeout_reading_us = 10 * 60 * 1000 * 1000;
    idle_timeout_library_us = 2 * 60 * 1000 * 1000;
    break;
  case IDLE_PROFILE_LONG:
    idle_timeout_reading_us = 40LL * 60 * 1000 * 1000;
    idle_timeout_library_us = 10LL * 60 * 1000 * 1000;
    break;
  case IDLE_PROFILE_NORMAL:
  default:
    idle_timeout_reading_us = 20 * 60 * 1000 * 1000;
    idle_timeout_library_us = 5 * 60 * 1000 * 1000;
    break;
  }
}

static void apply_page_margins(Renderer *renderer)
{
  int left = 10;
  int right = 10;
  switch (margin_profile)
  {
  case MARGIN_PROFILE_NARROW:
    left = 5;
    right = 5;
    break;
  case MARGIN_PROFILE_WIDE:
    left = 20;
    right = 20;
    break;
  case MARGIN_PROFILE_NORMAL:
  default:
    left = 10;
    right = 10;
    break;
  }
  if (left < DEADZONE_LEFT)
  {
    left = DEADZONE_LEFT;
  }
  if (right < DEADZONE_RIGHT)
  {
    right = DEADZONE_RIGHT;
  }
  renderer->set_margin_top(status_bar_visible ? status_bar_height(renderer) : DEADZONE_TOP);
  renderer->set_margin_left(left);
  renderer->set_margin_right(right);
  renderer->set_margin_bottom(DEADZONE_BOTTOM);
}

static void apply_gesture_profile()
{
  int profile = 1;
  switch (gesture_sensitivity)
  {
  case GESTURE_SENS_LOW:
    profile = 0;
    break;
  case GESTURE_SENS_HIGH:
    profile = 2;
    break;
  case GESTURE_SENS_MEDIUM:
  default:
    profile = 1;
    break;
  }
  PaperS3TouchControls::set_gesture_profile(profile);
}

void handleReaderMenu(Renderer *renderer, UIAction action)
{
  int item_total = reader_menu_advanced ? READER_MENU_ADVANCED_ITEMS : READER_MENU_BASIC_ITEMS;

  switch (action)
  {
  // Buttons felt inverted here vs. the rest of the UI, so the directions are
  // swapped: UP advances the highlight, DOWN moves it back.
  case UP:
    if (item_total > 0)
    {
      reader_menu_selected = (reader_menu_selected + 1) % item_total;
    }
    renderReaderMenu(renderer);
    break;
  case DOWN:
    if (item_total > 0)
    {
      reader_menu_selected = (reader_menu_selected - 1 + item_total) % item_total;
    }
    renderReaderMenu(renderer);
    break;
  case SELECT:
    if (!reader_menu_advanced)
    {
      if (reader_menu_selected == 0)
      {
        ui_state = READING_EPUB;
        renderer->clear_screen();
        if (reader)
        {
          apply_reading_font(renderer);
          reader->render();
        }
        else
        {
          // Reader was dropped (e.g. after a font-size change); rebuild and
          // re-paginate at the current reading size.
          handleEpub(renderer, NONE);
        }
      }
      else if (reader_menu_selected == 1)
      {
        ui_state = SELECTING_TABLE_CONTENTS;
        if (contents)
        {
          delete contents;
          contents = nullptr;
        }
        contents = new EpubToc(epub_list_state.epub_list[epub_list_state.selected_item], epub_index_state, renderer);
        if (!contents->load())
        {
          delete contents;
          contents = nullptr;
          ui_state = READING_EPUB;
          renderer->clear_screen();
          if (reader)
          {
            reader->render();
          }
          break;
        }
        contents->set_needs_redraw();
        handleEpubTableContents(renderer, NONE, true);
      }
      else if (reader_menu_selected == 2)
      {
        // Back to library: force a full-screen refresh and show the
        // same "Book library is loading" splash used on cold boot
        // while the EPUB list is (re)rendered.
        ui_state = SELECTING_EPUB;
        // Persist the bookmark before tearing down the reader.
        persist_reading_progress();
        renderer->reset();
        show_library_loading(renderer);
        if (reader)
        {
          delete reader;
          reader = nullptr;
        }
        handleEpubList(renderer, NONE, true);
      }
      else if (reader_menu_selected == 3)
      {
        reader_menu_advanced = true;
        reader_menu_selected = 0;
        renderReaderMenu(renderer);
      }
      else if (reader_menu_selected == 4)
      {
        // Full screen refresh of the current reading page to
        // mitigate ghosting.
        ui_state = READING_EPUB;
        renderer->reset();
        if (reader)
        {
          reader->render();
        }
      }
      else if (reader_menu_selected == 5)
      {
        // Request immediate sleep; main_task's event loop will
        // see this flag and break out to the sleep sequence.
        g_request_sleep_now = true;
      }
    }
    else
    {
      // Advanced menu entries
      if (reader_menu_selected == 0)
      {
        status_bar_visible = !status_bar_visible;
        save_app_settings(renderer);
        show_status_bar_toast(renderer, status_bar_visible ? "Status bar ON" : "Status bar OFF");
        renderReaderMenu(renderer);
      }
      else if (reader_menu_selected == 1)
      {
        epub_list_state.use_grid_view = !epub_list_state.use_grid_view;
        if (epub_list)
        {
          epub_list->set_needs_redraw();
        }
        save_app_settings(renderer);
        show_status_bar_toast(renderer, epub_list_state.use_grid_view ? "Library view: Grid" : "Library view: List");
        renderReaderMenu(renderer);
      }
      else if (reader_menu_selected == 2)
      {
        resume_on_select = !resume_on_select;
        save_app_settings(renderer);
        show_status_bar_toast(renderer, resume_on_select ? "Resume: On" : "Resume: Off");
        renderReaderMenu(renderer);
      }
      else if (reader_menu_selected == 3)
      {
        switch (sleep_image_mode)
        {
        case SLEEP_IMAGE_COVER:
          sleep_image_mode = SLEEP_IMAGE_RANDOM;
          show_status_bar_toast(renderer, "Sleep image: Random");
          break;
        case SLEEP_IMAGE_RANDOM:
          sleep_image_mode = SLEEP_IMAGE_OFF;
          show_status_bar_toast(renderer, "Sleep image: Off");
          break;
        case SLEEP_IMAGE_OFF:
        default:
          sleep_image_mode = SLEEP_IMAGE_COVER;
          show_status_bar_toast(renderer, "Sleep image: Cover");
          break;
        }
        save_app_settings(renderer);
        renderReaderMenu(renderer);
      }
      else if (reader_menu_selected == 4)
      {
#ifdef USE_FREETYPE
        int sizes[] = {18, 22, 26};
        int current_px = g_reading_font_px;
        int index = 0;
        for (int i = 0; i < 3; i++)
        {
          if (sizes[i] == current_px)
          {
            index = i;
          }
        }
        index = (index + 1) % 3;
        g_reading_font_px = sizes[index];
        // Drop the reader so it re-paginates at the new size when the user
        // returns to the book (the cached parser was laid out at the old size).
        if (reader)
        {
          delete reader;
          reader = nullptr;
        }
        save_app_settings(renderer);
        show_status_bar_toast(renderer, "Font size changed");
#else
        (void)renderer;
#endif
        renderReaderMenu(renderer);
      }
      else if (reader_menu_selected == 5)
      {
        // Toggle paragraph alignment between left-aligned and
        // fully-justified. The actual layout is handled by
        // RubbishHtmlParser via EpubReader::set_justified().
        justify_paragraphs = !justify_paragraphs;
        save_app_settings(renderer);
        if (reader)
        {
          reader->set_justified(justify_paragraphs);
        }
        show_status_bar_toast(renderer, justify_paragraphs ? "Alignment: Justified" : "Alignment: Left");
        renderReaderMenu(renderer);
      }
      else if (reader_menu_selected == 6)
      {
        invert_tap_zones = !invert_tap_zones;
        save_app_settings(renderer);
        show_status_bar_toast(renderer, invert_tap_zones ? "Tap zones: inverted" : "Tap zones: normal");
        renderReaderMenu(renderer);
      }
      else if (reader_menu_selected == 7)
      {
        idle_profile = (IdleProfile)(((int)idle_profile + 1) % 3);
        apply_idle_profile();
        save_app_settings(renderer);
        const char *label = "Idle: Normal";
        if (idle_profile == IDLE_PROFILE_SHORT)
        {
          label = "Idle: Short";
        }
        else if (idle_profile == IDLE_PROFILE_LONG)
        {
          label = "Idle: Long";
        }
        show_status_bar_toast(renderer, label);
        renderReaderMenu(renderer);
      }
      else if (reader_menu_selected == 8)
      {
        margin_profile = (MarginProfile)(((int)margin_profile + 1) % 3);
        apply_page_margins(renderer);
        save_app_settings(renderer);
        const char *label = "Margins: Normal";
        if (margin_profile == MARGIN_PROFILE_NARROW)
        {
          label = "Margins: Narrow";
        }
        else if (margin_profile == MARGIN_PROFILE_WIDE)
        {
          label = "Margins: Wide";
        }
        show_status_bar_toast(renderer, label);
        renderReaderMenu(renderer);
      }
      else if (reader_menu_selected == 9)
      {
        gesture_sensitivity = (GestureSensitivity)(((int)gesture_sensitivity + 1) % 3);
        apply_gesture_profile();
        save_app_settings(renderer);
        const char *label = "Gestures: Medium";
        if (gesture_sensitivity == GESTURE_SENS_LOW)
        {
          label = "Gestures: Low";
        }
        else if (gesture_sensitivity == GESTURE_SENS_HIGH)
        {
          label = "Gestures: High";
        }
        show_status_bar_toast(renderer, label);
        renderReaderMenu(renderer);
      }
    }
    break;
  case NONE:
  default:
    renderReaderMenu(renderer);
    break;
  }
}

void handleEpubList(Renderer *renderer, UIAction action, bool needs_redraw)
{
  apply_ui_font(renderer);
  apply_page_margins(renderer); // status bar + bezel dead-zone (home zeroes them)
  // load up the epub list from the filesystem
  if (!epub_list)
  {
    ESP_LOGI("main", "Creating epub list");
    epub_list = new EpubList(renderer, epub_list_state);
    // Paper S3 stores all EPUBs on the SD card under /fs/Books.
    if (epub_list->load("/fs/Books"))
    {
      ESP_LOGI("main", "Epub files loaded");
    }
  }
  if (needs_redraw)
  {
    epub_list->set_needs_redraw();
  }
  // work out what the user wants us to do
  // Buttons felt inverted here vs. the physical layout, so the directions are
  // swapped to match the rest of the UI: UP advances down the list.
  switch (action)
  {
  case UP:
    epub_list->next();
    break;
  case DOWN:
    epub_list->prev();
    break;
  case SELECT:
    // Bookmark resume: if enabled and this book already has reading
    // progress, jump straight back to the saved page instead of forcing
    // the user back through the table of contents (which would reset the
    // page to the top of a chapter). The TOC is still reachable from the
    // reader menu. Fresh books (no bookmark) fall through to the TOC.
    if (resume_on_select &&
        book_has_bookmark(epub_list_state.epub_list[epub_list_state.selected_item]))
    {
      ui_state = READING_EPUB;
      if (reader)
      {
        delete reader;
        reader = nullptr;
      }
      handleEpub(renderer, NONE);
      return;
    }
    // Try to show the table of contents if the book has one; otherwise
    // fall back to opening the book directly.
    ui_state = SELECTING_TABLE_CONTENTS;
    contents = new EpubToc(epub_list_state.epub_list[epub_list_state.selected_item], epub_index_state, renderer);
    if (!contents->load())
    {
      delete contents;
      contents = nullptr;
      ui_state = READING_EPUB;
      handleEpub(renderer, NONE);
      return;
    }
    contents->set_needs_redraw();
    handleEpubTableContents(renderer, NONE, true);
    return;
  case NONE:
  default:
    // nothing to do
    break;
  }
  epub_list->render();
}

#ifdef EPAPEROS_TASKS
// Top-level menu: choose between the EPUB reader and the Obsidian task app.
void handleHome(Renderer *renderer, UIAction action, bool needs_redraw)
{
  (void)needs_redraw;
  apply_ui_font(renderer);
  const int item_count = 3; // 0 = Tasks, 1 = Books, 2 = Settings

  switch (action)
  {
  // Direction intentionally swapped vs. the physical layout (Boot=DOWN, IO48=UP)
  // to match the rest of the UI: UP advances the highlight down the list
  // (Tasks -> Books -> Settings), DOWN moves it back.
  case UP:
    home_selected = (home_selected + 1) % item_count;
    break;
  case DOWN:
    home_selected = (home_selected - 1 + item_count) % item_count;
    break;
  case SELECT:
    if (home_selected == 0)
    {
      ui_state = TASKS;
      handleTasks(renderer, NONE);
    }
    else if (home_selected == 1)
    {
      ui_state = SELECTING_EPUB;
      // Rendering the library decodes a page of cover JPEGs (up to 9 in grid
      // view) straight from the zips -- seconds on a big library. Without a
      // splash the panel looks frozen. Show the same "loading" face the
      // reader-menu "Back to library" path uses before the heavy render.
      show_library_loading(renderer);
      handleEpubList(renderer, NONE, true);
    }
    else
    {
      ui_state = SETTINGS;
      handleSettings(renderer, NONE);
    }
    return;
  case NONE:
  default:
    break;
  }

  // Draw in absolute panel pixels: zero all margins so the touch hit-test
  // (PaperS3TouchControls, same ui::compute_home_layout geometry) maps 1:1.
  renderer->set_margin_top(0);
  renderer->set_margin_left(0);
  renderer->set_margin_right(0);
  renderer->set_margin_bottom(0);
  renderer->clear_screen();

  int W = renderer->get_page_width();  // full panel width (margins 0)
  int H = renderer->get_page_height(); // full panel height (margins 0)
  int lh = renderer->get_line_height();
  if (lh <= 0)
  {
    lh = 20;
  }
  ui::HomeLayout L = ui::compute_home_layout(W, H, lh);

  const ui::HRect *tiles[2] = {&L.tasks, &L.books};
  const char *labels[2] = {"Tasks", "Books"};
  for (int i = 0; i < 2; i++)
  {
    const ui::HRect &t = *tiles[i];
    // Selected tile (button-board nav) gets a thicker frame.
    int thickness = (i == home_selected) ? ui::HOME_TILE_BORDER + 2 : ui::HOME_TILE_BORDER;
    for (int b = 0; b < thickness; b++)
    {
      renderer->draw_rect(t.x + b, t.y + b, t.w - 2 * b, t.h - 2 * b, 0);
    }
    int tw = renderer->get_text_width(labels[i], false, false);
    if (tw < 0)
    {
      tw = 0;
    }
    renderer->draw_text(t.x + (t.w - tw) / 2, t.y + (t.h - lh) / 2, labels[i], false, false);
  }

  // Settings: centered text in its strip. When selected, draw an inverted
  // strip (black fill + white text) so its highlight reads as clearly as the
  // bordered tiles above.
  {
    const char *s = "Settings";
    int tw = renderer->get_text_width(s, false, false);
    if (tw < 0)
    {
      tw = 0;
    }
    bool selected = (home_selected == 2);
    if (selected)
    {
      renderer->fill_rect(L.settings.x, L.settings.y, L.settings.w, L.settings.h, 0);
      renderer->set_text_inverted(true);
    }
    renderer->draw_text(L.settings.x + (L.settings.w - tw) / 2,
                        L.settings.y + (L.settings.h - lh) / 2, s, selected, false);
    if (selected)
    {
      renderer->set_text_inverted(false);
    }
  }

  renderer->flush_display();
}

// Obsidian task app. Lazily creates the controller; routes touch/UI events to
// it; on exit tears it down and returns to the home menu.
void handleTasks(Renderer *renderer, UIAction action)
{
  apply_ui_font(renderer);
  // Reserve the top strip for the battery icon (the home menu leaves margin_top
  // at 0; without this the task list's full-width row fill overwrites the
  // battery the main loop draws afterwards). Matches the reading view: reserve
  // the status bar at the top and the bezel dead-zone on all sides.
  apply_page_margins(renderer);
  if (!tasks_controller)
  {
    tasks_controller = new TasksController(renderer, g_settings.cfg().tasks_configured());
    tasks_controller->enter(); // fetches from the vault + renders
    return;                    // ignore the action that brought us here
  }

  tasks_controller->handle_action(action);

  if (tasks_controller->exit_requested())
  {
    delete tasks_controller;
    tasks_controller = nullptr;
    ui_state = HOME_MENU;
    handleHome(renderer, NONE, true);
  }
}
#endif // EPAPEROS_TASKS

// Coordinate-driven Settings screen. Lazily created; consumes TAP (logical
// coords in g_tap_x/y). On "Save & Back" returns to the home menu. Editing
// values picks up on the next ObsidianClient/WiFiManager construction (they
// read the same NVS namespace SettingsStore writes).
void handleSettings(Renderer *renderer, UIAction action)
{
  apply_ui_font(renderer);
  if (!settings_screen)
  {
    settings_screen = new settings::SettingsScreen(renderer, &g_settings, g_battery);
    settings_screen->enter();
    return; // ignore the action that brought us here
  }

  if (action == TAP)
  {
    settings_screen->handle_tap(g_tap_x, g_tap_y);
  }

  if (settings_screen->exit_requested())
  {
    delete settings_screen;
    settings_screen = nullptr;

    // "Font px" in Settings controls the UI font (menus, library, tasks,
    // settings) only -- it no longer touches the book reading size. handleHome
    // below re-applies it via apply_ui_font().
    int px = g_settings.cfg().reader_font_px;
    if (px >= 8 && px <= 96)
    {
#ifdef USE_FREETYPE
      g_ui_font_px = px;
#endif
    }
    apply_page_margins(renderer);

#ifdef EPAPEROS_TASKS
    ui_state = HOME_MENU;
    handleHome(renderer, NONE, true);
#else
    ui_state = SELECTING_EPUB;
    handleEpubList(renderer, NONE, true);
#endif
  }
}

void handleUserInteraction(Renderer *renderer, UIAction ui_action, bool needs_redraw)
{
  // Global handling for status bar toggle while reading.
  if (ui_action == TOGGLE_STATUS_BAR && ui_state == READING_EPUB)
  {
    status_bar_visible = !status_bar_visible;
    save_app_settings(renderer);
    // Re-render the current page; draw_status_bar() will
    // pick up the new visibility on the next flush.
    handleEpub(renderer, NONE);
    show_status_bar_toast(renderer, status_bar_visible ? "Status bar ON" : "Status bar OFF");
    return;
  }

  // Physical home button: return to home menu from any screen.
  if (ui_action == GO_HOME && ui_state != HOME_MENU)
  {
    // Flush the reading bookmark to SD before leaving a reading context, so a
    // reset/reflash from the home menu keeps the page (the other reader exits
    // and deep-sleep already persist).
    if (ui_state == READING_EPUB || ui_state == READING_MENU ||
        ui_state == SELECTING_TABLE_CONTENTS)
    {
      persist_reading_progress();
    }
#ifdef EPAPEROS_TASKS
    if (tasks_controller)
    {
      delete tasks_controller;
      tasks_controller = nullptr;
    }
    ui_state = HOME_MENU;
    handleHome(renderer, NONE, true);
#else
    ui_state = SELECTING_EPUB;
    handleEpubList(renderer, NONE, true);
#endif
    return;
  }

  // Home key long-press: cycle a full-panel de-ghost refresh, then repaint the
  // current screen (reset() leaves the panel white, so each screen must redraw).
  if (ui_action == FULL_REFRESH)
  {
    renderer->reset();
    // Match the face to the screen being repainted (the Tasks/Settings
    // branches below draw directly without going through their handlers).
    if (ui_state == READING_EPUB)
    {
      apply_reading_font(renderer);
    }
    else
    {
      apply_ui_font(renderer);
    }
    switch (ui_state)
    {
    case READING_EPUB:
      handleEpub(renderer, NONE);
      break;
    case READING_MENU:
      renderReaderMenu(renderer);
      break;
    case SELECTING_TABLE_CONTENTS:
      handleEpubTableContents(renderer, NONE, true);
      break;
#ifdef EPAPEROS_TASKS
    case TASKS:
      if (tasks_controller)
      {
        apply_page_margins(renderer);
        tasks_controller->redraw();
      }
      break;
#endif
    case SETTINGS:
      if (settings_screen)
      {
        settings_screen->render();
      }
      break;
    case SELECTING_EPUB:
      handleEpubList(renderer, NONE, true);
      break;
#ifdef EPAPEROS_TASKS
    case HOME_MENU:
    default:
      handleHome(renderer, NONE, true);
      break;
#else
    default:
      handleEpubList(renderer, NONE, true);
      break;
#endif
    }
    return;
  }

  // From the library view, allow a gesture (e.g. two-finger swipe up)
  // to open the reader menu directly, focusing on advanced settings.
  if (ui_action == OPEN_READER_MENU && ui_state == SELECTING_EPUB)
  {
    ui_state = READING_MENU;
    reader_menu_advanced = true;
    reader_menu_selected = 0;
    renderReaderMenu(renderer);
    return;
  }

  switch (ui_state)
  {
#ifdef EPAPEROS_TASKS
  case HOME_MENU:
    handleHome(renderer, ui_action, needs_redraw);
    break;
  case TASKS:
    handleTasks(renderer, ui_action);
    break;
#endif
  case SETTINGS:
    handleSettings(renderer, ui_action);
    break;
  case READING_MENU:
    handleReaderMenu(renderer, ui_action);
    break;
  case READING_EPUB:
    if (ui_action == SELECT)
    {
      ui_state = READING_MENU;
      reader_menu_selected = 0;
      renderReaderMenu(renderer);
    }
    else
    {
      handleEpub(renderer, ui_action);
    }
    break;
  case SELECTING_TABLE_CONTENTS:
    handleEpubTableContents(renderer, ui_action, needs_redraw);
    break;
  case SELECTING_EPUB:
  default:
    handleEpubList(renderer, ui_action, needs_redraw);
    break;
  }
}

// Draw the top status strip: time (left), date (center), battery icon + percent
// (right), and a separator line underneath. Drawn across the full panel width in
// absolute coordinates; the current screen's margins are saved and restored so
// the bar is purely transient overlay state.
void draw_status_bar(Renderer *renderer, float voltage, float percentage)
{
  (void)voltage;
  // Hidden: leave the active screen's margins untouched (each screen already
  // reserves DEADZONE_TOP instead of the bar when status_bar_visible is false).
  if (!status_bar_visible)
  {
    return;
  }

  // Refresh the clock strings from the system time on every paint.
  timesync::format_clock(g_status_time, sizeof(g_status_time), g_status_date, sizeof(g_status_date));

  // Save margins, zero them so we draw in absolute panel coordinates.
  int s_top = renderer->get_margin_top();
  int s_left = renderer->get_margin_left();
  int s_right = renderer->get_margin_right();
  int s_bottom = renderer->get_margin_bottom();
  renderer->set_margin_top(0);
  renderer->set_margin_left(0);
  renderer->set_margin_right(0);
  renderer->set_margin_bottom(0);

  int W = renderer->get_page_width(); // full panel width (margins now 0)
  int lh = renderer->get_line_height();
  if (lh <= 0)
  {
    lh = 20;
  }
  int sb_h = status_bar_height(renderer);
  int text_y = DEADZONE_TOP;

  // Wipe the strip so a repaint (e.g. minute tick) does not ghost.
  renderer->fill_rect(0, 0, W, sb_h, 255);

  // Time, left-aligned inside the dead-zone.
  renderer->draw_text(DEADZONE_LEFT, text_y, g_status_time, false, false);

  // Date, centered.
  int dw = renderer->get_text_width(g_status_date, false, false);
  if (dw < 0)
  {
    dw = 0;
  }
  renderer->draw_text((W - dw) / 2, text_y, g_status_date, false, false);

  // Battery icon + percent, right-aligned. Icon height tracks the font.
  int icon_h = (lh * 3) / 5;
  if (icon_h < 12)
  {
    icon_h = 12;
  }
  int icon_w = icon_h * 2;
  int nub_w = icon_h / 4;
  if (nub_w < 2)
  {
    nub_w = 2;
  }
  int icon_y = text_y + (lh - icon_h) / 2;
  int icon_x = W - DEADZONE_RIGHT - nub_w - icon_w;

  if (percentage < 0)
  {
    percentage = 0;
  }
  if (percentage > 100)
  {
    percentage = 100;
  }
  int fill_w = (int)(icon_w * percentage / 100);
  renderer->fill_rect(icon_x, icon_y, icon_w, icon_h, 255);
  renderer->fill_rect(icon_x, icon_y, fill_w, icon_h, 0);
  renderer->draw_rect(icon_x, icon_y, icon_w, icon_h, 0);
  renderer->fill_rect(icon_x + icon_w, icon_y + icon_h / 4, nub_w, icon_h / 2, 0);

  char pct_buf[8];
  snprintf(pct_buf, sizeof(pct_buf), "%d%%", (int)percentage);
  int pw = renderer->get_text_width(pct_buf, false, false);
  if (pw > 0)
  {
    int tx = icon_x - 6 - pw;
    renderer->draw_text(tx, text_y, pct_buf, false, false);
  }

  // Separator line under the strip.
  int sep_y = text_y + lh + 3;
  renderer->fill_rect(DEADZONE_LEFT, sep_y, W - DEADZONE_LEFT - DEADZONE_RIGHT, 2, 0);

  // Restore the active screen's margins.
  renderer->set_margin_top(s_top);
  renderer->set_margin_left(s_left);
  renderer->set_margin_right(s_right);
  renderer->set_margin_bottom(s_bottom);
}

static void show_library_loading(Renderer *renderer)
{
  renderer->clear_screen();
  int page_width = renderer->get_page_width();
  int page_height = renderer->get_page_height();
  int line_height = renderer->get_line_height();
  if (page_width <= 0 || page_height <= 0 || line_height <= 0)
  {
    return;
  }

  const char *msg = "Book library is loading";
  int text_width = renderer->get_text_width(msg, false, false);
  if (text_width < 0)
  {
    text_width = 0;
  }

  int x = (page_width - text_width) / 2;
  if (x < 0)
  {
    x = 0;
  }
  int center_y = page_height / 2;
  int y = center_y - (3 * line_height) / 4;

  renderer->draw_text(x, y, msg, false, false);
  renderer->flush_display();
}

static void show_sleep_cover(Renderer *renderer)
{
  int book_index = -1;
  if (epub_list_state.num_epubs > 0)
  {
    if (ui_state == READING_EPUB || ui_state == READING_MENU || ui_state == SELECTING_TABLE_CONTENTS)
    {
      book_index = epub_list_state.selected_item;
    }
    if (book_index < 0)
    {
      book_index = find_last_open_book_index();
    }
  }

  if (book_index < 0)
  {
    return;
  }

  EpubListItem &item = epub_list_state.epub_list[book_index];
  if (item.cover_path[0] == '\0')
  {
    return;
  }

  Epub epub(item.path);
  if (!epub.load())
  {
    return;
  }

  size_t image_data_size = 0;
  uint8_t *image_data = epub.get_item_contents(item.cover_path, &image_data_size);
  if (!image_data || image_data_size == 0)
  {
    free(image_data);
    return;
  }

  int img_w = 0;
  int img_h = 0;
  bool can_render = renderer->get_image_size(item.cover_path, image_data, image_data_size, &img_w, &img_h);
  if (!can_render || img_w <= 0 || img_h <= 0)
  {
    free(image_data);
    return;
  }

  renderer->set_margin_top(0);
  renderer->set_margin_bottom(0);
  renderer->set_margin_left(0);
  renderer->set_margin_right(0);

  int width = renderer->get_page_width();
  int height = renderer->get_page_height();

  float scale = std::min((float)width / img_w, (float)height / img_h);
  int draw_w = (int)(img_w * scale);
  int draw_h = (int)(img_h * scale);
  int x_off = (width - draw_w) / 2;
  int y_off = (height - draw_h) / 2;

  renderer->clear_screen();
  renderer->set_image_placeholder_enabled(false);
  renderer->set_image_enhance(true); // contrast-stretch + dither the cover
  renderer->draw_image(item.cover_path, image_data, image_data_size, x_off, y_off, draw_w, draw_h);
  renderer->set_image_enhance(false);
  renderer->set_image_placeholder_enabled(true);
  free(image_data);
  renderer->flush_display();
}

#ifdef EPAPEROS_TASKS
// Screensaver for the non-reading context: paint the task list. No disk cache
// exists for tasks, so enter() brings WiFi up, fetches the latest from the
// vault, drops the radio, then renders+flushes (Track B coexistence). Reuses a
// live controller if the Tasks app is open; otherwise spins up a throwaway one.
static void show_tasks_screensaver(Renderer *renderer)
{
  // Full bleed: bezel dead-zone only, no status-bar strip reserved at the top
  // (the screensaver draws no status bar / battery).
  renderer->set_margin_top(DEADZONE_TOP);
  renderer->set_margin_left(DEADZONE_LEFT);
  renderer->set_margin_right(DEADZONE_RIGHT);
  renderer->set_margin_bottom(DEADZONE_BOTTOM);
  bool owned = false;
  if (!tasks_controller)
  {
    tasks_controller = new TasksController(renderer, g_settings.cfg().tasks_configured());
    owned = true;
  }
  tasks_controller->show_screensaver(); // WiFi fetch -> task list only, no bar/buttons
  if (owned)
  {
    delete tasks_controller;
    tasks_controller = nullptr;
  }
}
#endif // EPAPEROS_TASKS

static void show_sleep_image(Renderer *renderer)
{
  // Context drives the sleep face: reading -> book cover; everywhere else
  // (home/library/settings/tasks) -> the task list as a screensaver.
  bool in_reading_context = (ui_state == READING_EPUB || ui_state == READING_MENU ||
                             ui_state == SELECTING_TABLE_CONTENTS);
#ifdef EPAPEROS_TASKS
  if (!in_reading_context)
  {
    show_tasks_screensaver(renderer);
    return;
  }
#endif

  if (sleep_image_mode == SLEEP_IMAGE_OFF)
  {
    return;
  }

  if (sleep_image_mode == SLEEP_IMAGE_COVER)
  {
    show_sleep_cover(renderer);
    return;
  }

  if (sleep_image_mode != SLEEP_IMAGE_RANDOM)
  {
    return;
  }

  const char *pics_dir = nullptr;
  DIR *dir = nullptr;
  const char *candidates[] = {"/fs/Images", "/fs/images", "/fs/Pics"};
  for (int i = 0; i < 2; i++)
  {
    dir = opendir(candidates[i]);
    if (dir)
    {
      pics_dir = candidates[i];
      break;
    }
  }
  if (!dir)
  {
    ESP_LOGW("main", "Sleep image directory not found: %s", "/fs/Images");
    show_sleep_cover(renderer);
    return;
  }

  char selected_path[512] = {0};
  int image_count = 0;
  struct dirent *ent;

  while ((ent = readdir(dir)) != NULL)
  {
    if (ent->d_name[0] == '.' || ent->d_type == DT_DIR)
    {
      continue;
    }

    const char *dot = strrchr(ent->d_name, '.');
    if (!dot || !dot[1])
    {
      continue;
    }
    const char *ext = dot + 1;
    char e0 = tolower(ext[0]);
    char e1 = tolower(ext[1]);
    char e2 = tolower(ext[2]);
    char e3 = tolower(ext[3]);

    bool is_jpg = (e0 == 'j' && e1 == 'p' && e2 == 'g' && ext[3] == '\0');
    bool is_jpeg = (e0 == 'j' && e1 == 'p' && e2 == 'e' && e3 == 'g' && ext[4] == '\0');
    bool is_png = (e0 == 'p' && e1 == 'n' && e2 == 'g' && ext[3] == '\0');
    if (!is_jpg && !is_jpeg && !is_png)
    {
      continue;
    }

    // Reservoir sampling so each image has equal probability.
    image_count++;
    if (image_count == 1)
    {
      snprintf(selected_path, sizeof(selected_path), "%s/%s", pics_dir, ent->d_name);
    }
    else
    {
      uint32_t r = esp_random();
      if (r % (uint32_t)image_count == 0)
      {
        snprintf(selected_path, sizeof(selected_path), "%s/%s", pics_dir, ent->d_name);
      }
    }
  }
  closedir(dir);

  if (image_count == 0 || selected_path[0] == '\0')
  {
    ESP_LOGW("main", "No image files found in %s", pics_dir);
    show_sleep_cover(renderer);
    return;
  }

  const char *sleep_image_path = selected_path;
  FILE *fp = fopen(sleep_image_path, "rb");
  if (!fp)
  {
    ESP_LOGW("main", "Sleep image not found at %s", sleep_image_path);
    show_sleep_cover(renderer);
    return;
  }

  if (fseek(fp, 0, SEEK_END) != 0)
  {
    fclose(fp);
    show_sleep_cover(renderer);
    return;
  }
  long size = ftell(fp);
  if (size <= 0)
  {
    fclose(fp);
    ESP_LOGW("main", "Sleep image file has invalid size: %s", sleep_image_path);
    show_sleep_cover(renderer);
    return;
  }
  if (fseek(fp, 0, SEEK_SET) != 0)
  {
    fclose(fp);
    show_sleep_cover(renderer);
    return;
  }

  uint8_t *data = (uint8_t *)malloc((size_t)size);
  if (!data)
  {
    fclose(fp);
    ESP_LOGW("main", "Failed to allocate memory for sleep image");
    show_sleep_cover(renderer);
    return;
  }

  size_t read = fread(data, 1, (size_t)size, fp);
  fclose(fp);
  if (read != (size_t)size)
  {
    free(data);
    ESP_LOGW("main", "Failed to read full sleep image");
    show_sleep_cover(renderer);
    return;
  }

  int img_w = 0;
  int img_h = 0;
  bool can_render = renderer->get_image_size(sleep_image_path, data, (size_t)size, &img_w, &img_h);
  if (!can_render || img_w <= 0 || img_h <= 0)
  {
    free(data);
    ESP_LOGW("main", "Sleep image decode failed: %s", sleep_image_path);
    show_sleep_cover(renderer);
    return;
  }

  // Draw full-screen, ignoring margins.
  renderer->set_margin_top(0);
  renderer->set_margin_bottom(0);
  renderer->set_margin_left(0);
  renderer->set_margin_right(0);

  int width = renderer->get_page_width();
  int height = renderer->get_page_height();

  float scale = std::min((float)width / img_w, (float)height / img_h);
  int draw_w = (int)(img_w * scale);
  int draw_h = (int)(img_h * scale);
  int x_off = (width - draw_w) / 2;
  int y_off = (height - draw_h) / 2;

  renderer->clear_screen();
  // For sleep images we do not want to show the generic cover placeholder
  // if decoding fails; just leave the screen as-is in that case.
  renderer->set_image_placeholder_enabled(false);
  renderer->set_image_enhance(true); // contrast-stretch + dither the sleep image
  renderer->draw_image(sleep_image_path, data, (size_t)size, x_off, y_off, draw_w, draw_h);
  renderer->set_image_enhance(false);
  renderer->set_image_placeholder_enabled(true);
  free(data);
  renderer->flush_display();
}

void main_task(void *param)
{
  // start the board up
  ESP_LOGI("main", "Powering up the board");
  Board *board = Board::factory();
  board->power_up();
  // create the renderer for the board
  ESP_LOGI("main", "Creating renderer");
  Renderer *renderer = board->get_renderer();
  // bring the file system up - SPIFFS or SDCard depending on the defines in platformio.ini
  ESP_LOGI("main", "Starting file system");
  board->start_filesystem();

#ifdef USE_FREETYPE
#if defined(BOARD_TYPE_PAPER_S3)
  // For Paper S3, initialize the global FreeType font once the filesystem
  // is available so that all subsequent UI and reading text rendering uses
  // the TTF font from /fs.
  init_freetype_for_paper_s3(renderer);
#endif
#endif

  load_app_settings(renderer);

  // NVS must be up before SettingsStore can read persisted config (WiFi creds,
  // Obsidian transport, reader prefs). Idempotent with WiFiManager's own init.
  esp_err_t nvs_err = nvs_flash_init();
  if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND)
  {
    nvs_flash_erase();
    nvs_flash_init();
  }
  g_settings.load();
  // One-time seed: if SD has settings.ini, import it into NVS (handles long
  // URLs too big to type) and rename it so on-device edits aren't clobbered on
  // later boots. Drop a fresh settings.ini to re-seed.
  g_settings.load_from_sd("/fs/settings.ini");

#ifdef USE_FREETYPE
  // Seed the UI font size from NVS ("Font px") and make the shared face start
  // at the UI size; book pages switch it to the reading size on demand.
  {
    int ui_px = g_settings.cfg().reader_font_px;
    if (ui_px >= 8 && ui_px <= 96)
    {
      g_ui_font_px = ui_px;
    }
    apply_ui_font(renderer);
  }
#endif

  // battery details
  ESP_LOGI("main", "Starting battery monitor");
  g_battery = board->get_battery();
  Battery *battery = g_battery;
  if (battery)
  {
    battery->setup();
  }

  apply_page_margins(renderer);

  // create a message queue for UI events
  QueueHandle_t ui_queue = xQueueCreate(10, sizeof(UIAction));

  // set the controls up
  ESP_LOGI("main", "Setting up controls");
  ButtonControls *button_controls = board->get_button_controls(ui_queue);
  TouchControls *touch_controls = board->get_touch_controls(renderer, ui_queue);

  ESP_LOGI("main", "Controls configured");

  // Time: set the timezone. We deliberately do NOT seed the system clock from
  // the PCF8563 on every boot. On this board the PCF8563 oscillator FREEZES
  // during deep sleep (no backup keeps it clocked), so on a deep-sleep wake its
  // registers still read the sleep-ENTRY time -- seeding from it would shove the
  // clock backward by the whole sleep duration. ESP-IDF keeps wall-clock across
  // deep sleep on its own RTC timer (far more accurate here), so on wake we keep
  // that. Only cold boot seeds from the RTC (see the cold-boot branch below).
  timesync::init_timezone();
  // work out if we were woken from deep sleep
  if (button_controls->did_wake_from_deep_sleep())
  {
    // restore the renderer state - it should have been saved when we went to sleep...
    bool hydrate_success = renderer->hydrate();
    UIAction ui_action = button_controls->get_deep_sleep_action();

#if defined(BOARD_TYPE_PAPER_S3)
    // Paper S3 has no navigation buttons, so a deep-sleep wake resumes whatever
    // screen we slept on (recorded in g_sleep_ui_state). If we slept inside the
    // reader, rebuild the EPUB list and jump back into the last-open book/page;
    // otherwise restore that screen (Tasks / Home / Settings).
    if (!epub_list)
    {
      epub_list = new EpubList(renderer, epub_list_state);
      epub_list->load("/fs/Books");
    }
    bool slept_reading = (g_sleep_ui_state == READING_EPUB ||
                          g_sleep_ui_state == READING_MENU ||
                          g_sleep_ui_state == SELECTING_TABLE_CONTENTS);
    if (slept_reading || g_sleep_ui_state < 0)
    {
      // Prefer the exact book we slept on (recorded in RTC). Fall back to the
      // progress heuristic only if that index is missing/stale (e.g. the library
      // changed while asleep).
      int last_book_index = -1;
      if (g_sleep_book_index >= 0 && g_sleep_book_index < epub_list_state.num_epubs)
      {
        last_book_index = g_sleep_book_index;
      }
      else
      {
        last_book_index = find_last_open_book_index();
      }
      if (last_book_index >= 0)
      {
        epub_list_state.selected_item = last_book_index;
        ui_state = READING_EPUB;
      }
    }
    else
    {
      ui_state = (UIState)g_sleep_ui_state;
    }
    // No deep-sleep button action on Paper S3; just render the restored screen.
    ui_action = NONE;
#endif

    handleUserInteraction(renderer, ui_action, !hydrate_success);
  }
  else
  {
    // reset the screen
    renderer->reset();
    // Cold boot: seed the system clock from the PCF8563 first (its registers are
    // retained across a power-off, unlike across deep sleep), so we have a
    // plausible time even if NTP is unavailable. NTP below overrides it when it
    // succeeds. (On deep-sleep wake this is skipped -- ESP keeps wall-clock.)
    timesync::apply_rtc_to_system();
    // Cold boot only: if WiFi is configured, auto-sync the clock over NTP before
    // showing the UI. ntp_sync_with_wifi brings the radio up, syncs the RTC, and
    // tears it down (honoring the no-refresh-while-radio-up rule). Skipped on
    // deep-sleep resume so returning to a book stays instant.
#ifdef EPAPEROS_TASKS
    if (!g_settings.cfg().wifi_ssid.empty())
    {
      timesync::ntp_sync_with_wifi(renderer);
    }
#endif
    show_library_loading(renderer);
    // preload the library so entering "Books" from the home menu is instant
    if (!epub_list)
    {
      epub_list = new EpubList(renderer, epub_list_state);
      epub_list->load("/fs/Books");
    }
#ifdef EPAPEROS_TASKS
    // Cold boot lands on the home menu (Books / Tasks / Settings). First run
    // (Tasks not yet configured) lands in Settings so the user can enter WiFi +
    // Obsidian before anything tries to sync. Reader still works offline.
    if (!g_settings.cfg().tasks_configured())
    {
      ui_state = SETTINGS;
    }
    else
    {
      ui_state = HOME_MENU;
    }
#else
    // Pure reader build: cold boot lands straight in the library.
    ui_state = SELECTING_EPUB;
#endif
    // make sure the UI is in the right state
    handleUserInteraction(renderer, NONE, true);
  }

  // draw the battery level before flushing the screen
  if (battery)
  {
    draw_status_bar(renderer, battery->get_voltage(), battery->get_percentage());
  }
  touch_controls->render(renderer);
  renderer->flush_display();

  // keep track of when the user last interacted and go to sleep after N seconds
  int64_t last_user_interaction = esp_timer_get_time();
  int64_t last_battery_update = last_user_interaction;
  bool screen_dirty = false;
  const int64_t battery_update_interval_us = 60 * 1000 * 1000;
  while (true)
  {
    if (g_request_sleep_now)
    {
      break;
    }

    bool in_reading_context = (ui_state == READING_EPUB || ui_state == READING_MENU || ui_state == SELECTING_TABLE_CONTENTS);
    int64_t idle_timeout_us = in_reading_context ? idle_timeout_reading_us : idle_timeout_library_us;
    if (esp_timer_get_time() - last_user_interaction >= idle_timeout_us)
    {
      break;
    }
    UIAction ui_action = NONE;
    // wait for something to happen for 60 seconds
    if (xQueueReceive(ui_queue, &ui_action, pdMS_TO_TICKS(60000)) == pdTRUE)
    {
      if (ui_action == REQUEST_SLEEP)
      {
        // Boot button pressed: drop straight to the sleep sequence.
        g_request_sleep_now = true;
        continue;
      }
      if (ui_action != NONE)
      {
        // something happened!
        last_user_interaction = esp_timer_get_time();
        // show feedback on the touch controls
        touch_controls->renderPressedState(renderer, ui_action);
        handleUserInteraction(renderer, ui_action, false);

        // epd_fullclear() blocks for ~2 s. Drain any touch events that
        // accumulated in the queue during that window (GT911 phantom touches
        // from display waveforms) so they don't fire on the fresh screen.
        if (ui_action == FULL_REFRESH)
        {
          UIAction discard;
          while (xQueueReceive(ui_queue, &discard, 0) == pdTRUE) {}
        }

        // make sure to clear the feedback on the touch controls
        touch_controls->render(renderer);
        if (battery)
        {
          draw_status_bar(renderer, battery->get_voltage(), battery->get_percentage());
        }
        screen_dirty = true;
      }
    }
    int64_t now = esp_timer_get_time();
    if (battery && (now - last_battery_update) >= battery_update_interval_us)
    {
      last_battery_update = now;
      ESP_LOGI("main", "Battery Level %f, percent %d", battery->get_voltage(), battery->get_percentage());
      draw_status_bar(renderer, battery->get_voltage(), battery->get_percentage());

      // Full panel width (status bar is drawn margin-free; battery sits at the
      // far right edge, which the content width would miss). flush_area is absolute.
      int top_width = renderer->get_page_width() + renderer->get_margin_left() + renderer->get_margin_right();
      int top_height = status_bar_height(renderer) + 4;
      if (top_width > 0 && top_height > 0)
      {
        renderer->flush_area(0, 0, top_width, top_height);
      }
    }
    if (screen_dirty)
    {
      renderer->flush_display();
      screen_dirty = false;

      // The reading page is now on the panel. Use the idle time before the
      // next interaction to lay out the NEXT section, so crossing a chapter
      // boundary is an instant prefetched-parser swap instead of a multi-second
      // parse. Synchronous on purpose (shared FreeType face is not reentrant);
      // a tap arriving mid-prefetch simply queues and is handled after.
      if (ui_state == READING_EPUB && reader)
      {
        reader->prefetch();
      }
    }
  }
  // Persist EPUB list state (including current section/page) so that
  // cold boots and deep-sleep resumes can restore the last-read book
  // and page via the BOOKS.IDX index.
  if (epub_list)
  {
    epub_list->save_index("/fs/Books/BOOKS.IDX");
  }
  // Remember the screen we slept on so the wake path can return to it (Paper S3
  // has no nav buttons and otherwise always resumes the last-open book).
  g_sleep_ui_state = (int)ui_state;
  // Remember exactly which book was open so the wake path resumes *this* book,
  // not whichever in-progress book happens to have the highest page number.
  g_sleep_book_index = epub_list_state.selected_item;
  show_sleep_image(renderer);
  ESP_LOGI("main", "Saving state");
  // save the state of the renderer
  renderer->dehydrate();
  // turn off the filesystem
  board->stop_filesystem();
  // get ready to go to sleep
  board->prepare_to_sleep();
  // Put the touch IC and charger ADC into their lowest-power states.
  // GT911 active mode draws ~20mA; sleep mode drops it to ~100µA.
  // BQ25896 continuous ADC draws ~2mA; stopping it before sleep saves that.
  touch_controls->prepare_for_deep_sleep();
  if (battery)
  {
    battery->prepare_for_deep_sleep();
  }
  // Diagnostic: snapshot the RTC time at sleep-entry. Compare this to the
  // "System clock set from RTC" line printed on the next wake -- the difference
  // is how much the PCF8563 advanced while asleep. ~= real sleep duration means
  // the chip keeps time (any error is set-once drift); barely advancing means
  // the oscillator is frozen during deep sleep.
  {
    struct tm rt = {};
    bool rt_valid = false;
    if (rtc::read(&rt, &rt_valid) && rt_valid)
    {
      ESP_LOGI("main", "RTC at sleep-entry: %04d-%02d-%02d %02d:%02d:%02d",
               rt.tm_year + 1900, rt.tm_mon + 1, rt.tm_mday, rt.tm_hour, rt.tm_min, rt.tm_sec);
    }
    else
    {
      ESP_LOGW("main", "RTC read failed/invalid at sleep-entry");
    }
  }
  ESP_LOGI("main", "Entering deep sleep");
  // Configure the wake source (boot button ext0 on Paper S3). This also waits
  // for the button to be released so a held press doesn't re-wake immediately.
  button_controls->setup_deep_sleep();
  vTaskDelay(pdMS_TO_TICKS(500));
  // go to sleep
  esp_deep_sleep_start();
}

void app_main()
{
  // Logging control
  esp_log_level_set("main", LOG_LEVEL);
  esp_log_level_set("EPUB", LOG_LEVEL);
  esp_log_level_set("PUBLIST", LOG_LEVEL);
  esp_log_level_set("ZIP", LOG_LEVEL);
  esp_log_level_set("JPG", LOG_LEVEL);
  esp_log_level_set("TOUCH", LOG_LEVEL);

  // dump out the epub list state
  ESP_LOGI("main", "epub list state num_epubs=%d", epub_list_state.num_epubs);
  ESP_LOGI("main", "epub list state is_loaded=%d", epub_list_state.is_loaded);
  ESP_LOGI("main", "epub list state selected_item=%d", epub_list_state.selected_item);

  // ESP_LOGI("main", "Memory before main task start %d", esp_get_free_heap_size());
  ESP_LOGI("main", "Memory before main task start %" PRIu32, esp_get_free_heap_size());
  xTaskCreatePinnedToCore(main_task, "main_task", 32768, NULL, 1, NULL, 1);
}