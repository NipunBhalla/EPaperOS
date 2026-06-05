#include "TaskScreen.h"
#include "TaskModel.h"
#include <Renderer/Renderer.h>

#define ROW_PADDING 14
#define LEFT_PAD 8
#define CHECKBOX_GAP 12
#define BAR_GAP 6 // inset between bar buttons and around their borders

// y of the first row, in renderer (margin-relative) coords. 0 sits just below
// the battery area because draw_* already add margin_top.
int TaskScreen::top_offset() const
{
  return 6;
}

int TaskScreen::line_height() const
{
  return renderer->get_line_height();
}

int TaskScreen::single_row_height() const
{
  return line_height() + ROW_PADDING;
}

// Fixed button bar pinned to the bottom of the screen.
int TaskScreen::bar_height() const
{
  return single_row_height() + 14; // a little taller for finger-friendly taps
}

// Renderer-y where the task list ends and the button bar begins.
int TaskScreen::list_bottom() const
{
  return renderer->get_page_height() - bar_height();
}

// Fixed-width column reserved on the left for the "P1:"/"P2:"/"P3:" priority
// indicator (sized to the widest label so checkboxes stay aligned).
int TaskScreen::section_col_width() const
{
  return renderer->get_text_width("P3:", true, false) + 10;
}

// Pixel width left for the task label after the left pad + section column +
// checkbox + gap, with an equal pad reserved on the right edge.
int TaskScreen::text_avail_width() const
{
  int w = renderer->get_page_width() -
          (LEFT_PAD + section_col_width() + line_height() + CHECKBOX_GAP) - LEFT_PAD;
  return w < 1 ? 1 : w;
}

std::vector<std::string> TaskScreen::wrap_text(const std::string &s, int max_w) const
{
  std::vector<std::string> lines;
  if (max_w <= 0)
  {
    lines.push_back(s);
    return lines;
  }

  std::string cur;
  size_t i = 0, n = s.size();
  while (i < n)
  {
    // next whitespace-delimited word
    size_t j = i;
    while (j < n && s[j] != ' ')
    {
      j++;
    }
    std::string word = s.substr(i, j - i);
    size_t k = j;
    while (k < n && s[k] == ' ')
    {
      k++;
    }

    std::string trial = cur.empty() ? word : cur + " " + word;
    if (renderer->get_text_width(trial.c_str(), false, false) <= max_w)
    {
      cur = trial;
    }
    else
    {
      if (!cur.empty())
      {
        lines.push_back(cur);
        cur.clear();
      }
      if (renderer->get_text_width(word.c_str(), false, false) <= max_w)
      {
        cur = word;
      }
      else
      {
        // single word too long for a line: hard-break by characters
        std::string chunk;
        for (size_t c = 0; c < word.size(); ++c)
        {
          std::string t2 = chunk + word[c];
          if (renderer->get_text_width(t2.c_str(), false, false) <= max_w)
          {
            chunk = t2;
          }
          else
          {
            if (!chunk.empty())
            {
              lines.push_back(chunk);
            }
            chunk = std::string(1, word[c]);
          }
        }
        cur = chunk;
      }
    }
    i = k;
  }
  if (!cur.empty() || lines.empty())
  {
    lines.push_back(cur);
  }
  return lines;
}

int TaskScreen::row_pixel_height(int row) const
{
  if (row >= (int)model.size())
  {
    return single_row_height(); // add / sync / save action rows
  }
  int n = (int)wrap_text(model.display_text(row), text_avail_width()).size();
  if (n < 1)
  {
    n = 1;
  }
  return n * line_height() + ROW_PADDING;
}

int TaskScreen::total_rows() const
{
  return (int)model.size() + 4; // tasks + add + sync + exit + calendar
}

bool TaskScreen::on_add_row() const
{
  return m_cursor == (int)model.size();
}

bool TaskScreen::on_sync_row() const
{
  return m_cursor == (int)model.size() + 1;
}

bool TaskScreen::on_save_row() const
{
  return m_cursor == (int)model.size() + 2;
}

bool TaskScreen::on_filter_row() const
{
  return m_cursor == (int)model.size() + 3;
}

// Scroll the window (m_top) so the focused row fits on screen. Scrolls up if the
// cursor is above the window, or down until [m_top..cursor] fits the usable height.
void TaskScreen::ensure_visible()
{
  if (m_cursor >= (int)model.size())
  {
    return; // a bar button is focused; the list scroll window is unaffected
  }
  if (m_cursor < m_top)
  {
    m_top = m_cursor;
    return;
  }
  int usable = list_bottom() - top_offset();
  while (m_top < m_cursor)
  {
    int sum = 0;
    for (int r = m_top; r <= m_cursor; r++)
    {
      sum += row_pixel_height(r);
    }
    if (sum <= usable)
    {
      break;
    }
    m_top++;
  }
}

