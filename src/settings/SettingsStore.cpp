#include "SettingsStore.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>

#ifdef ESP_PLATFORM
#include <nvs.h>
#endif

namespace settings
{
#ifdef ESP_PLATFORM
static const char *NS = "epaperos";

static std::string get_str(nvs_handle_t h, const char *key, const std::string &dflt)
{
  size_t len = 0;
  if (nvs_get_str(h, key, nullptr, &len) != ESP_OK || len == 0)
  {
    return dflt;
  }
  std::string out(len, '\0');
  if (nvs_get_str(h, key, &out[0], &len) != ESP_OK)
  {
    return dflt;
  }
  // NVS strings are NUL-terminated; trim the trailing NUL from the buffer.
  if (!out.empty() && out.back() == '\0')
  {
    out.pop_back();
  }
  return out;
}

void SettingsStore::load()
{
  nvs_handle_t h;
  if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK)
  {
    return; // no namespace yet => first run, keep defaults
  }
  m_cfg.wifi_ssid = get_str(h, "wifi_ssid", m_cfg.wifi_ssid);
  m_cfg.wifi_pass = get_str(h, "wifi_pass", m_cfg.wifi_pass);
  m_cfg.obsidian_url = get_str(h, "obs_url", m_cfg.obsidian_url);
  m_cfg.obsidian_token = get_str(h, "obs_token", m_cfg.obsidian_token);
  m_cfg.note_path = get_str(h, "note_path", m_cfg.note_path);

  int32_t v = 0;
  if (nvs_get_i32(h, "rd_font", &v) == ESP_OK)
  {
    m_cfg.reader_font_px = v;
  }
  if (nvs_get_i32(h, "rd_ttf", &v) == ESP_OK)
  {
    m_cfg.reader_use_ttf = v != 0;
  }
  if (nvs_get_i32(h, "rd_margin", &v) == ESP_OK)
  {
    m_cfg.reader_margin = v;
  }
  nvs_close(h);
}

void SettingsStore::save()
{
  nvs_handle_t h;
  if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK)
  {
    return;
  }
  nvs_set_str(h, "wifi_ssid", m_cfg.wifi_ssid.c_str());
  nvs_set_str(h, "wifi_pass", m_cfg.wifi_pass.c_str());
  nvs_set_str(h, "obs_url", m_cfg.obsidian_url.c_str());
  nvs_set_str(h, "obs_token", m_cfg.obsidian_token.c_str());
  nvs_set_str(h, "note_path", m_cfg.note_path.c_str());
  nvs_set_i32(h, "rd_font", m_cfg.reader_font_px);
  nvs_set_i32(h, "rd_ttf", m_cfg.reader_use_ttf ? 1 : 0);
  nvs_set_i32(h, "rd_margin", m_cfg.reader_margin);
  nvs_commit(h);
  nvs_close(h);
}
#else
// Host build (unit tests): no NVS. Config is whatever the test sets directly.
void SettingsStore::load() {}
void SettingsStore::save() {}
#endif

bool SettingsStore::load_from_sd(const char *path)
{
  FILE *f = fopen(path, "r");
  if (!f)
  {
    return false;
  }

  char line[512];
  while (fgets(line, sizeof(line), f))
  {
    // Strip trailing newline/CR
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
    {
      line[--len] = '\0';
    }
    // Skip blank lines and comments
    if (len == 0 || line[0] == '#' || line[0] == ';')
    {
      continue;
    }
    char *eq = strchr(line, '=');
    if (!eq)
    {
      continue;
    }
    *eq = '\0';
    const char *key = line;
    const char *val = eq + 1;

    if (strcmp(key, "wifi_ssid") == 0)        m_cfg.wifi_ssid       = val;
    else if (strcmp(key, "wifi_pass") == 0)   m_cfg.wifi_pass       = val;
    else if (strcmp(key, "obsidian_url") == 0) m_cfg.obsidian_url   = val;
    else if (strcmp(key, "obsidian_token") == 0) m_cfg.obsidian_token = val;
    else if (strcmp(key, "note_path") == 0)   m_cfg.note_path       = val;
    else if (strcmp(key, "reader_font_px") == 0)
    {
      int v = atoi(val);
      if (v >= 8 && v <= 96) m_cfg.reader_font_px = v;
    }
    else if (strcmp(key, "reader_margin") == 0)
    {
      int v = atoi(val);
      if (v >= 0 && v <= 100) m_cfg.reader_margin = v;
    }
    else if (strcmp(key, "reader_use_ttf") == 0)
    {
      m_cfg.reader_use_ttf = atoi(val) != 0;
    }
  }

  fclose(f);
  save(); // persist to NVS so SD not needed on next boot

  // settings.ini is a one-time seed (for values too long to type on-device,
  // e.g. Obsidian URL/token). Rename it after importing so it does NOT clobber
  // on-device edits (which live in NVS) on every subsequent boot. To re-seed,
  // drop a fresh settings.ini on the card.
  char done_path[300];
  snprintf(done_path, sizeof(done_path), "%s.imported", path);
  remove(done_path); // clear a stale backup so the rename can succeed
  if (rename(path, done_path) != 0)
  {
    remove(path); // fallback: at least stop it re-importing next boot
  }
  return true;
}
} // namespace settings
