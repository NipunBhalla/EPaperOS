#include "TasksController.h"
#include "ObsidianStore.h"
#include "LocalSdStore.h"
#include <Renderer/Renderer.h>
#include <esp_log.h>
#include <ctime>

static const char *TAG = "TASKSCTRL";

// Logical-page tap coordinates published by the touch driver (defined in
// main.cpp), consumed when a TAP action arrives.
extern volatile int g_tap_x;
extern volatile int g_tap_y;

TasksController::TasksController(Renderer *renderer, bool use_remote)
    : renderer(renderer), screen(renderer, model)
{
  if (use_remote)
  {
    store = new ObsidianStore(screen);
  }
  else
  {
    store = new LocalSdStore(screen);
  }
}

TasksController::~TasksController()
{
  delete store;
}

bool TasksController::fetch()
{
  std::string md;
  if (!store->fetch(md))
  {
    return false;
  }
  model.load(md);
  model.set_today(today_iso()); // hide done tasks not completed today
  ESP_LOGI(TAG, "Loaded %d visible tasks", (int)model.size());
  return true;
}

bool TasksController::push()
{
  if (!model.dirty())
  {
    return true; // nothing to write
  }
  if (!store->put(model.to_markdown()))
  {
    return false;
  }
  model.clear_dirty();
  ESP_LOGI(TAG, "Saved tasks");
  return true;
}

void TasksController::enter()
{
  m_exit_requested = false;
  fetch();
  screen.set_needs_redraw();
  screen.render();
}

void TasksController::redraw()
{
  screen.set_needs_redraw();
  screen.render();
}

void TasksController::show_screensaver()
{
  fetch(); // pull latest from the store (remote backend sequences WiFi inside)
  screen.set_needs_redraw();
  screen.render(true); // task list only: no cursor, no action rows
}

// Today as ISO "YYYY-MM-DD" from the (RTC-backed) system clock. Empty string if
// the clock has not been set, which suppresses done-date stamping.
std::string TasksController::today_iso() const
{
  time_t now = time(nullptr);
  struct tm *lt = localtime(&now);
  if (!lt || lt->tm_year < 120) // before 2020 => clock not set
  {
    return "";
  }
  char buf[16];
  strftime(buf, sizeof(buf), "%Y-%m-%d", lt);
  return std::string(buf);
}

void TasksController::start_add()
{
  m_add.open(today_iso());
  m_add.render(renderer);
}

void TasksController::start_filter()
{
  m_filter.open(today_iso());
  m_filter.render(renderer);
}

void TasksController::handle_action(UIAction action)
{
  // Modal add-task wizard: while active, route taps to it and ignore
  // navigation. On completion insert the collected task into its priority
  // section (dirty until synced); Cancel discards.
  if (m_add.active())
  {
    if (action == TAP)
    {
      m_add.handle_tap(renderer, g_tap_x, g_tap_y);
      if (m_add.done())
      {
        model.add(m_add.result());
        screen.set_needs_redraw();
        screen.render();
      }
      else if (m_add.cancelled())
      {
        screen.set_needs_redraw();
        screen.render();
      }
    }
    return;
  }

  // Modal calendar filter: pick a day (historical view) or restore the default.
  if (m_filter.active())
  {
    if (action == TAP)
    {
      m_filter.handle_tap(renderer, g_tap_x, g_tap_y);
      if (m_filter.done())
      {
        if (m_filter.clear_to_default())
        {
          model.set_today(today_iso());
        }
        else
        {
          model.set_filter_day(m_filter.date());
        }
        screen.reset_cursor();
        screen.set_needs_redraw();
        screen.render();
      }
      else if (m_filter.cancelled())
      {
        screen.set_needs_redraw();
        screen.render();
      }
    }
    return;
  }

  switch (action)
  {
  // Direction intentionally swapped vs. other screens: UP advances the cursor,
  // DOWN moves it back (matches this view's physical button layout).
  case UP:
    screen.next();
    screen.render();
    break;
  case DOWN:
    screen.prev();
    screen.render();
    break;
  case TAP:
  {
    // Fixed bottom bar takes priority over the list (it spans full width).
    int btn = screen.button_at_abs(g_tap_x, g_tap_y);
    if (btn >= 0)
    {
      screen.set_cursor(btn);
      // fall through to SELECT to run the button's action
    }
    else
    {
      // Right 1/4 of the LIST region = invisible scroll zone (clamped so it
      // never overlaps the status bar or the bottom button bar).
      int sc = screen.scroll_hit(g_tap_x, g_tap_y);
      if (sc)
      {
        if (sc == 1)
          screen.scroll_up();
        else
          screen.scroll_down();
        screen.render();
        break;
      }
      int row = screen.row_at_abs_y(g_tap_y);
      if (row < 0)
      {
        break;
      }
      screen.set_cursor(row);
      // fall through to the same handling as SELECT
    }
  }
  // fallthrough
  case SELECT:
    if (screen.on_add_row())
    {
      start_add();
    }
    else if (screen.on_sync_row())
    {
      push();  // PUT if dirty (WiFi sequenced inside); stay on the list
      fetch(); // pull latest back from the vault
      screen.set_needs_redraw();
      screen.render();
    }
    else if (screen.on_save_row())
    {
      push();                  // "Exit": PUT if dirty (WiFi sequenced inside)
      m_exit_requested = true; // back to home menu
    }
    else if (screen.on_filter_row())
    {
      start_filter(); // calendar: pick a day to view historically
    }
    else
    {
      model.toggle(screen.selected_task(), today_iso());
      screen.render_toggle(); // partial refresh just this line
    }
    break;
  case NONE:
  default:
    break;
  }
}
