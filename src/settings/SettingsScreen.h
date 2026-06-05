#pragma once

#include "SettingsStore.h"
#include "../ui/KeyboardView.h"
#include "../boards/battery/Battery.h"

class Renderer;

namespace settings
{
// Coordinate-driven Settings screen with three sub-tabs (Tasks / Reader / Device).
// Each editable field opens the on-screen keyboard; values persist to NVS via
// SettingsStore on "Save & Back". Device tab is read-only (battery info).
class SettingsScreen
{
public:
  SettingsScreen(Renderer *renderer, SettingsStore *store, Battery *battery = nullptr);

  void enter();                  // initial render
  void handle_tap(int x, int y); // route a logical tap
  void render();                 // (re)draw current view
  bool exit_requested() const { return m_exit; }

private:
  enum SubTab
  {
    TAB_TASKS = 0,
    TAB_READER = 1,
    TAB_DEVICE = 2
  };

  struct FieldHit
  {
    int index;  // field index within the active sub-tab, -1 if none
    int row_y;  // top y of the row (for layout reuse)
  };

  int field_count() const;
  void field_label_value(int idx, std::string &label, std::string &value,
                         bool &masked, bool &is_bool, bool &read_only) const;
  void begin_edit(int idx);
  void commit_edit();
  // Draw the "Hello" sleep screen and put the board into ship mode.
  void do_shutdown();
  void apply_buffer_to_field(int idx, const std::string &text);
  void toggle_bool_field(int idx);

  Renderer *m_renderer;
  SettingsStore *m_store;
  Battery *m_battery;
  SubTab m_tab = TAB_TASKS;

  bool m_editing = false;
  int m_edit_field = -1;
  ui::KeyboardView m_kb;

  bool m_exit = false;

  // Layout metrics recomputed each render so handle_tap can map y->row.
  int m_tabs_y = 0;
  int m_tabs_h = 0;
  int m_list_top = 0;
  int m_row_h = 0;
  int m_save_y = 0;
  int m_save_h = 0;
  int m_shutdown_y = 0;
  int m_shutdown_h = 0;
};
} // namespace settings
