#pragma once

namespace ui
{
// Axis-aligned rectangle in logical (portrait) page coordinates.
// A constructor (rather than default member initializers) keeps brace
// initialization working under -std=c++11, which the native test env uses.
struct Rect
{
  int x;
  int y;
  int w;
  int h;

  Rect(int x_ = 0, int y_ = 0, int w_ = 0, int h_ = 0)
      : x(x_), y(y_), w(w_), h(h_) {}

  bool contains(int px, int py) const
  {
    return px >= x && px < x + w && py >= y && py < y + h;
  }
};

// Map a point to a cell index in a cols x rows grid laid over `area`, in
// row-major order (index = row * cols + col). Returns -1 when the point is
// outside `area` or the grid is degenerate.
int grid_cell(const Rect &area, int cols, int rows, int px, int py);
} // namespace ui
