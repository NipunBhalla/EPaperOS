#pragma once

#include <string>

namespace settings
{
// All persisted device configuration in one struct. Strings are empty until
// configured via the (deferred) Settings screen or NVS.
struct AppConfig
{
  // Tasks / Obsidian transport (Local REST API plugin).
  std::string wifi_ssid;
  std::string wifi_pass;
  std::string obsidian_url;   // REST API base, e.g. http://192.168.1.50:27123
  std::string obsidian_token; // Bearer token from the plugin
  std::string note_path;      // vault-relative note, e.g. Tasks.md

  // Reader preferences.
  int reader_font_px = 22;
  bool reader_use_ttf = true;
  int reader_margin = 10;

  // True once the minimum needed to sync tasks is present. First boot with an
  // empty config (false) should land in Settings with sync disabled.
  bool tasks_configured() const
  {
    return !wifi_ssid.empty() && !obsidian_url.empty() && !note_path.empty();
  }
};

// Loads / saves AppConfig. On device this persists to NVS (namespace
// "epaperos"); on the host (unit tests) load()/save() are no-ops so the struct
// logic stays testable without ESP-IDF.
class SettingsStore
{
public:
  AppConfig &cfg() { return m_cfg; }
  const AppConfig &cfg() const { return m_cfg; }

  void load();
  void save();
  // Load from a key=value file on SD (e.g. /fs/settings.ini).
  // Merges into current config, then saves to NVS so SD is not needed next boot.
  // Returns true if the file was found and parsed.
  bool load_from_sd(const char *path);

private:
  AppConfig m_cfg;
};
} // namespace settings
