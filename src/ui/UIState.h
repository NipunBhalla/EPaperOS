#pragma once

// Canonical top-level UI state, shared by main.cpp (the owner of `ui_state`)
// and the touch driver so that tap hit-testing branches on the SAME integer
// values. Previously main.cpp and PaperS3TouchControls.cpp each declared their
// own UIState enum with different ordering, so the touch layer mis-identified
// screens (HOME_MENU was read as SELECTING_EPUB, TASKS fell out of range).
//
// D2 (touch UI) adds SETTINGS and KEYBOARD modes on top of Track D.
typedef enum
{
  HOME_MENU,
  SELECTING_EPUB,
  SELECTING_TABLE_CONTENTS,
  READING_EPUB,
  READING_MENU,
  TASKS,
  SETTINGS,
  KEYBOARD
} UIState;
