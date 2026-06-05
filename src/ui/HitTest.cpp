#include "HitTest.h"

namespace ui
{
int grid_cell(const Rect &area, int cols, int rows, int px, int py)
{
  if (cols <= 0 || rows <= 0)
  {
    return -1;
  }
  if (!area.contains(px, py))
  {
    return -1;
  }
  int cw = area.w / cols;
  int ch = area.h / rows;
  if (cw <= 0 || ch <= 0)
  {
    return -1;
  }
  int col = (px - area.x) / cw;
  int row = (py - area.y) / ch;
  // Clamp the far edge so points in the integer-division remainder still land
  // on the last cell rather than overflowing.
  if (col >= cols)
  {
    col = cols - 1;
  }
  if (row >= rows)
  {
    row = rows - 1;
  }
  return row * cols + col;
}
} // namespace ui
