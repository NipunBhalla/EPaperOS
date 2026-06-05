#include "KeyboardView.h"
#include "Renderer/Renderer.h"

namespace ui
{
void KeyboardView::open(const std::string &initial, bool masked, const std::string &title)
{
  m_title = title;
  m_model.begin(initial, masked);
}

// Draw the title, edit line, and key grid. Computes m_kb_area for hit-testing.
void KeyboardView::draw_panel(Renderer *r)
{
  int pw = r->get_page_width();
  int ph = r->get_page_height();
  int lh = r->get_line_height();
  if (lh <= 0)
  {
    lh = 24;
  }

  // Panel occupies the bottom ~60% of the screen: title, edit line, then keys.
  int title_h = lh + 8;
  int edit_h = lh + 16;
  int rows = static_cast<int>(m_model.layout().size());
  if (rows <= 0)
  {
    rows = 1;
  }
  // Keyboard grid gets a fixed slice of the screen height.
  int grid_h = (ph * 45) / 100;
  int panel_top = ph - (title_h + edit_h + grid_h);
  if (panel_top < 0)
  {
    panel_top = 0;
  }
  m_panel_top = panel_top;

  // Clear the whole panel to white.
  r->fill_rect(0, panel_top, pw, ph - panel_top, 255);

  // Title.
  if (!m_title.empty())
  {
    r->draw_text(8, panel_top + 4, m_title.c_str(), true, false);
  }

  // Edit line box with the current (masked-aware) text.
  int edit_y = panel_top + title_h;
  r->draw_rect(6, edit_y, pw - 12, edit_h, 0);
  std::string shown = m_model.display();
  shown += "_"; // simple caret
  r->draw_text(12, edit_y + 6, shown.c_str(), false, false);

  // Key grid.
  int grid_top = edit_y + edit_h + 4;
  m_kb_area = Rect(0, grid_top, pw, ph - grid_top);

  const auto &layout = m_model.layout();
  int n_rows = static_cast<int>(layout.size());
  if (n_rows <= 0)
  {
    return;
  }
  int row_h = m_kb_area.h / n_rows;
  for (int ri = 0; ri < n_rows; ++ri)
  {
    const auto &row = layout[ri];
    int n_cols = static_cast<int>(row.size());
    if (n_cols <= 0)
    {
      continue;
    }
    int key_w = m_kb_area.w / n_cols;
    int ky = m_kb_area.y + ri * row_h;
    for (int ci = 0; ci < n_cols; ++ci)
    {
      int kx = m_kb_area.x + ci * key_w;
      r->draw_rect(kx + 1, ky + 1, key_w - 2, row_h - 2, 0);

      const Key &k = row[ci];
      char buf[2] = {0, 0};
      const char *label = nullptr;
      if (k.type == KeyType::CHAR)
      {
        buf[0] = k.ch;
        label = buf;
      }
      else
      {
        label = k.label ? k.label : "";
      }
      // Roughly center the label.
      int tw = r->get_text_width(label, false, false);
      int tx = kx + (key_w - tw) / 2;
      if (tx < kx + 2)
      {
        tx = kx + 2;
      }
      int ty = ky + (row_h - r->get_line_height()) / 2;
      if (ty < ky + 1)
      {
        ty = ky + 1;
      }
      r->draw_text(tx, ty, label, false, false);
    }
  }
}

void KeyboardView::render(Renderer *r)
{
  if (!r)
  {
    return;
  }
  draw_panel(r);
}

void KeyboardView::handle_tap(Renderer *r, int x, int y)
{
  if (!r)
  {
    return;
  }
  Key k = m_model.key_at(m_kb_area, x, y);
  if (k.type == KeyType::NONE)
  {
    return;
  }
  m_model.press(k);
  if (m_model.done() || m_model.cancelled())
  {
    return; // host will tear the keyboard down
  }
  // Repaint the panel and fast-refresh just that region. flush_area wants
  // ABSOLUTE pixels (it does not add margins like draw_*), so offset by the
  // current margins -- Settings now reserves a status-bar + dead-zone margin.
  draw_panel(r);
  int ph = r->get_page_height();
  int pw = r->get_page_width();
  r->flush_area(r->get_margin_left(), r->get_margin_top() + m_panel_top, pw, ph - m_panel_top);
}
} // namespace ui
