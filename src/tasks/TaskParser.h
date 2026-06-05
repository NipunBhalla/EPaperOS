#pragma once

#include <string>
#include <vector>

// One line of the markdown file. Either a checkbox task or a passthrough line
// (heading, blank, prose, sub-bullet...). Passthrough lines are preserved
// verbatim so write-back never destroys the rest of the file.
struct TaskLine
{
  bool is_task = false;   // true => "- [ ]" / "- [x]" checkbox line
  bool done = false;      // checkbox state (task lines only)
  std::string indent;     // leading whitespace before the marker
  std::string text;       // everything after "[ ] " (incl. emoji metadata)
  std::string raw;        // full original line (passthrough lines only)
};

// Parse + serialize the Obsidian Tasks-plugin checkbox format.
//   "- [ ] Buy milk 📅 2026-06-10"
//   "- [x] Done thing"
// Emoji / date metadata after the text is kept verbatim inside `text`.
namespace TaskParser
{
  // Split markdown into lines, classifying each as task or passthrough.
  std::vector<TaskLine> parse(const std::string &markdown);

  // Re-join lines back into a markdown string (newline separated).
  std::string serialize(const std::vector<TaskLine> &lines);
}
