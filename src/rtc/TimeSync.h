#pragma once

#include <time.h>
#include <stddef.h>

class Renderer;

// Time plumbing on top of the PCF8563 RTC: timezone setup, system-clock sync,
// status-bar formatting, manual set, and NTP sync (auto via Tasks WiFi, or
// manual from Settings).
namespace timesync
{
// Set the process timezone (IST, UTC+5:30, no DST) so localtime()/strftime()
// render local wall-clock. Call once at boot before any formatting.
void init_timezone();

// Probe the RTC and, if it holds a valid time, push it to the system clock
// (settimeofday) so time()/localtime() work. Returns true if a valid time was
// applied.
bool apply_rtc_to_system();

// Format the current local time into caller buffers:
//   time_out -> "H:MM AM/PM"   (no leading zero on the hour)
//   date_out -> "DD Mon YYYY"
void format_clock(char *time_out, size_t time_sz, char *date_out, size_t date_sz);

// Set a new local date/time: updates the system clock AND writes it to the RTC.
// Returns true if the RTC write succeeded.
bool set_local_datetime(const struct tm *local);

// Run SNTP assuming WiFi is already connected (no WiFi or display work here).
// On success the system clock is updated and mirrored to the RTC. Safe to call
// from TasksController while the radio is up. Returns true if time was obtained.
bool ntp_sync_store();

// Full manual NTP sync: render a status message, bring WiFi up, sync, tear WiFi
// down, then return. Honors the Track-B coexistence rule (no display refresh
// between WiFi connect and disconnect). Returns true on success.
bool ntp_sync_with_wifi(Renderer *renderer);
} // namespace timesync