void TaskScreen::set_cursor(int row)
{
  int total = total_rows();
  if (row < 0)
  {
    row = 0;
  }
  if (row >= total)
  {
    row = total - 1;
  }
  m_cursor = row;
  ensure_visible();
}

// Touch delivers absolute logical-page coordinates; draw_* are margin-relative,
// already in CONTENT space (the touch driver subtracts the margins), the same
// space draw_* uses, so map directly with no further margin math.
int TaskScreen::row_at_abs_y(int abs_y) const
{
  int rel = abs_y - top_offset();
  if (rel < 0)
  {
    return -1;
  }
  // Taps in the bar are handled by button_at_abs(); stop at the list bottom.
  if (abs_y >= list_bottom())
  {
    return -1;
  }
  int n = (int)model.size();
  int y = 0;
  for (int row = m_top; row < n; row++)
  {
    int h = row_pixel_height(row);
    if (rel < y + h)
    {
      return row;
    }
    y += h;
  }
  return -1;
}

// Width of the narrow right-hand calendar-icon cell in the button bar.
int TaskScreen::bar_icon_width() const
{
  return bar_height();
}

// Hit-test the bottom button bar: three text buttons [Exit][+Add][Sync] sharing
// the left, then a narrow calendar-icon cell on the right.
int TaskScreen::button_at_abs(int abs_x, int abs_y) const
{
  // abs_x/abs_y are CONTENT-space taps (margins already removed by the driver).
  if (abs_y < list_bottom())
  {
    return -1; // above the bar
  }
  int w = renderer->get_page_width();
  int n = (int)model.size();
  int text_w = w - bar_icon_width();
  int rel_x = abs_x;
  if (rel_x < 0)
  {
    rel_x = 0;
  }
  if (rel_x >= text_w)
  {
    return n + 3; // calendar icon cell
  }
  int idx = rel_x * 3 / text_w;
  if (idx > 2)
  {
    idx = 2;
  }
  if (idx == 0)
  {
    return n + 2; // Exit
  }
  if (idx == 1)
  {
    return n; // Add Task
  }
  return n + 1; // Sync
}

// Right-quarter scroll strip, clamped to the list region so it never overlaps
// the status bar (above top_offset) or the button bar (at/below list_bottom).
int TaskScreen::scroll_hit(int abs_x, int abs_y) const
{
  int w = renderer->get_page_width();
  if (abs_x < w * 3 / 4)
  {
    return 0;
  }
  int top = top_offset();
  int bot = list_bottom();
  if (abs_y < top || abs_y >= bot)
  {
    return 0; // outside the list region
  }
  int mid = (top + bot) / 2;
  return (abs_y < mid) ? 1 : 2; // 1 = up, 2 = down
}

void TaskScreen::next()
{
  int total = total_rows();
  m_cursor = (m_cursor + 1) % total;
  ensure_visible();
}

void TaskScreen::prev()
{
  int total = total_rows();
  m_cursor = (m_cursor - 1 + total) % total;
  ensure_visible();
}

void TaskScreen::scroll_down()
{
  int usable = list_bottom() - top_offset();
  int n = (int)model.size();
  int sum = 0;
  int new_top = m_top;
  for (int r = m_top; r < n; r++)
  {
    sum += row_pixel_height(r);
    if (sum > usable)
      break;
    new_top = r + 1;
  }
  if (new_top >= n)
    new_top = (n > 0) ? n - 1 : 0;
  if (new_top != m_top)
  {
    m_top = new_top;
    m_needs_full = true;
  }
}

void TaskScreen::scroll_up()
{
  int usable = list_bottom() - top_offset();
  int sum = 0;
  int new_top = m_top;
  for (int r = m_top - 1; r >= 0; r--)
  {
    sum += row_pixel_height(r);
    if (sum > usable)
      break;
    new_top = r;
  }
  if (new_top < 0)
    new_top = 0;
  if (new_top != m_top)
  {
    m_top = new_top;
    m_needs_full = true;
  }
}

void TaskScreen::draw_checkbox(int x, int y, int s, bool done)
{
  renderer->draw_rect(x, y, s, s, 0);
  renderer->draw_rect(x + 1, y + 1, s - 2, s - 2, 0);
  if (done)
  {
    renderer->fill_rect(x + 4, y + 4, s - 8, s - 8, 0);
  }
}

