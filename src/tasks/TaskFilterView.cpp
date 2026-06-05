#include "TaskFilterView.h"
#include "DateUtil.h"
#include <Renderer/Renderer.h>

void TaskFilterView::open(const std::string &today_iso)
{
  m_today = today_iso;
  m_date = today_iso;
  m_clear = false;
  m_step = Step::Menu;
}

bool TaskFilterView::active() const
{
  return m_step != Step::Inactive && m_step != Step::Done &&
         m_step != Step::Cancelled;
}

void TaskFilterView::begin_numpick()
{
  int y, mo, d;
  if (dateutil::parse(m_today, y, mo, d))
  {
    m_y = y;
    m_mo = mo;
    m_d = d;
  }
}

void TaskFilterView::render_menu(Renderer *r)
{
  std::vector<std::vector<ui::PanelBtn>> rows = {
      {{"Yesterday", "yday"}, {"Tomorrow", "tmrw"}},
      {{"Pick date...", "pick"}},
      {{"Default view", "default"}, {"Cancel", "cancel"}},
  };
  m_panel.draw(r, "View tasks for day", rows);
}

void TaskFilterView::render_numpick(Renderer *r)
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
  m_panel.draw(r, "View tasks for day", rows);
}

void TaskFilterView::render_step(Renderer *r, bool full)
{
  switch (m_step)
  {
  case Step::Menu:
    render_menu(r);
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

void TaskFilterView::render(Renderer *r)
{
  render_step(r, true);
}

void TaskFilterView::handle_tap(Renderer *r, int x, int y)
{
  if (!r)
  {
    return;
  }

  if (m_step == Step::Menu)
  {
    std::string id = m_panel.hit(x, y);
    if (id.empty())
    {
      return;
    }
    if (id == "cancel")
    {
      m_step = Step::Cancelled;
    }
    else if (id == "default")
    {
      m_clear = true;
      m_step = Step::Done;
    }
    else if (id == "yday")
    {
      m_date = dateutil::plus(m_today, -1);
      m_step = Step::Done;
    }
    else if (id == "tmrw")
    {
      m_date = dateutil::plus(m_today, 1);
      m_step = Step::Done;
    }
    else if (id == "pick")
    {
      begin_numpick();
      m_step = Step::NumPick;
      render_step(r, true);
    }
    return;
  }

  if (m_step == Step::NumPick)
  {
    std::string id = m_panel.hit(x, y);
    if (id.empty())
    {
      return;
    }
    if (id == "cancel")
    {
      m_step = Step::Menu; // back to the menu
      render_step(r, true);
      return;
    }
    if (id == "ok")
    {
      m_date = dateutil::format(m_y, m_mo, m_d);
      m_step = Step::Done;
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
      m_d = dim;
    }
    if (m_d > dim)
    {
      m_d = 1;
    }
    render_step(r, false);
    return;
  }
}
