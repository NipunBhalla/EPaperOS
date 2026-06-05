#include "TimeSync.h"
#include "Pcf8563.h"
#include "Renderer/Renderer.h"
#ifdef EPAPEROS_TASKS
// NTP + WiFi are only built into the full EPaperOS firmware. A pure reader has
// no WiFi stack; its clock comes from the PCF8563 RTC alone.
#include "../tasks/WiFiManager.h"
#include <esp_sntp.h>
#endif

#include <cstring>
#include <cstdlib>
#include <sys/time.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace
{
const char *TAG = "TIMESYNC";

// India Standard Time, UTC+5:30, no DST. POSIX TZ offsets are inverted in sign.
const char *TZ_IST = "IST-5:30";

// A plausible lower bound for "the clock is actually set" (2023-01-01 UTC).
const time_t TIME_VALID_THRESHOLD = 1672531200;

#ifdef EPAPEROS_TASKS
// Center a short status line on the panel and flush. Used only OUTSIDE the
// WiFi-up window (coexistence: no refresh while the radio is active).
void show_message(Renderer *r, const char *msg)
{
  if (!r)
  {
    return;
  }
  r->set_margin_top(0);
  r->clear_screen();
  int pw = r->get_page_width();
  int ph = r->get_page_height();
  int lh = r->get_line_height();
  if (lh <= 0)
  {
    lh = 24;
  }
  int tw = r->get_text_width(msg, false, false);
  if (tw < 0)
  {
    tw = 0;
  }
  int x = (pw - tw) / 2;
  if (x < 0)
  {
    x = 0;
  }
  r->draw_text(x, ph / 2 - lh, msg, false, false);
  r->flush_display();
}
#endif // EPAPEROS_TASKS
} // namespace

namespace timesync
{
void init_timezone()
{
  setenv("TZ", TZ_IST, 1);
  tzset();
}

bool apply_rtc_to_system()
{
  bool was_stopped = false;
  if (!rtc::begin(&was_stopped))
  {
    return false;
  }
  if (was_stopped)
  {
    // Oscillator was stopped (I2C glitch during deep-sleep power cycle); the
    // time registers are frozen at the moment it stopped — typically the
    // sleep-entry time. Using that value would set the system clock backward.
    // ESP-IDF preserves the POSIX time offset in RTC memory across deep sleep,
    // so time() is already correct without us touching it. Skip settimeofday().
    ESP_LOGW(TAG, "PCF8563 STOP bit was set; skipping settimeofday to avoid stale time");
    return false;
  }
  struct tm t = {};
  bool valid = false;
  if (!rtc::read(&t, &valid) || !valid)
  {
    ESP_LOGW(TAG, "RTC has no valid time yet");
    return false;
  }
  // RTC holds local wall-clock; mktime interprets it via TZ -> UTC epoch.
  time_t epoch = mktime(&t);
  if (epoch <= 0)
  {
    return false;
  }
  struct timeval tv = {.tv_sec = epoch, .tv_usec = 0};
  settimeofday(&tv, nullptr);
  ESP_LOGI(TAG, "System clock set from RTC: %04d-%02d-%02d %02d:%02d:%02d",
           t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
  return true;
}

void format_clock(char *time_out, size_t time_sz, char *date_out, size_t date_sz)
{
  time_t now = time(nullptr);
  struct tm lt;
  localtime_r(&now, &lt);
  if (time_out && time_sz)
  {
    strftime(time_out, time_sz, "%I:%M %p", &lt);
    // Drop a leading zero on the 12-hour value: "01:24 PM" -> "1:24 PM".
    if (time_out[0] == '0')
    {
      memmove(time_out, time_out + 1, strlen(time_out));
    }
  }
  if (date_out && date_sz)
  {
    strftime(date_out, date_sz, "%d %b %Y", &lt);
  }
}

bool set_local_datetime(const struct tm *local)
{
  if (!local)
  {
    return false;
  }
  struct tm t = *local;
  t.tm_isdst = -1;
  time_t epoch = mktime(&t);
  if (epoch <= 0)
  {
    return false;
  }
  struct timeval tv = {.tv_sec = epoch, .tv_usec = 0};
  settimeofday(&tv, nullptr);
  // Mirror to the RTC as local wall-clock (recompute weekday via localtime).
  struct tm norm;
  localtime_r(&epoch, &norm);
  return rtc::write(&norm);
}

#ifdef EPAPEROS_TASKS
bool ntp_sync_store()
{
  if (esp_sntp_enabled())
  {
    esp_sntp_stop();
  }
  esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, "pool.ntp.org");
  esp_sntp_init();

  bool ok = false;
  for (int i = 0; i < 30; i++) // up to ~15 s
  {
    if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED)
    {
      ok = true;
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(500));
  }
  esp_sntp_stop();

  if (!ok)
  {
    ESP_LOGW(TAG, "SNTP did not complete");
    return false;
  }
  time_t now = time(nullptr);
  if (now < TIME_VALID_THRESHOLD)
  {
    return false;
  }
  // SNTP already updated the system clock; mirror local wall-clock to the RTC.
  struct tm lt;
  localtime_r(&now, &lt);
  rtc::write(&lt);
  ESP_LOGI(TAG, "NTP synced: %04d-%02d-%02d %02d:%02d:%02d",
           lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min, lt.tm_sec);
  return true;
}

bool ntp_sync_with_wifi(Renderer *renderer)
{
  // Render BEFORE the radio comes up; nothing else draws until after teardown.
  show_message(renderer, "Syncing time...");

  WiFiManager wifi;
  if (!wifi.connect())
  {
    wifi.disconnect();
    show_message(renderer, "WiFi failed");
    vTaskDelay(pdMS_TO_TICKS(1200));
    return false;
  }
  bool ok = ntp_sync_store();
  wifi.disconnect(); // radio down before any further display refresh

  show_message(renderer, ok ? "Time synced" : "NTP failed");
  vTaskDelay(pdMS_TO_TICKS(1200));
  return ok;
}
#endif // EPAPEROS_TASKS
} // namespace timesync