// Draw one task row (checkbox + wrapped text) at renderer-y `y`, occupying `rh`
// pixels. Action buttons live in the fixed bottom bar, not here.
void TaskScreen::draw_row(int row, int y, int rh, bool focused)
{
  int w = renderer->get_page_width();
  // erase the row background (clears any stale cursor box / old text)
  renderer->fill_rect(0, y, w, rh, 255);

  int text_y = y + ROW_PADDING / 2;
  int task_i = row;
  // Size the checkbox to the glyph pixel height (not line_height, which
  // includes line-gap and floats the box above the text) and vertically
  // center it on the first text line.
  int lh = line_height();
  int px = renderer->get_reading_font_pixel_height();
  int cb = (px > 0 && px <= lh) ? px : lh;
  int box_y = text_y + (lh - cb) / 2;
  // Priority indicator ("P1:"/"P2:"/"P3:") in the reserved left column.
  int sec_w = section_col_width();
  const std::string &sec = model.section_label(task_i);
  if (!sec.empty())
  {
    renderer->draw_text(LEFT_PAD, text_y, (sec + ":").c_str(), true, false);
  }
  int box_x = LEFT_PAD + sec_w;
  draw_checkbox(box_x, box_y, cb, model.is_done(task_i));
  int text_x = box_x + lh + CHECKBOX_GAP;
  std::vector<std::string> lines = wrap_text(model.display_text(task_i), text_avail_width());
  for (size_t i = 0; i < lines.size(); i++)
  {
    renderer->draw_text(text_x, text_y + (int)i * line_height(), lines[i].c_str(), false, false);
  }

  // cursor box around the focused row
  if (focused)
  {
    for (int i = 0; i < 3; i++)
    {
      renderer->draw_rect(i, y + i, w - 2 * i, rh - 2 * i, 0);
    }
  }
}

// Draw the fixed bottom button bar: three bordered buttons, left-to-right
// [ Exit ] [ + Add Task ] [ Sync ]. The focused button (cursor on a bar row)
// gets a thicker border. Labels are centered in each cell.
void TaskScreen::draw_bar()
{
  int w = renderer->get_page_width();
  int by = list_bottom();
  int bh = bar_height();

  renderer->fill_rect(0, by, w, bh, 255); // clear bar area
  renderer->fill_rect(0, by, w, 2, 0);    // divider line above the bar

  int n = (int)model.size();
  const char *labels[3] = {"Exit", "+Add", "Sync"};
  int rows[3] = {n + 2, n, n + 1};
  int icon_w = bar_icon_width();
  int text_w = w - icon_w; // three text buttons share this
  int tcell = text_w / 3;

  // Three bordered text buttons on the left.
  for (int i = 0; i < 3; i++)
  {
    int x = i * tcell;
    int cw = (i == 2) ? (text_w - x) : tcell; // soak up rounding before the icon
    int ix = x + BAR_GAP;
    int iy = by + BAR_GAP + 2; // +2 to clear the divider line
    int iw = cw - 2 * BAR_GAP;
    int ih = bh - 2 * BAR_GAP - 2;

    bool focused = (m_cursor == rows[i]);
    int thick = focused ? 3 : 1;
    for (int t = 0; t < thick; t++)
    {
      renderer->draw_rect(ix + t, iy + t, iw - 2 * t, ih - 2 * t, 0);
    }
    int tw = renderer->get_text_width(labels[i], true, false);
    int tx = ix + (iw - tw) / 2;
    if (tx < ix)
    {
      tx = ix;
    }
    int ty = iy + (ih - line_height()) / 2;
    renderer->draw_text(tx, ty, labels[i], true, false);
  }

  // Calendar filter: borderless icon in the narrow right cell. A thin box is
  // drawn only when it is the focused (button-nav) target.
  bool cal_focused = (m_cursor == n + 3);
  int cx = text_w, cy = by;
  int s = (icon_w < bh ? icon_w : bh) - 14;
  if (s < 12)
  {
    s = 12;
  }
  draw_calendar_icon(cx + (icon_w - s) / 2, cy + (bh - s) / 2, s);
  if (cal_focused)
  {
    renderer->draw_rect(cx + 3, cy + BAR_GAP, icon_w - 6, bh - 2 * BAR_GAP, 0);
  }
}

