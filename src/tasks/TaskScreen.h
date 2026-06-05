#pragma once

#include <string>
#include <vector>

class Renderer;
class TaskModel;

// Renders the task list using the existing Renderer (font + draw primitives).
//
// Layout:
//   - scrollable task list (rows 0..N-1) fills the area above a fixed bar
//   - a fixed button bar pinned to the bottom of the screen, three buttons:
//       [ Exit ]  [ + Add Task ]  [ Sync ]
//     Exit = push + back to home; Add Task = keyboard; Sync = push + pull, stay.
// Cursor 0..N-1 = task; N=add, N+1=sync, N+2=exit (the bar buttons), so the
// hardware UP/DOWN/SELECT buttons can reach the bar too. A single-line toggle
// uses partial refresh (flush_area) so flipping a checkbox does not full-flash.
class TaskScreen
{
private:
  Renderer *renderer;
  TaskModel &model;

  int m_cursor = 0;      // 0..N-1 => task index, N=add, N+1=sync, N+2=save&sync
  int m_top = 0;         // first visible task index (scroll window)
  int m_drawn_cursor = -1; // task row currently showing the cursor box (-1=none)
  bool m_needs_full = true;

  int top_offset() const;          // y of first row (below battery area)
  int line_height() const;         // renderer font line height
  int single_row_height() const;   // one-line row (add / sync): line_height + padding
  int row_pixel_height(int row) const; // variable: task rows grow with wrapped lines
  int total_rows() const;          // tasks + 3 bar buttons
  int bar_height() const;          // height of the fixed bottom button bar
  int bar_icon_width() const;      // width of the narrow calendar-icon cell
  int list_bottom() const;         // renderer-y where the list ends / bar starts

  int section_col_width() const;   // reserved left column for the P1/P2/P3 tag
  int text_avail_width() const;    // pixel width available for wrapped task text
  // Break a UTF-8 string into lines that each fit within max_w pixels. Splits on
  // spaces; hard-breaks a single word longer than the line.
  std::vector<std::string> wrap_text(const std::string &s, int max_w) const;

  // Scroll m_top so the focused row is fully on screen.
  void ensure_visible();

  void draw_row(int row, int y, int rh, bool focused);
  // Redraw one task row in place and partial-flush just its region. Returns
  // false if the row is outside the current scroll window (nothing drawn).
  bool draw_flush_row(int row, bool focused);
  void draw_checkbox(int x, int y, int size, bool done);
  void draw_calendar_icon(int x, int y, int s); // vector glyph for the Cal button
  void draw_bar(); // the fixed bottom button bar (Exit / Add / Sync / Cal)

public:
  TaskScreen(Renderer *renderer, TaskModel &model) : renderer(renderer), model(model) {}

  void set_needs_redraw() { m_needs_full = true; }
  void reset_cursor() { m_cursor = 0; m_top = 0; m_drawn_cursor = -1; }

  void next();
  void prev();
  void scroll_up();   // move viewport up one page, no cursor change
  void scroll_down(); // move viewport down one page, no cursor change

  bool on_add_row() const;       // "+ Add Task" button
  bool on_sync_row() const;      // "Sync" button (sync only, stay)
  bool on_save_row() const;      // "Exit" button (push + back home)
  bool on_filter_row() const;    // calendar filter button
  int selected_task() const { return m_cursor; }      // valid when a task row is focused
  void set_cursor(int row);                           // clamp + scroll to row

  // Map an absolute logical-page y to a task row (0..N-1), or -1 if the tap is
  // outside the list region (e.g. in the bottom button bar).
  int row_at_abs_y(int abs_y) const;

  // Map an absolute tap inside the bottom button bar to its logical button row
  // (N=add, N+1=sync, N+2=exit, N+3=calendar), or -1 if the tap is above the bar.
  int button_at_abs(int abs_x, int abs_y) const;

  // Hit-test the invisible scroll strip: the right quarter of the LIST region
  // only (never the status bar above or the button bar below). Returns 1=scroll
  // up, 2=scroll down, 0=not a scroll tap.
  int scroll_hit(int abs_x, int abs_y) const;

  // Full / paged render. screensaver=true omits the cursor box and the bottom
  // button bar so the sleep face shows only the task list (full height).
  void render(bool screensaver = false);

  // Toggle the focused task's checkbox visual via partial refresh.
  void render_toggle();

  // Centered status message (Connecting / Syncing / errors). Full refresh.
  void show_message(const char *msg);
};
