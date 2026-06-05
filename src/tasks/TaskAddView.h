#pragma once

#include <string>
#include "TaskModel.h"
#include "../ui/KeyboardView.h"
#include "../ui/ButtonPanel.h"

class Renderer;

// Modal multi-step "+ Add Task" wizard. Steps, in order:
//   1. Text      - on-screen keyboard (reuses ui::KeyboardView).
//   2. Priority  - High(P1) / Medium(P2) / Low(P3) / None(P3).
//   3. Started   - date presets (Today/Tomorrow/+3d/+1wk/None) or numeric Pick.
//   4. Scheduled - same date picker. If set, the due step is skipped (a task
//                  carries a scheduled date XOR a due date).
//   5. Due       - same date picker (only when scheduled was left None).
// On completion done() is true and result() holds the collected NewTask; the
// host then calls TaskModel::add(result()). Cancel at any step -> cancelled().
//
// Usage mirrors KeyboardView: host open()s, forwards TAPs via handle_tap(),
// then polls done()/cancelled(). Only TAP drives it; nav buttons are ignored.
class TaskAddView
{
public:
  // Reset to the first step. `today_iso` ("YYYY-MM-DD") seeds the date presets.
  void open(const std::string &today_iso);

  // Draw the current step's panel (full refresh). Call once after open();
  // step transitions repaint themselves.
  void render(Renderer *r);

  // Route a logical-page tap to the current step.
  void handle_tap(Renderer *r, int x, int y);

  bool active() const;
  bool done() const { return m_step == Step::Done; }
  bool cancelled() const { return m_step == Step::Cancelled; }
  const NewTask &result() const { return m_task; }

private:
  enum class Step
  {
    Inactive,
    Text,
    Priority,
    DateStart,
    DateSched,
    DateDue,
    NumPick,
    Done,
    Cancelled
  };

  Step m_step = Step::Inactive;
  Step m_num_return = Step::DateStart; // date step the numeric picker feeds
  NewTask m_task;
  std::string m_today;

  ui::KeyboardView m_kb;
  ui::ButtonPanel m_panel;

  int m_y = 2026, m_mo = 1, m_d = 1; // numeric date-picker working values

  void render_step(Renderer *r, bool full);
  void render_priority(Renderer *r);
  void render_date(Renderer *r, const char *title);
  void render_numpick(Renderer *r);

  void set_date_and_advance(Renderer *r, const std::string &iso);
  void begin_numpick();
  const char *date_step_title() const;
};