// A small calendar glyph in an s x s box at (x,y): two top tabs, a body with a
// dark title band, and a 3x2 grid of date dots. Vector-drawn so it renders on
// any font.
void TaskScreen::draw_calendar_icon(int x, int y, int s)
{
  int tab_w = (s / 8 < 2) ? 2 : s / 8;
  int tab_h = (s / 5 < 3) ? 3 : s / 5;
  // Two tabs poking up from the top edge.
  renderer->fill_rect(x + s / 4 - tab_w / 2, y, tab_w, tab_h, 0);
  renderer->fill_rect(x + 3 * s / 4 - tab_w / 2, y, tab_w, tab_h, 0);

  int by = y + tab_h / 2; // body sits below the tab midpoint
  int bh = s - tab_h / 2;
  // Body border (2px) + dark title band.
  renderer->draw_rect(x, by, s, bh, 0);
  renderer->draw_rect(x + 1, by + 1, s - 2, bh - 2, 0);
  int band_h = (bh / 4 < 3) ? 3 : bh / 4;
  renderer->fill_rect(x, by, s, band_h, 0);

  // 3x2 grid of date dots in the body below the band.
  int dot = (s / 9 < 2) ? 2 : s / 9;
  int gx = x + s / 6;
  int gy = by + band_h + (bh - band_h - dot) / 4;
  int step_x = (s - 2 * (s / 6) - dot) / 2; // 3 columns
  int step_y = (bh - band_h - dot) / 2;     // 2 rows (with a little top inset)
  for (int r = 0; r < 2; r++)
  {
    for (int c = 0; c < 3; c++)
    {
      renderer->fill_rect(gx + c * step_x, gy + r * step_y, dot, dot, 0);
    }
  }
}

void TaskScreen::render(bool screensaver)
{
  if (m_needs_full)
  {
    renderer->clear_screen();
    m_needs_full = false;
  }

  // The list only ever draws task rows; the action buttons live in the fixed
  // bottom bar. Screensaver paints the list full-height with no bar/cursor.
  int n = (int)model.size();
  int page_h = renderer->get_page_height();
  int region_bottom = screensaver ? page_h : list_bottom();
  int y = top_offset();
  for (int row = m_top; row < n; row++)
  {
    int h = row_pixel_height(row);
    // Only draw a row that fits whole, so wrapped lines never spill past the
    // list region. ensure_visible() guarantees the focused row fits the window.
    // Always draw at least the first row so a single over-tall task still shows.
    if (row != m_top && y + h > region_bottom)
    {
      break;
    }
    draw_row(row, y, h, screensaver ? false : (row == m_cursor));
    y += h;
  }

  // Clear the rest of the list region so stale content from a previously-taller
  // row (different scroll position) doesn't persist in the framebuffer.
  if (y < region_bottom)
  {
    renderer->fill_rect(0, y, renderer->get_page_width(), region_bottom - y, 255);
  }

  if (model.empty())
  {
    renderer->draw_text(LEFT_PAD, top_offset() + ROW_PADDING, "(no tasks - tap Sync to fetch)", false, true);
  }

  if (!screensaver)
  {
    draw_bar();
  }

  // Record which row carries the cursor box so render_toggle() can erase it
  // later (screensaver draws no cursor).
  m_drawn_cursor = screensaver ? -1 : (m_cursor < (int)model.size() ? m_cursor : -1);

  renderer->flush_display();
}

// Redraw one task row in place + partial-flush its region. Returns false (no
// draw) when the row is off the current scroll window.
bool TaskScreen::draw_flush_row(int row, bool focused)
{
  if (row < m_top || row >= (int)model.size())
  {
    return false;
  }
  int y = top_offset();
  for (int r = m_top; r < row; r++)
  {
    y += row_pixel_height(r);
  }
  int rh = row_pixel_height(row);
  if (y + rh > renderer->get_page_height())
  {
    return false; // not fully visible
  }
  draw_row(row, y, rh, focused);
  // flush_area wants absolute pixels (it does NOT add margins like draw_*)
  renderer->flush_area(renderer->get_margin_left(), y + renderer->get_margin_top(),
                       renderer->get_page_width(), rh);
  return true;
}

// Repaint the focused row (and erase the previously-focused row's box) with
// partial refresh (no full flash).
void TaskScreen::render_toggle()
{
  if (m_cursor >= (int)model.size())
  {
    return; // action row, not a task - nothing to toggle
  }
  // Erase the stale cursor box left on the previously-focused row.
  if (m_drawn_cursor >= 0 && m_drawn_cursor != m_cursor)
  {
    draw_flush_row(m_drawn_cursor, false); // off-window -> nothing to erase
  }
  // Paint the now-focused row (also reflects the toggled checkbox).
  if (!draw_flush_row(m_cursor, true))
  {
    render(); // focused row not fully visible - full repaint
    return;
  }
  m_drawn_cursor = m_cursor;
}

void TaskScreen::show_message(const char *msg)
{
  renderer->clear_screen();
  int y = renderer->get_page_height() / 2 - line_height();
  renderer->draw_text_box(msg, 0, y, renderer->get_page_width(), line_height() * 2, true, false);
  renderer->flush_display();
  m_needs_full = true; // next list render must repaint fully
}
