#pragma once

#include "TaskStore.h"

class TaskScreen;

// Offline backend: read/write a markdown file on the SD card (mounted at /fs).
// No WiFi, no coexistence sequencing. A missing file reads as an empty task
// list (first run), so the user can add tasks and have them saved on put().
class LocalSdStore : public TaskStore
{
private:
  TaskScreen &screen;
  const char *m_path;

public:
  explicit LocalSdStore(TaskScreen &screen, const char *path = "/fs/Tasks.md")
      : screen(screen), m_path(path) {}

  bool fetch(std::string &out) override;
  bool put(const std::string &content) override;
};
