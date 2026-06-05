#include "TaskAddView.h"
#include "DateUtil.h"
#include <Renderer/Renderer.h>

// Tap coordinates arrive in CONTENT space (the touch driver already subtracts
// the active margins), the same space draw_* takes. So panels are drawn and
// hit-tested in identical coords here -- no margin math (see ui::ButtonPanel).

namespace
{
  std::string trim(const std::string &s)
  {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t'))
    {
      a++;
    }
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t'))
    {
      b--;
    }
    return s.substr(a, b - a);
  }
}

void TaskAddView::open(const std::string &today_iso)
{
  m_task = NewTask();
  m_today = today_iso;
  m_step = Step::Text;
  m_kb.open("", false, "New task");
}

bool TaskAddView::active() const
{
  return m_step != Step::Inactive && m_step != Step::Done &&
         m_step != Step::Cancelled;
}

const char *TaskAddView::date_step_title() const
{
  Step s = (m_step == Step::NumPick) ? m_num_return : m_step;
  switch (s)
  {
  case Step::DateStart:
    return "Started date?";
  case Step::DateSched:
    return "Scheduled date?";
  case Step::DateDue:
    return "Due date?";
  default:
    return "Date?";
  }
}

void TaskAddView::begin_numpick()
{
  int y, mo, d;
  if (dateutil::parse(m_today, y, mo, d))
  {
    m_y = y;
    m_mo = mo;
    m_d = d;
  }
}

// ----- rendering -----

void TaskAddView::render_priority(Renderer *r)
{
  std::vector<std::vector<ui::PanelBtn>> rows = {
      {{"High (P1)", "high"}, {"Medium (P2)", "med"}},
      {{"Low (P3)", "low"}, {"None (P3)", "none"}},
      {{"Cancel", "cancel"}},
  };
  m_panel.draw(r, "Priority?", rows);
}

void TaskAddView::render_date(Renderer *r, const char *title)
{
  std::vector<std::vector<ui::PanelBtn>> rows = {
      {{"Today", "today"}, {"Tomorrow", "tomorrow"}},
      {{"+3 days", "p3"}, {"+1 week", "p7"}},
      {{"None", "none"}, {"Pick...", "pick"}},
      {{"Cancel", "cancel"}},
  };
  m_panel.draw(r, title, rows);
}

void TaskAddView::render_numpick(Renderer *r)
{
  std::string iso = dateutil::format(m_y, m_mo, m_d);
  char ys[8], ms[8], ds[8];
  std::snprintf(ys, sizeof(ys), "%04d", m_y);
  std::snprintf(ms, sizeof(ms), "%02d", m_mo);
  std::snprintf(ds, sizeof(ds), "%02d", m_d);

  std::vector<std::vector<ui::PanelBtn>> rows = {
      {{"Year -", "y-"}, {ys, ""}, {"Year +", "y+"}},
      {{"Mon -", "mo-"}, {ms, ""}, {"Mon +", "mo+"}},
      {{"Day -", "d-"}, {ds, ""}, {"Day +", "d+"}},
      {{"Cancel", "cancel"}, {iso, ""}, {"OK", "ok"}},
  };
  m_panel.draw(r, date_step_title(), rows);
}

void TaskAddView::render_step(Renderer *r, bool full)
{
  switch (m_step)
  {
  case Step::Text:
    m_kb.render(r);
    r->flush_display();
    return;
  case Step::Priority:
    render_priority(r);
    break;
  case Step::DateStart:
  case Step::DateSched:
  case Step::DateDue:
    render_date(r, date_step_title());
    break;
  case Step::NumPick:
    render_numpick(r);
    break;
  default:
    return;
  }
  if (full)
  {
    r->flush_display();
  }
  else
  {
    m_panel.flush(r);
  }
}

void TaskAddView::render(Renderer *r)
{
  render_step(r, true);
}

// ----- step logic -----

