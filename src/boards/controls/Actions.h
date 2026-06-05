#pragma once

#include <functional>

typedef enum
{
  NONE,
  UP,
  DOWN,
  SELECT,
  LAST_INTERACTION,
  PREV_SECTION,
  NEXT_SECTION,
  TOGGLE_STATUS_BAR,
  REFRESH_PAGE,
  OPEN_READER_MENU,
  // Raw coordinate tap, used by coordinate-driven screens (Settings, on-screen
  // keyboard) that hit-test (x,y) themselves rather than consuming a semantic
  // action. The logical-page coordinates are published in g_tap_x / g_tap_y
  // (defined in main.cpp) just before this action is queued.
  TAP,
  // Physical home button pressed — return to home menu from any screen.
  GO_HOME,
  // Home key long-press — de-ghost the whole panel and repaint current screen.
  FULL_REFRESH,
  // Physical boot button pressed — request immediate deep sleep.
  REQUEST_SLEEP
} UIAction;

typedef std::function<void(UIAction)> ActionCallback_t;
