#pragma once

#include "TaskStore.h"
#include "WiFiManager.h"
#include "ObsidianClient.h"

class TaskScreen;

// Remote backend: WiFi up -> GET/PUT one vault note -> WiFi down. Enforces the
// coexistence rule (no display refresh while the radio is up) by rendering all
// status messages BEFORE connect() and after disconnect().
class ObsidianStore : public TaskStore
{
private:
  TaskScreen &screen;
  WiFiManager wifi;
  ObsidianClient client;

public:
  explicit ObsidianStore(TaskScreen &screen) : screen(screen) {}

  bool fetch(std::string &out) override;
  bool put(const std::string &content) override;
};
