#include "SettingsScreen.h"
#include "Renderer/Renderer.h"
#include "../rtc/TimeSync.h"
#include "../ui/HomeLayout.h"

// Status-bar visibility + scaling height live in main.cpp; the main loop paints
// the status bar over every screen, so Settings must reserve room for it.
extern bool status_bar_visible;

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

namespace
{
// Number of fixed (read-only) battery rows on the Device tab, before the
// Date / Time / NTP rows are appended.
int device_battery_rows(bool has_fuel_gauge) { return has_fuel_gauge ? 10 : 3; }
} // namespace

namespace settings
{
SettingsScreen::SettingsScreen(Renderer *renderer, SettingsStore *store, Battery *battery)
    : m_renderer(renderer), m_store(store), m_battery(battery) {}

void SettingsScreen::enter()
{
  m_exit = false;
  m_editing = false;
  m_edit_field = -1;
  render();
}

int SettingsScreen::field_count() const
{
  if (m_tab == TAB_TASKS) return 5;
  if (m_tab == TAB_READER) return 3;
  // TAB_DEVICE: battery rows (3 or 10) + Date + Time + NTP Sync.
  return device_battery_rows(m_battery && m_battery->has_fuel_gauge()) + 3;
}

// Describe a field of the active sub-tab.
void SettingsScreen::field_label_value(int idx, std::string &label, std::string &value,
                                       bool &masked, bool &is_bool, bool &read_only) const
{
  const AppConfig &c = m_store->cfg();
  masked = false;
  is_bool = false;
  read_only = false;
  if (m_tab == TAB_TASKS)
  {
    switch (idx)
    {
    case 0: label = "WiFi SSID"; value = c.wifi_ssid; break;
    case 1: label = "WiFi Pass"; value = c.wifi_pass; masked = true; break;
    case 2: label = "Obsidian URL"; value = c.obsidian_url; break;
    case 3: label = "API Token"; value = c.obsidian_token; masked = true; break;
    case 4: label = "Note Path"; value = c.note_path; break;
    default: label = ""; value = ""; break;
    }
  }
  else if (m_tab == TAB_READER)
  {
    switch (idx)
    {
    case 0: label = "UI font px"; value = std::to_string(c.reader_font_px); break;
    case 1: label = "Use TTF"; value = c.reader_use_ttf ? "Yes" : "No"; is_bool = true; break;
    case 2: label = "Margin"; value = std::to_string(c.reader_margin); break;
    default: label = ""; value = ""; break;
    }
  }
  else // TAB_DEVICE
  {
    read_only = true;
    int base = device_battery_rows(m_battery && m_battery->has_fuel_gauge());
    if (idx >= base)
    {
      // Appended editable rows: Date, Time, NTP Sync.
      read_only = false;
      time_t now = time(nullptr);
      struct tm lt;
      localtime_r(&now, &lt);
      char ymd[40];
      char hm[24];
      snprintf(ymd, sizeof(ymd), "%04d-%02d-%02d", lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);
      snprintf(hm, sizeof(hm), "%02d:%02d", lt.tm_hour, lt.tm_min);
      switch (idx - base)
      {
      case 0: label = "Date"; value = ymd; break;
      case 1: label = "Time"; value = hm; break;
      default: label = "NTP Sync"; value = "tap to run"; break;
      }
      return;
    }
    char buf[32];
    float voltage = m_battery ? m_battery->get_voltage() / 1000.0f : 0.0f;
    int pct       = m_battery ? m_battery->get_percentage() : 0;
    switch (idx)
    {
    case 0:
      label = "Battery";
      snprintf(buf, sizeof(buf), "%d%%", pct);
      value = buf;
      break;
    case 1:
      label = "Voltage";
      snprintf(buf, sizeof(buf), "%.2fV", voltage);
      value = buf;
      break;
    case 2:
      label = "Charging";
      value = m_battery ? m_battery->get_charging_status() : "N/A";
      break;
    // Fields below only shown when fuel gauge (BQ27220) present
    case 3:
      label = "Current";
      snprintf(buf, sizeof(buf), "%dmA", m_battery ? m_battery->get_current_ma() : 0);
      value = buf;
      break;
    case 4:
      label = "Remain";
      snprintf(buf, sizeof(buf), "%dmAh", m_battery ? m_battery->get_remaining_mah() : 0);
      value = buf;
      break;
    case 5:
      label = "Full cap";
      snprintf(buf, sizeof(buf), "%dmAh", m_battery ? m_battery->get_full_capacity_mah() : 0);
      value = buf;
      break;
    case 6:
      label = "Health";
      snprintf(buf, sizeof(buf), "%d%%", m_battery ? m_battery->get_health_percent() : 0);
      value = buf;
      break;
    case 7:
      label = "Temp";
      snprintf(buf, sizeof(buf), "%dC", m_battery ? m_battery->get_temperature_celsius() : 0);
      value = buf;
      break;
    case 8:
      label = "VBUS";
      snprintf(buf, sizeof(buf), "%.2fV  %s",
               m_battery ? m_battery->get_vbus_voltage() : 0.0f,
               m_battery ? m_battery->get_vbus_status() : "N/A");
      value = buf;
      break;
    case 9:
      label = "CHG curr";
      snprintf(buf, sizeof(buf), "%.0fmA  NTC:%s",
               m_battery ? m_battery->get_charge_current_ma() : 0.0f,
               m_battery ? m_battery->get_ntc_status() : "N/A");
      value = buf;
      break;
    default: label = ""; value = ""; break;
    }
  }
}

void SettingsScreen::apply_buffer_to_field(int idx, const std::string &text)
{
  if (m_tab == TAB_DEVICE)
  {
    // Only the appended Date/Time rows are writable; parse and push to RTC.
    int base = device_battery_rows(m_battery && m_battery->has_fuel_gauge());
    int sub = idx - base;
    if (sub != 0 && sub != 1)
    {
      return;
    }
    time_t now = time(nullptr);
    struct tm lt;
    localtime_r(&now, &lt);
    if (sub == 0)
    {
      int y = 0, mo = 0, d = 0;
      if (sscanf(text.c_str(), "%d-%d-%d", &y, &mo, &d) == 3 && y >= 2000 && mo >= 1 && mo <= 12 && d >= 1 && d <= 31)
      {
        lt.tm_year = y - 1900;
        lt.tm_mon = mo - 1;
        lt.tm_mday = d;
        timesync::set_local_datetime(&lt);
      }
    }
    else // sub == 1: time HH:MM
    {
      int h = -1, mi = -1;
      if (sscanf(text.c_str(), "%d:%d", &h, &mi) == 2 && h >= 0 && h <= 23 && mi >= 0 && mi <= 59)
      {
        lt.tm_hour = h;
        lt.tm_min = mi;
        lt.tm_sec = 0;
        timesync::set_local_datetime(&lt);
      }
    }
    return;
  }
  AppConfig &c = m_store->cfg();
  if (m_tab == TAB_TASKS)
  {
    switch (idx)
    {
    case 0: c.wifi_ssid = text; break;
    case 1: c.wifi_pass = text; break;
    case 2: c.obsidian_url = text; break;
    case 3: c.obsidian_token = text; break;
    case 4: c.note_path = text; break;
    }
  }
  else
  {
    switch (idx)
    {
    case 0:
    {
      int v = std::atoi(text.c_str());
      if (v >= 8 && v <= 96)
      {
        c.reader_font_px = v;
      }
      break;
    }
    case 2:
    {
      int v = std::atoi(text.c_str());
      if (v >= 0 && v <= 100)
      {
        c.reader_margin = v;
      }
      break;
    }
    }
  }
}

void SettingsScreen::toggle_bool_field(int idx)
{
  AppConfig &c = m_store->cfg();
  if (m_tab == TAB_READER && idx == 1)
  {
    c.reader_use_ttf = !c.reader_use_ttf;
    m_store->save();
  }
}

void SettingsScreen::begin_edit(int idx)
{
  // Device tab: the "NTP Sync" row is an action, not a text field.
  if (m_tab == TAB_DEVICE)
  {
    int base = device_battery_rows(m_battery && m_battery->has_fuel_gauge());
    if (idx == base + 2)
    {
#ifdef EPAPEROS_TASKS
      timesync::ntp_sync_with_wifi(m_renderer);
#endif
      render();
      return;
    }
  }

  std::string label, value;
  bool masked = false, is_bool = false, read_only = false;
  field_label_value(idx, label, value, masked, is_bool, read_only);
  if (read_only) return;
  if (is_bool)
  {
    toggle_bool_field(idx);
    render();
    return;
  }
  m_edit_field = idx;
  m_editing = true;
  m_kb.open(value, masked, label);
  render();
}

void SettingsScreen::commit_edit()
{
  if (m_edit_field >= 0)
  {
    apply_buffer_to_field(m_edit_field, m_kb.buffer());
    m_store->save();
  }
  m_editing = false;
  m_edit_field = -1;
  render();
}

void SettingsScreen::render()
{
  Renderer *r = m_renderer;
  int lh = r->get_line_height();
  if (lh <= 0)
  {
    lh = 24;
  }
  // Reserve the status bar (drawn by the main loop) + the bezel dead-zone so the
  // title is not covered and nothing draws under the painted glass edge.
  int top = status_bar_visible ? (ui::HOME_DZ_TOP + lh + 8) : ui::HOME_DZ_TOP;
  r->set_margin_top(top);
  r->set_margin_left(ui::HOME_DZ_LEFT);
  r->set_margin_right(ui::HOME_DZ_RIGHT);
  r->set_margin_bottom(ui::HOME_DZ_BOTTOM);
  r->clear_screen();

  int pw = r->get_page_width();

  // Title.
  r->draw_text(8, 4, "Settings", true, false);

  // Sub-tab buttons (three equal columns).
  m_tabs_y = 8 + lh + 6;
  m_tabs_h = lh + 12;
  int third = pw / 3;
  r->draw_rect(2,           m_tabs_y, third - 4,     m_tabs_h, 0);
  r->draw_rect(third + 2,   m_tabs_y, third - 4,     m_tabs_h, 0);
  r->draw_rect(third*2 + 2, m_tabs_y, pw-third*2-4,  m_tabs_h, 0);
  r->draw_text(10,          m_tabs_y + 6, m_tab == TAB_TASKS   ? "[Tasks]"  : "Tasks",  m_tab == TAB_TASKS,  false);
  r->draw_text(third + 10,  m_tabs_y + 6, m_tab == TAB_READER  ? "[Reader]" : "Reader", m_tab == TAB_READER, false);
  r->draw_text(third*2+10,  m_tabs_y + 6, m_tab == TAB_DEVICE  ? "[Device]" : "Device", m_tab == TAB_DEVICE, false);

  if (m_editing)
  {
    // The keyboard panel occupies the lower half; draw a hint above it.
    r->draw_text(8, m_tabs_y + m_tabs_h + 8, "Editing field - tap keys:", false, false);
    m_kb.render(r);
    r->flush_display();
    return;
  }

  // Field rows.
  m_list_top = m_tabs_y + m_tabs_h + 10;
  m_row_h = lh + 16;
  int n = field_count();
  for (int i = 0; i < n; ++i)
  {
    std::string label, value;
    bool masked = false, is_bool = false, read_only = false;
    field_label_value(i, label, value, masked, is_bool, read_only);
    if (masked && !value.empty())
    {
      value = std::string(value.size() > 12 ? 12 : value.size(), '*');
    }
    int y = m_list_top + i * m_row_h;
    r->draw_rect(4, y, pw - 8, m_row_h - 4, 0);
    std::string line = label + ": " + value;
    r->draw_text(12, y + 6, line.c_str(), false, false);
  }

  // Save & Back.
  m_save_h = lh + 16;
  m_save_y = m_list_top + n * m_row_h + 12;
  r->draw_rect(4, m_save_y, pw - 8, m_save_h, 0);
  r->draw_text(12, m_save_y + 6, "Save & Back", true, false);

  // Shut down (ship mode). Only meaningful on the Device tab where a battery /
  // charger is present.
  if (m_tab == TAB_DEVICE && m_battery)
  {
    m_shutdown_h = lh + 16;
    m_shutdown_y = m_save_y + m_save_h + 10;
    r->draw_rect(4, m_shutdown_y, pw - 8, m_shutdown_h, 0);
    r->draw_text(12, m_shutdown_y + 6, "Shut down (ship mode)", true, false);
  }
  else
  {
    m_shutdown_h = 0;
  }

  if (m_tab == TAB_TASKS && !m_store->cfg().tasks_configured())
  {
    r->draw_text(8, m_save_y + m_save_h + 10, "Configure WiFi + Obsidian to enable Tasks sync.", false, true);
  }
  if (m_tab == TAB_DEVICE && !m_battery)
  {
    r->draw_text(8, m_save_y + m_save_h + 10, "No battery monitor detected.", false, true);
  }

  r->flush_display();
}

void SettingsScreen::handle_tap(int x, int y)
{
  if (m_editing)
  {
    m_kb.handle_tap(m_renderer, x, y);
    if (m_kb.cancelled())
    {
      m_editing = false;
      m_edit_field = -1;
      render();
    }
    else if (m_kb.done())
    {
      commit_edit();
    }
    return;
  }

  // Sub-tab buttons.
  if (y >= m_tabs_y && y < m_tabs_y + m_tabs_h)
  {
    int pw = m_renderer->get_page_width();
    int third = pw / 3;
    SubTab newtab = (x < third) ? TAB_TASKS : (x < third * 2) ? TAB_READER : TAB_DEVICE;
    if (newtab != m_tab)
    {
      m_tab = newtab;
      render();
    }
    return;
  }

  // Field rows.
  int n = field_count();
  if (y >= m_list_top && y < m_list_top + n * m_row_h)
  {
    int idx = (y - m_list_top) / m_row_h;
    if (idx >= 0 && idx < n)
    {
      begin_edit(idx);
    }
    return;
  }

  // Save & Back.
  if (y >= m_save_y && y < m_save_y + m_save_h)
  {
    m_store->save();
    m_exit = true;
    return;
  }

  // Shut down (ship mode).
  if (m_shutdown_h > 0 && y >= m_shutdown_y && y < m_shutdown_y + m_shutdown_h)
  {
    do_shutdown();
    return;
  }
}

void SettingsScreen::do_shutdown()
{
  Renderer *r = m_renderer;
  // Persist any pending edits before cutting power.
  m_store->save();

  // Full-screen "Hello" so the e-ink visibly shows the board is parked in ship
  // mode (the image persists with no power).
  r->set_margin_top(0);
  r->set_margin_left(0);
  r->set_margin_right(0);
  r->set_margin_bottom(0);
  r->reset(); // de-ghost to a clean white panel
  r->clear_screen();

  const char *msg = "Hello!";
  int pw = r->get_page_width();
  int ph = r->get_page_height();
  int lh = r->get_line_height();
  if (lh <= 0)
  {
    lh = 24;
  }
  int tw = r->get_text_width(msg, true, false);
  if (tw < 0)
  {
    tw = 0;
  }
  int x = (pw - tw) / 2;
  if (x < 0)
  {
    x = 0;
  }
  int yy = ph / 2 - (3 * lh) / 4;
  r->draw_text(x, yy, msg, true, false);
  r->flush_display();

  // Cut the battery. On USB this is a no-op (VBUS keeps it on); on battery the
  // board powers off and stays off until PWR/QON is pressed.
  if (m_battery)
  {
    m_battery->shutdown();
  }
}
} // namespace settings
