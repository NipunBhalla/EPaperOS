#pragma once

#include <string>

// Storage backend for the single tasks markdown file. The task model and parser
// are transport-agnostic (they work on a markdown string), so the only thing
// that varies between online and offline is how that string is read and written.
//
// Two implementations, chosen once at construction (see TasksController):
//   * ObsidianStore - remote, over WiFi (GET/PUT one vault note).
//   * LocalSdStore  - offline, a markdown file on the SD card.
// Selection is a hard either/or by whether Obsidian credentials are configured;
// there is no fallback and no local<->remote merge.
class TaskStore
{
public:
  virtual ~TaskStore() = default;

  // Load the whole tasks markdown into `out`. Returns true on success.
  virtual bool fetch(std::string &out) = 0;

  // Overwrite the tasks markdown with `content`. Returns true on success.
  virtual bool put(const std::string &content) = 0;
};
