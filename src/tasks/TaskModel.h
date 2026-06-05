#pragma once

#include <string>
#include <vector>
#include "TaskParser.h"

// Obsidian Tasks-plugin priority. Maps to a section + an emoji:
//   High   -> "### P1:"  + "⏫"  (fast-forward)
//   Medium -> "### P2:"  + "\U0001f53c" (up triangle)
//   Low    -> "### P3:"  + "\U0001f53d" (down triangle)
//   None   -> "### P3:"  + (no emoji)
enum class TaskPriority
{
  None = 0,
  Low,
  Medium,
  High
};

// All the fields the "+ Add Task" wizard collects. Empty date strings are
// omitted from the serialized line. Dates are ISO "YYYY-MM-DD".
struct NewTask
{
  std::string text;
  TaskPriority priority = TaskPriority::None;
  std::string start;     // \U0001f6eb start date
  std::string scheduled; // ⏳ scheduled date
  std::string due;       // \U0001f4c5 due date
};

// In-memory task list backed by the parsed markdown lines. Only checkbox lines
// are exposed as "tasks"; passthrough lines (headings, blanks, prose) are kept
// internally and re-emitted on serialize so the rest of the vault file survives
// a round-trip.
//
// The file is organised into "### P1:" / "### P2:" / "### P3:" priority
// sections. add() inserts a new task at the end of the section its priority
// maps to (creating the heading if absent). toggle() with a date stamps a
// "✅ YYYY-MM-DD" done date when checking a task and strips it when unchecking.
class TaskModel
{
public:
  // Visible-list filter:
  //   Today - pending tasks + tasks completed today (the working view).
  //   Day   - historical: tasks started, scheduled, OR completed on m_filter_date.
  enum class FilterMode
  {
    Today,
    Day
  };

private:
  std::vector<TaskLine> m_lines;       // all lines (tasks + passthrough)
  std::vector<size_t> m_task_index;    // m_lines indices of VISIBLE tasks
  std::vector<std::string> m_task_sec; // section label per visible task ("P1"..)
  std::string m_today;                 // ISO today; filters out stale done tasks
  FilterMode m_mode = FilterMode::Today;
  std::string m_filter_date;           // ISO day for FilterMode::Day
  bool m_dirty = false;

  void reindex();

  // Index in m_lines to insert a new task line for `prio`. Creates the section
  // heading (and a leading blank separator) at end-of-file if it is missing.
  size_t section_insert_index(TaskPriority prio);

public:
  // Replace contents from raw markdown.
  void load(const std::string &markdown);

  // Serialize back to markdown.
  std::string to_markdown() const;

  size_t size() const { return m_task_index.size(); }
  bool empty() const { return m_task_index.empty(); }

  // Set "today" (ISO "YYYY-MM-DD"), switch to the Today filter, and re-filter.
  // Done tasks whose ✅ date is not today are hidden (still kept in the file).
  // Empty string shows everything.
  void set_today(const std::string &today_iso);

  // Switch to the historical Day filter: show only tasks started, scheduled, or
  // completed on `iso`. set_today() switches back to the working view.
  void set_filter_day(const std::string &iso);

  FilterMode filter_mode() const { return m_mode; }
  const std::string &filter_date() const { return m_filter_date; }

  bool is_done(size_t task_i) const;
  const std::string &text(size_t task_i) const; // raw label (with emoji)

  // Display label for a visible row: emoji stripped/replaced with ASCII tags
  // (🛫->ST ⏳->SD 📅->DD ✅->DNE; priority emoji dropped).
  std::string display_text(size_t task_i) const;
  // Section indicator for a visible row: "P1" / "P2" / "P3" / "".
  const std::string &section_label(size_t task_i) const;

  // Flip a task's checkbox. When `today_iso` is non-empty, checking appends a
  // "✅ today" done date and unchecking strips any existing one. Marks dirty.
  void toggle(size_t task_i, const std::string &today_iso = "");

  // Append a bare unchecked task to the end of the file (no metadata, no
  // section placement). Kept for callers/tests that only need plain text.
  void add(const std::string &text);

  // Insert a fully-specified task: build the "- [ ] text <emoji metadata>" line
  // and place it at the end of the section its priority maps to. Marks dirty.
  void add(const NewTask &task);

  bool dirty() const { return m_dirty; }
  void clear_dirty() { m_dirty = false; }

  // ---- helpers (also used by the add wizard / tests) ----

  // Heading text for a priority's section, e.g. "### P1:".
  static const char *section_heading(TaskPriority prio);
  // Compose the task label (everything after "- [ ] ") from fields.
  static std::string compose_text(const NewTask &task);
  // Remove a trailing "✅ YYYY-MM-DD" done-date token from a label, if present.
  static std::string strip_done_date(const std::string &text);
  // Extract the "✅ YYYY-MM-DD" done date from a label, or "" if none.
  static std::string done_date(const std::string &text);
  // Extract the start (🛫) / scheduled (⏳) / due (📅) date, or "" if none.
  static std::string start_date(const std::string &text);
  static std::string scheduled_date(const std::string &text);
  static std::string due_date(const std::string &text);
  // Emoji -> ASCII for on-display rendering (panels can't draw emoji glyphs).
  static std::string to_display(const std::string &text);
};
