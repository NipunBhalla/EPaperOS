#include "ButtonPanel.h"
#include <Renderer/Renderer.h>

namespace ui
{
void ButtonPanel::draw(Renderer *r, const char *title,
                       const std::vector<std::vector<PanelBtn>> &rows,
                       int top_pct)
{
  int pw = r->get_page_width();
  int ph = r->get_page_height();
  int lh = r->get_line_height();
  if (lh <= 0)
  {
    lh = 24;
  }

  int title_h = lh + 12;
  int panel_top = (ph * top_pct) / 100;
  if (panel_top < 0)
  {
    panel_top = 0;
  }
  m_panel_top = panel_top;

  r->fill_rect(0, panel_top, pw, ph - panel_top, 255); // clear panel
  r->fill_rect(0, panel_top, pw, 2, 0);                // divider above

  if (title && title[0])
  {
    r->draw_text(8, panel_top + 6, title, true, false);
  }

  int grid_top = panel_top + title_h;
  int grid_h = ph - grid_top;
  int n_rows = (int)rows.size();
  if (n_rows <= 0)
  {
    return;
  }
  int row_h = grid_h / n_rows;

  m_rects.clear();
  m_ids.clear();
  for (int ri = 0; ri < n_rows; ri++)
  {
    const std::vector<PanelBtn> &row = rows[ri];
    int n_cols = (int)row.size();
    if (n_cols <= 0)
    {
      continue;
    }
    int key_w = pw / n_cols;
    int ky = grid_top + ri * row_h;
    for (int ci = 0; ci < n_cols; ci++)
    {
      int kx = ci * key_w;
      int cw = (ci == n_cols - 1) ? (pw - kx) : key_w;
      const PanelBtn &b = row[ci];

      m_rects.push_back(Rect(kx, ky, cw, row_h));
      m_ids.push_back(b.id);

      if (!b.id.empty())
      {
        r->draw_rect(kx + 4, ky + 4, cw - 8, row_h - 8, 0);
      }
      int tw = r->get_text_width(b.label.c_str(), false, false);
      int tx = kx + (cw - tw) / 2;
      if (tx < kx + 4)
      {
        tx = kx + 4;
      }
      int ty = ky + (row_h - lh) / 2;
      if (ty < ky + 2)
      {
        ty = ky + 2;
      }
      r->draw_text(tx, ty, b.label.c_str(), false, false);
    }
  }
}

std::string ButtonPanel::hit(int x, int y) const
{
  for (size_t i = 0; i < m_rects.size(); i++)
  {
    if (!m_ids[i].empty() && m_rects[i].contains(x, y))
    {
      return m_ids[i];
    }
  }
  return "";
}

void ButtonPanel::flush(Renderer *r) const
{
  int pw = r->get_page_width();
  int ph = r->get_page_height();
  r->flush_area(r->get_margin_left(), r->get_margin_top() + m_panel_top, pw,
                ph - m_panel_top);
}
} // namespace ui
