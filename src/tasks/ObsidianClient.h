#pragma once

#include <string>

// HTTP transport for a single Obsidian markdown note.
//
// GET  -> download the whole note into a std::string
// PUT  -> upload a std::string, overwriting the note
//
// Two backends, selected at load_config() time:
//   * Local REST API plugin (preferred, D2): base URL + Bearer token + vault
//     note path. URL becomes <base>/vault/<path>; requests carry an
//     Authorization: Bearer header.
//   * WebDAV (Track D fallback): full file URL + optional HTTP Basic auth.
// REST is used when an Obsidian base URL + token + note path are configured
// (NVS namespace "epaperos"); otherwise it falls back to the WebDAV settings.
//
// Requires WiFi to be UP (see WiFiManager). Does NOT manage WiFi itself, and
// does NOT touch the display - the caller sequences WiFi/EPD (Track B coexistence).
class ObsidianClient
{
private:
  std::string m_url;
  std::string m_user;
  std::string m_pass;
  std::string m_bearer; // Local REST API token; empty => WebDAV/Basic mode

  void load_config();

public:
  ObsidianClient();

  // Download the tasks file. Returns true on HTTP 2xx; fills `out`.
  bool fetch(std::string &out);

  // Upload (overwrite) the tasks file. Returns true on HTTP 2xx.
  bool put(const std::string &content);
};