void TaskAddView::set_date_and_advance(Renderer *r, const std::string &iso)
{
  switch (m_step)
  {
  case Step::DateStart:
    m_task.start = iso;
    m_step = Step::DateSched;
    break;
  case Step::DateSched:
    m_task.scheduled = iso;
    // Scheduled XOR due: only ask for a due date if scheduled was left None.
    if (!iso.empty())
    {
      m_step = Step::Done;
      return;
    }
    m_step = Step::DateDue;
    break;
  case Step::DateDue:
    m_task.due = iso;
    m_step = Step::Done; // all fields collected; host tears the wizard down
    return;
  default:
    return;
  }
  render_step(r, true);
}

void TaskAddView::handle_tap(Renderer *r, int x, int y)
{
  if (!r)
  {
    return;
  }

  switch (m_step)
  {
  case Step::Text:
  {
    m_kb.handle_tap(r, x, y);
    if (m_kb.cancelled())
    {
      m_step = Step::Cancelled;
      return;
    }
    if (m_kb.done())
    {
      std::string t = trim(m_kb.buffer());
      if (t.empty())
      {
        m_step = Step::Cancelled; // empty task == cancel
        return;
      }
      m_task.text = t;
      m_step = Step::Priority;
      render_step(r, true);
    }
    return;
  }

  case Step::Priority:
  {
    std::string id = m_panel.hit(x, y);
    if (id.empty())
    {
      return;
    }
    if (id == "cancel")
    {
      m_step = Step::Cancelled;
      return;
    }
    if (id == "high")
    {
      m_task.priority = TaskPriority::High;
    }
    else if (id == "med")
    {
      m_task.priority = TaskPriority::Medium;
    }
    else if (id == "low")
    {
      m_task.priority = TaskPriority::Low;
    }
    else
    {
      m_task.priority = TaskPriority::None;
    }
    m_step = Step::DateStart;
    render_step(r, true);
    return;
  }

  case Step::DateStart:
  case Step::DateSched:
  case Step::DateDue:
  {
    std::string id = m_panel.hit(x, y);
    if (id.empty())
    {
      return;
    }
    if (id == "cancel")
    {
      m_step = Step::Cancelled;
      return;
    }
    if (id == "pick")
    {
      begin_numpick();
      m_num_return = m_step;
      m_step = Step::NumPick;
      render_step(r, true);
      return;
    }
    std::string iso;
    if (id == "today")
    {
      iso = dateutil::plus(m_today, 0);
    }
    else if (id == "tomorrow")
    {
      iso = dateutil::plus(m_today, 1);
    }
    else if (id == "p3")
    {
      iso = dateutil::plus(m_today, 3);
    }
    else if (id == "p7")
    {
      iso = dateutil::plus(m_today, 7);
    }
    // id == "none" leaves iso empty
    set_date_and_advance(r, iso);
    return;
  }

  case Step::NumPick:
  {
    std::string id = m_panel.hit(x, y);
    if (id.empty())
    {
      return;
    }
    if (id == "cancel")
    {
      m_step = m_num_return; // back to the preset row, keep prior value
      render_step(r, true);
      return;
    }
    if (id == "ok")
    {
      m_step = m_num_return;
      set_date_and_advance(r, dateutil::format(m_y, m_mo, m_d));
      return;
    }
    if (id == "y-")
    {
      m_y--;
    }
    else if (id == "y+")
    {
      m_y++;
    }
    else if (id == "mo-")
    {
      if (--m_mo < 1)
      {
        m_mo = 12;
      }
    }
    else if (id == "mo+")
    {
      if (++m_mo > 12)
      {
        m_mo = 1;
      }
    }
    else if (id == "d-")
    {
      m_d--;
    }
    else if (id == "d+")
    {
      m_d++;
    }
    if (m_y < 2000)
    {
      m_y = 2000;
    }
    if (m_y > 2099)
    {
      m_y = 2099;
    }
    int dim = dateutil::days_in_month(m_y, m_mo);
    if (m_d < 1)
    {
      m_d = dim; // wrap
    }
    if (m_d > dim)
    {
      m_d = 1;
    }
    render_step(r, false); // partial refresh of the picker panel
    return;
  }

  default:
    return;
  }
}
