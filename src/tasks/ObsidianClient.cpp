#include "ObsidianClient.h"
#include "TasksConfig.h"
#include "ObsidianUrl.h"

#include <esp_http_client.h>
#include <esp_log.h>
#include <nvs.h>

static const char *TAG = "OBSIDIAN";

// NVS namespace shared with SettingsStore for the Local REST API config.
static const char *SETTINGS_NVS_NAMESPACE = "epaperos";

ObsidianClient::ObsidianClient()
{
  m_url = TASKS_DAV_URL;
  m_user = TASKS_DAV_USER;
  m_pass = TASKS_DAV_PASS;
  load_config();
}

// Reads two NVS namespaces:
//   * "epaperos" (SettingsStore): obs_url, obs_token, note_path -> REST mode.
//   * "tasks" (legacy Track D): dav_url, dav_user, dav_pass -> WebDAV fallback.
// REST wins when a base URL + token + note path are all present.
void ObsidianClient::load_config()
{
  // --- Local REST API config (preferred) ---
  std::string rest_base = TASKS_OBSIDIAN_URL;
  std::string rest_token = TASKS_OBSIDIAN_TOKEN;
  std::string rest_path = TASKS_NOTE_PATH;

  nvs_handle_t sh;
  if (nvs_open(SETTINGS_NVS_NAMESPACE, NVS_READONLY, &sh) == ESP_OK)
  {
    char buf[256];
    size_t len = sizeof(buf);
    if (nvs_get_str(sh, "obs_url", buf, &len) == ESP_OK && len > 1)
    {
      rest_base = buf;
    }
    len = sizeof(buf);
    if (nvs_get_str(sh, "obs_token", buf, &len) == ESP_OK && len > 1)
    {
      rest_token = buf;
    }
    len = sizeof(buf);
    if (nvs_get_str(sh, "note_path", buf, &len) == ESP_OK && len > 1)
    {
      rest_path = buf;
    }
    nvs_close(sh);
  }

  if (!rest_base.empty() && !rest_path.empty())
  {
    m_url = tasks::build_vault_url(rest_base, rest_path);
    m_bearer = rest_token;
    m_user.clear();
    m_pass.clear();
    ESP_LOGI(TAG, "Using Local REST API transport: %s", m_url.c_str());
    return;
  }

  // --- WebDAV fallback (NVS namespace "tasks"): dav_url, dav_user, dav_pass ---
  nvs_handle_t handle;
  if (nvs_open(TASKS_NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK)
  {
    return;
  }
  char buf[256];
  size_t len = sizeof(buf);
  if (nvs_get_str(handle, "dav_url", buf, &len) == ESP_OK && len > 1)
  {
    m_url = buf;
  }
  len = sizeof(buf);
  if (nvs_get_str(handle, "dav_user", buf, &len) == ESP_OK)
  {
    m_user = buf;
  }
  len = sizeof(buf);
  if (nvs_get_str(handle, "dav_pass", buf, &len) == ESP_OK)
  {
    m_pass = buf;
  }
  nvs_close(handle);
}

// Accumulates the response body into the std::string passed via user_data.
static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
  if (evt->event_id == HTTP_EVENT_ON_DATA && evt->user_data)
  {
    std::string *out = static_cast<std::string *>(evt->user_data);
    if (out->size() + evt->data_len <= TASKS_FILE_MAX_BYTES)
    {
      out->append((const char *)evt->data, evt->data_len);
    }
    else
    {
      ESP_LOGE(TAG, "Response exceeds TASKS_FILE_MAX_BYTES, truncating");
    }
  }
  return ESP_OK;
}

bool ObsidianClient::fetch(std::string &out)
{
  out.clear();

  esp_http_client_config_t config = {};
  config.url = m_url.c_str();
  config.event_handler = http_event_handler;
  config.user_data = &out;
  config.timeout_ms = 10000;
  if (!m_user.empty())
  {
    config.username = m_user.c_str();
    config.password = m_pass.c_str();
    config.auth_type = HTTP_AUTH_TYPE_BASIC;
  }

  esp_http_client_handle_t client = esp_http_client_init(&config);
  esp_http_client_set_method(client, HTTP_METHOD_GET);
  if (!m_bearer.empty())
  {
    std::string auth = "Bearer " + m_bearer;
    esp_http_client_set_header(client, "Authorization", auth.c_str());
  }

  esp_err_t err = esp_http_client_perform(client);
  bool ok = false;
  if (err == ESP_OK)
  {
    int status = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "GET %s -> %d (%d bytes)", m_url.c_str(), status, (int)out.size());
    ok = (status >= 200 && status < 300);
  }
  else
  {
    ESP_LOGE(TAG, "GET failed: %s", esp_err_to_name(err));
  }
  esp_http_client_cleanup(client);
  return ok;
}

bool ObsidianClient::put(const std::string &content)
{
  esp_http_client_config_t config = {};
  config.url = m_url.c_str();
  config.timeout_ms = 10000;
  if (!m_user.empty())
  {
    config.username = m_user.c_str();
    config.password = m_pass.c_str();
    config.auth_type = HTTP_AUTH_TYPE_BASIC;
  }

  esp_http_client_handle_t client = esp_http_client_init(&config);
  esp_http_client_set_method(client, HTTP_METHOD_PUT);
  esp_http_client_set_header(client, "Content-Type", "text/markdown");
  if (!m_bearer.empty())
  {
    std::string auth = "Bearer " + m_bearer;
    esp_http_client_set_header(client, "Authorization", auth.c_str());
  }
  esp_http_client_set_post_field(client, content.c_str(), content.size());

  esp_err_t err = esp_http_client_perform(client);
  bool ok = false;
  if (err == ESP_OK)
  {
    int status = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "PUT %s -> %d (%d bytes)", m_url.c_str(), status, (int)content.size());
    // WebDAV servers answer 200/201/204 on a successful write.
    ok = (status >= 200 && status < 300);
  }
  else
  {
    ESP_LOGE(TAG, "PUT failed: %s", esp_err_to_name(err));
  }
  esp_http_client_cleanup(client);
  return ok;
}
