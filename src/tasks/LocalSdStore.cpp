#include "LocalSdStore.h"
#include "TaskScreen.h"
#include "TasksConfig.h" // TASKS_FILE_MAX_BYTES

#include <cstdio>
#include <esp_log.h>

static const char *TAG = "LOCALTASKS";

bool LocalSdStore::fetch(std::string &out)
{
  out.clear();
  FILE *f = fopen(m_path, "rb");
  if (!f)
  {
    // No file yet: treat as an empty list. put() creates it on first save.
    ESP_LOGI(TAG, "%s not found; starting empty", m_path);
    return true;
  }
  char buf[1024];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
  {
    if (out.size() + n > TASKS_FILE_MAX_BYTES)
    {
      ESP_LOGE(TAG, "%s exceeds TASKS_FILE_MAX_BYTES, truncating", m_path);
      out.append(buf, TASKS_FILE_MAX_BYTES - out.size());
      break;
    }
    out.append(buf, n);
  }
  fclose(f);
  ESP_LOGI(TAG, "Loaded %d bytes from %s", (int)out.size(), m_path);
  return true;
}

bool LocalSdStore::put(const std::string &content)
{
  screen.show_message("Saving...");
  FILE *f = fopen(m_path, "wb");
  if (!f)
  {
    ESP_LOGE(TAG, "Cannot open %s for write", m_path);
    screen.show_message("Save failed");
    return false;
  }
  size_t written = fwrite(content.data(), 1, content.size(), f);
  fclose(f);
  if (written != content.size())
  {
    ESP_LOGE(TAG, "Short write to %s (%d/%d)", m_path, (int)written, (int)content.size());
    screen.show_message("Save failed");
    return false;
  }
  ESP_LOGI(TAG, "Wrote %d bytes to %s", (int)written, m_path);
  return true;
}
