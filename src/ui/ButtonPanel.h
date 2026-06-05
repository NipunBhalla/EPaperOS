#pragma once

#include <string>
#include <vector>
#include "HitTest.h"

class Renderer;

namespace ui
{
// One drawn button: a label and a logical id used by hit-testing. An empty id
// marks a non-interactive display cell (e.g. a value readout).
struct PanelBtn
{
  std::string label;
  std::string id;
};

// A modal-style button grid pinned to the bottom of the screen: a title row
// plus equal-height button rows (equal-width cells within a row). Draws into
// the framebuffer and records cell rects so taps map back to button ids.
// Shared by the add-task wizard and the calendar filter picker.
//
// Coordinates: drawn and hit-tested in CONTENT space (the touch driver already
// removes margins), matching draw_*; only flush() re-adds margins for pixels.
class ButtonPanel
{
public:
  // Draw the panel. `top_pct` = panel height as a percentage of screen height
  // measured from the bottom (e.g. 38 => bottom 62% blank, top of panel at
  // 38% down... actually panel_top = ph*top_pct/100, so larger => shorter).
  void draw(Renderer *r, const char *title,
            const std::vector<std::vector<PanelBtn>> &rows, int top_pct = 38);

  // Button id under (x,y), or "" on a miss / display-only cell.
  std::string hit(int x, int y) const;

  // Partial-refresh just the panel region (absolute pixels).
  void flush(Renderer *r) const;

  int panel_top() const { return m_panel_top; }

private:
  std::vector<Rect> m_rects;
  std::vector<std::string> m_ids;
  int m_panel_top = 0;
};
} // namespace ui
