#pragma once

#include <string>
#include "Keyboard.h"

class Renderer;

namespace ui
{
// Device-side view for the pure KeyboardModel. Draws the title, edit line, and
// key grid, and hit-tests raw taps onto keys. Modal: a host screen open()s it,
// forwards taps via handle_tap(), and polls done()/cancelled() then reads
// buffer(). Layout matches KeyboardModel::key_at exactly (equal-height rows,
// equal-width keys within a row) so taps land on the drawn keys.
class KeyboardView
{
public:
  void open(const std::string &initial, bool masked, const std::string &title);

  // Full draw of the keyboard panel into the framebuffer (caller flushes).
  void render(Renderer *r);

  // Hit-test (x,y) -> key -> press; repaint + partial-flush the panel. No-op on
  // a miss. (x,y) are logical-page coordinates.
  void handle_tap(Renderer *r, int x, int y);

  bool done() const { return m_model.done(); }
  bool cancelled() const { return m_model.cancelled(); }
  const std::string &buffer() const { return m_model.buffer(); }

private:
  void draw_panel(Renderer *r);

  KeyboardModel m_model;
  std::string m_title;
  Rect m_kb_area;   // keyboard grid region (set in draw_panel, used by hit-test)
  int m_panel_top = 0; // top of the whole panel (title) for partial flush
};
} // namespace ui
