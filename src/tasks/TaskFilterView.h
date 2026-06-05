#pragma once

#include <string>
#include "../ui/ButtonPanel.h"

class Renderer;

// Modal calendar-filter picker, opened from the bottom-bar calendar button.
// Lets the user choose a day to view historically (tasks started, scheduled, or
// completed that day) or return to the default working view.
//
// Result after done(): clear_to_default() true => caller restores the Today
// view; otherwise date() holds the chosen ISO day for TaskModel::set_filter_day.
// Usage mirrors the other modal views (open / handle_tap / poll done/cancelled).
class TaskFilterView
{
public:
  void open(const std::string &today_iso);
  void render(Renderer *r);
  void handle_tap(Renderer *r, int x, int y);

  bool active() const;
  bool done() const { return m_step == Step::Done; }
  bool cancelled() const { return m_step == Step::Cancelled; }
  bool clear_to_default() const { return m_clear; }
  const std::string &date() const { return m_date; }

private:
  enum class Step
  {
    Inactive,
    Menu,
    NumPick,
    Done,
    Cancelled
  };

  Step m_step = Step::Inactive;
  std::string m_today;
  std::string m_date;
  bool m_clear = false;
  int m_y = 2026, m_mo = 1, m_d = 1;

  ui::ButtonPanel m_panel;

  void render_step(Renderer *r, bool full);
  void render_menu(Renderer *r);
  void render_numpick(Renderer *r);
  void begin_numpick();
};
