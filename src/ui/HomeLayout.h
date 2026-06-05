#pragma once

// Exact-pixel home-screen geometry, shared by the renderer (handleHome in
// main.cpp) and the touch hit-test (PaperS3TouchControls) so the drawn tiles and
// the tap zones never drift. All coordinates are absolute panel pixels: the home
// screen draws with margins zeroed, so a raw touch maps 1:1.
//
// Reference panel is 540 (w) x 960 (h) logical. Matches the design mockup:
//   - 5 px left/right, 10 px top/bottom bezel dead-zone
//   - two 500 x 190 tiles, inset 20 px, 20 px gap, vertically centered between
//     the status bar and the Settings strip
//   - Settings strip pinned to the bottom dead-zone (no border)
namespace ui
{
struct HRect
{
  int x, y, w, h;
  bool contains(int px, int py) const
  {
    return px >= x && px < x + w && py >= y && py < y + h;
  }
};

struct HomeLayout
{
  HRect tasks;
  HRect books;
  HRect settings;
  int status_bar_h; // top strip reserved for the status bar
};

// Bezel dead-zone (no draw elements here). Painted glass edges are NOT
// symmetric. MUST match clear_deadzone() in EpdiyFrameBufferRenderer and the
// DEADZONE_* constants in main.cpp.
constexpr int HOME_DZ_TOP = 15;
constexpr int HOME_DZ_BOTTOM = 10;
constexpr int HOME_DZ_LEFT = 15;
constexpr int HOME_DZ_RIGHT = 12;
// Tiles.
constexpr int HOME_TILE_MARGIN_X = 20; // tile inset from panel edge
constexpr int HOME_TILE_H = 190;
constexpr int HOME_TILE_GAP = 20;
constexpr int HOME_TILE_BORDER = 3;
// Bottom Settings strip.
constexpr int HOME_SETTINGS_H = 70;

inline HomeLayout compute_home_layout(int panel_w, int panel_h, int line_height)
{
  int lh = line_height > 0 ? line_height : 20;
  int sb_h = HOME_DZ_TOP + lh + 8; // mirrors status_bar_height()

  HomeLayout L;
  L.status_bar_h = sb_h;

  int region_top = sb_h;
  int region_bottom = panel_h - HOME_DZ_BOTTOM - HOME_SETTINGS_H;
  int group_h = 2 * HOME_TILE_H + HOME_TILE_GAP;
  int tiles_top = region_top + (region_bottom - region_top - group_h) / 2;
  if (tiles_top < region_top)
  {
    tiles_top = region_top;
  }

  int tile_w = panel_w - 2 * HOME_TILE_MARGIN_X;
  L.tasks = {HOME_TILE_MARGIN_X, tiles_top, tile_w, HOME_TILE_H};
  L.books = {HOME_TILE_MARGIN_X, tiles_top + HOME_TILE_H + HOME_TILE_GAP, tile_w, HOME_TILE_H};
  L.settings = {HOME_DZ_LEFT, panel_h - HOME_DZ_BOTTOM - HOME_SETTINGS_H,
                panel_w - HOME_DZ_LEFT - HOME_DZ_RIGHT, HOME_SETTINGS_H};
  return L;
}
} // namespace ui
