#include "WiFiManager.h"
#include "TasksConfig.h"

#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <esp_log.h>

static const char *TAG = "TASKS_WIFI";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

static EventGroupHandle_t s_wifi_events = nullptr;
static int s_retry_count = 0;
static const int MAX_RETRY = 5;
// True only while connect() is actively waiting for an IP. Gates the handler so
// the STA_DISCONNECTED event raised by esp_wifi_stop() during disconnect() does
// NOT kick off a reconnect.
static volatile bool s_connecting = false;

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START)
  {
    if (s_connecting)
    {
      esp_wifi_connect();
    }
  }
  else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED)
  {
    if (!s_connecting)
    {
      return; // intentional stop, do not reconnect
    }
    if (s_retry_count < MAX_RETRY)
    {
      s_retry_count++;
      esp_wifi_connect();
      ESP_LOGI(TAG, "Retry connect (%d/%d)", s_retry_count, MAX_RETRY);
    }
    else
    {
      xEventGroupSetBits(s_wifi_events, WIFI_FAIL_BIT);
    }
  }
  else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP)
  {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
    ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
    s_retry_count = 0;
    xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
  }
}

WiFiManager::WiFiManager()
{
  m_ssid = TASKS_WIFI_SSID;
  m_password = TASKS_WIFI_PASSWORD;
}

WiFiManager::~WiFiManager()
{
  disconnect();
}

// Read ssid/password overrides from NVS if present (so creds can be flashed
// without rebuilding). Falls back to compile-time defaults.
void WiFiManager::load_credentials()
{
  char buf[128];

  // Preferred source: the SettingsStore namespace "epaperos" (wifi_ssid /
  // wifi_pass), written by the on-device Settings screen. This is what makes
  // editing WiFi in Settings actually take effect.
  nvs_handle_t sh;
  bool got_from_settings = false;
  if (nvs_open("epaperos", NVS_READONLY, &sh) == ESP_OK)
  {
    size_t len = sizeof(buf);
    if (nvs_get_str(sh, "wifi_ssid", buf, &len) == ESP_OK && len > 1)
    {
      m_ssid = buf;
      got_from_settings = true;
    }
    len = sizeof(buf);
    if (nvs_get_str(sh, "wifi_pass", buf, &len) == ESP_OK)
    {
      m_password = buf;
    }
    nvs_close(sh);
  }
  if (got_from_settings)
  {
    return;
  }

  // Fallback: legacy Track D namespace "tasks" (ssid / password).
  nvs_handle_t handle;
  if (nvs_open(TASKS_NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK)
  {
    return;
  }
  size_t len = sizeof(buf);
  if (nvs_get_str(handle, "ssid", buf, &len) == ESP_OK && len > 1)
  {
    m_ssid = buf;
  }
  len = sizeof(buf);
  if (nvs_get_str(handle, "password", buf, &len) == ESP_OK)
  {
    m_password = buf;
  }
  nvs_close(handle);
}

// One-time bring-up: NVS, netif, default event loop, STA netif, WiFi driver,
// event handlers, mode + credentials. Allocated ONCE and never torn down,
// because unregistering/freeing the esp_event handler instances per
// connect/disconnect corrupts the heap free-list (StoreProhibited in tlsf_free
// inside handler_instances_remove). connect()/disconnect() only start/stop the
// radio; the driver + handlers stay up.
bool WiFiManager::ensure_init()
{
  if (m_wifi_inited)
  {
    return true;
  }

  // NVS must be ready for the WiFi driver (calibration data) + our creds.
  esp_err_t nvs_err = nvs_flash_init();
  if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND)
  {
    nvs_flash_erase();
    nvs_flash_init();
  }

  load_credentials();

  if (!s_wifi_events)
  {
    s_wifi_events = xEventGroupCreate();
  }

  ESP_ERROR_CHECK(esp_netif_init());
  // esp_event loop may already exist; ignore "already created".
  esp_err_t le = esp_event_loop_create_default();
  if (le != ESP_OK && le != ESP_ERR_INVALID_STATE)
  {
    ESP_ERROR_CHECK(le);
  }
  esp_netif_create_default_wifi_sta();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, nullptr, &m_wifi_handler));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, nullptr, &m_ip_handler));

  wifi_config_t wifi_config = {};
  strncpy((char *)wifi_config.sta.ssid, m_ssid.c_str(), sizeof(wifi_config.sta.ssid) - 1);
  strncpy((char *)wifi_config.sta.password, m_password.c_str(), sizeof(wifi_config.sta.password) - 1);
  wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

  m_wifi_inited = true;
  return true;
}

bool WiFiManager::connect()
{
  if (m_connected)
  {
    return true;
  }

  ensure_init();

  xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
  s_retry_count = 0;
  s_connecting = true;

  // Start the radio. STA_START fires our handler -> esp_wifi_connect().
  ESP_ERROR_CHECK(esp_wifi_start());

  ESP_LOGI(TAG, "Connecting to '%s'...", m_ssid.c_str());
  EventBits_t bits = xEventGroupWaitBits(
      s_wifi_events,
      WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
      pdFALSE, pdFALSE,
      pdMS_TO_TICKS(TASKS_WIFI_CONNECT_TIMEOUT_S * 1000));

  if (bits & WIFI_CONNECTED_BIT)
  {
    m_connected = true;
    return true;
  }

  ESP_LOGE(TAG, "WiFi connect failed/timed out");
  // Stop the radio but keep the driver + handlers initialised for next time.
  s_connecting = false;
  esp_wifi_stop();
  return false;
}

void WiFiManager::disconnect()
{
  if (!m_connected)
  {
    return;
  }
  ESP_LOGI(TAG, "Stopping WiFi (release radio for display refresh)");

  // Clear the gate FIRST so the STA_DISCONNECTED event that esp_wifi_stop()
  // raises is ignored by the handler (no reconnect attempt). The driver and
  // event handlers stay initialised; only the radio is stopped. Next connect()
  // just calls esp_wifi_start() again.
  s_connecting = false;
  esp_wifi_disconnect();
  esp_wifi_stop();

  m_connected = false;
}