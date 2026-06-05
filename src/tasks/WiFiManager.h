#pragma once

#include <string>
#include <esp_event.h>

// Minimal ESP-IDF WiFi station bring-up / teardown.
//
// COEXISTENCE (Track B): EPDiy parallel e-paper shares the GDMA controller with
// WiFi. A WiFi DMA preempt during an active panel row transfer corrupts pixels.
// Mitigation here = SEQUENCE, never overlap: the caller must NOT trigger any
// display refresh (renderer->flush_display / flush_area) between connect() and
// disconnect(). Render text BEFORE connect(), and only render results AFTER
// disconnect(). See plans/track-d-atomic14-obsidian.md + track-b-epdiy-driver-fix.md.
class WiFiManager
{
private:
  bool m_wifi_inited = false;
  bool m_connected = false;
  std::string m_ssid;
  std::string m_password;
  esp_event_handler_instance_t m_wifi_handler = nullptr;
  esp_event_handler_instance_t m_ip_handler = nullptr;

  void load_credentials();
  // One-time WiFi driver + event handler bring-up. Idempotent.
  bool ensure_init();

public:
  WiFiManager();
  ~WiFiManager();

  // Bring the station up and block until we get an IP (or timeout).
  // Returns true on success. Safe to call repeatedly (connect/disconnect cycle).
  bool connect();

  // Stop WiFi and release the radio so the display can refresh safely again.
  void disconnect();

  bool is_connected() const { return m_connected; }
};
