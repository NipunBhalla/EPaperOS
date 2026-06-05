#pragma once

#include "TaskStore.h"
#include "TaskModel.h"
#include "TaskScreen.h"
#include "TaskAddView.h"
#include "TaskFilterView.h"
#include "../boards/controls/Actions.h"

class Renderer;

// Top-level "Tasks" app. Owns the model/screen and a TaskStore backend (remote
// Obsidian over WiFi, or a local SD file), picked once at construction. With
// the remote backend the store enforces the coexistence rule (WiFi up only
// inside fetch()/push(); no display refresh while the radio is up).
class TasksController
{
private:
  Renderer *renderer;
  TaskModel model;
  TaskScreen screen;
  TaskStore *store = nullptr;
  bool m_exit_requested = false;

  // Multi-step "+ Add Task" wizard (modal while active): text -> priority ->
  // started -> scheduled -> due. On completion its NewTask is inserted into the
  // matching priority section.
  TaskAddView m_add;

  // Calendar filter picker (modal while active): pick a day to view
  // historically, or return to the default working view.
  TaskFilterView m_filter;

  // Load from the store into the model. Returns true on success.
  bool fetch();
  // Serialize the model and write it back through the store. Returns true if
  // nothing was dirty or the write succeeded.
  bool push();
  // Open the add-task wizard.
  void start_add();
  // Open the calendar filter picker.
  void start_filter();
  // Today as ISO "YYYY-MM-DD" from the system clock (RTC-backed), for done
  // stamps and date presets. Empty if the clock is not set.
  std::string today_iso() const;

public:
  // use_remote = true -> Obsidian over WiFi; false -> local SD file.
  TasksController(Renderer *renderer, bool use_remote);
  ~TasksController();

  // Show the list and pull the latest from the store.
  void enter();

  // Sleep face: fetch the latest, then paint only the task list (no cursor, no
  // Add/Sync/Save rows). Same store sequencing as enter().
  void show_screensaver();

  // Handle a single UI button event.
  void handle_action(UIAction action);

  // Repaint the current task list without re-fetching from the vault. Used after
  // a full-panel de-ghost so the screen is restored.
  void redraw();

  // Controller wants main to return to the home menu.
  bool exit_requested() const { return m_exit_requested; }
};
