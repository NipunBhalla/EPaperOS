#include "ObsidianStore.h"
#include "TaskScreen.h"
#include "../rtc/TimeSync.h"

bool ObsidianStore::fetch(std::string &out)
{
  screen.show_message("Connecting WiFi...");
  if (!wifi.connect())
  {
    wifi.disconnect();
    screen.show_message("WiFi failed");
    return false;
  }
  // Opportunistically refresh the clock while the radio is up (no display work
  // here -- coexistence). Best-effort; failure does not affect the task fetch.
  timesync::ntp_sync_store();
  bool ok = client.fetch(out);
  wifi.disconnect(); // radio down before any further display refresh
  if (!ok)
  {
    screen.show_message("Fetch failed");
  }
  return ok;
}

bool ObsidianStore::put(const std::string &content)
{
  screen.show_message("Syncing...");
  if (!wifi.connect())
  {
    wifi.disconnect();
    screen.show_message("WiFi failed");
    return false;
  }
  bool ok = client.put(content);
  wifi.disconnect();
  if (!ok)
  {
    screen.show_message("Sync failed");
  }
  return ok;
}
