#include "TaskModel.h"

#include <cctype>

static const std::string EMPTY_STR = "";

// Obsidian Tasks emoji (UTF-8 byte escapes so the source stays ASCII-safe).
static const char *EMOJI_HIGH = "\xE2\x8F\xAB";       // ⏫
static const char *EMOJI_MEDIUM = "\xF0\x9F\x94\xBC"; // 🔼
static const char *EMOJI_LOW = "\xF0\x9F\x94\xBD";    // 🔽
static const char *EMOJI_START = "\xF0\x9F\x9B\xAB";  // 🛫
static const char *EMOJI_SCHED = "\xE2\x8F\xB3";      // ⏳
static const char *EMOJI_DUE = "\xF0\x9F\x93\x85";    // 📅
static const char *EMOJI_CREATED = "\xE2\x9E\x95";    // ➕
static const char *EMOJI_DONE = "\xE2\x9C\x85";       // ✅

namespace
{
  std::string trim(const std::string &s)
  {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t'))
    {
      a++;
    }
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r'))
    {
      b--;
    }
    return s.substr(a, b - a);
  }

  void rstrip(std::string &s)
  {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t'))
    {
      s.pop_back();
    }
  }

  // "P1" / "P2" / "P3" for matching an existing section heading by prefix.
  const char *section_token(TaskPriority prio)
  {
    switch (prio)
    {
    case TaskPriority::High:
      return "P1";
    case TaskPriority::Medium:
      return "P2";
    default:
      return "P3"; // Low + None
    }
  }

  const char *priority_emoji(TaskPriority prio)
  {
    switch (prio)
    {
    case TaskPriority::High:
      return EMOJI_HIGH;
    case TaskPriority::Medium:
      return EMOJI_MEDIUM;
    case TaskPriority::Low:
      return EMOJI_LOW;
    default:
      return nullptr; // None: no emoji
    }
  }
}

const char *TaskModel::section_heading(TaskPriority prio)
{
  switch (prio)
  {
  case TaskPriority::High:
    return "### P1:";
  case TaskPriority::Medium:
    return "### P2:";
  default:
    return "### P3:"; // Low + None
  }
}

std::string TaskModel::compose_text(const NewTask &task)
{
  std::string out = trim(task.text);
  const char *pe = priority_emoji(task.priority);
  if (pe)
  {
    out += " ";
    out += pe;
  }
  if (!task.start.empty())
  {
    out += " ";
    out += EMOJI_START;
    out += " ";
    out += task.start;
  }
  if (!task.scheduled.empty())
  {
    out += " ";
    out += EMOJI_SCHED;
    out += " ";
    out += task.scheduled;
  }
  if (!task.due.empty())
  {
    out += " ";
    out += EMOJI_DUE;
    out += " ";
    out += task.due;
  }
  return out;
}

std::string TaskModel::strip_done_date(const std::string &text)
{
  const std::string done = EMOJI_DONE;
  size_t pos = text.find(done);
  if (pos == std::string::npos)
  {
    return text;
  }
  // End = past the emoji, any spaces, and the YYYY-MM-DD date chars.
  size_t end = pos + done.size();
  while (end < text.size() && text[end] == ' ')
  {
    end++;
  }
  while (end < text.size() &&
         (std::isdigit((unsigned char)text[end]) || text[end] == '-'))
  {
    end++;
  }
  // Begin = trim spaces sitting just before the emoji.
  size_t begin = pos;
  while (begin > 0 && text[begin - 1] == ' ')
  {
    begin--;
  }
  std::string out = text.substr(0, begin) + text.substr(end);
  rstrip(out);
  return out;
}

namespace
{
  // Date token immediately following `emoji` in `text` ("YYYY-MM-DD"), or "".
  std::string field_date(const std::string &text, const char *emoji)
  {
    std::string tag = emoji;
    size_t pos = text.find(tag);
    if (pos == std::string::npos)
    {
      return "";
    }
    size_t i = pos + tag.size();
    while (i < text.size() && text[i] == ' ')
    {
      i++;
    }
    size_t start = i;
    while (i < text.size() &&
           (std::isdigit((unsigned char)text[i]) || text[i] == '-'))
    {
      i++;
    }
    return text.substr(start, i - start);
  }
}

std::string TaskModel::done_date(const std::string &text)
{
  return field_date(text, EMOJI_DONE);
}

std::string TaskModel::start_date(const std::string &text)
{
  return field_date(text, EMOJI_START);
}

std::string TaskModel::scheduled_date(const std::string &text)
{
  return field_date(text, EMOJI_SCHED);
}

std::string TaskModel::due_date(const std::string &text)
{
  return field_date(text, EMOJI_DUE);
}

namespace
{
  void replace_all(std::string &s, const std::string &from, const std::string &to)
  {
    if (from.empty())
    {
      return;
    }
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos)
    {
      s.replace(pos, from.size(), to);
      pos += to.size();
    }
  }

  // Heading line -> "P1"/"P2"/"P3", else "".
  std::string heading_label(const std::string &raw)
  {
    std::string t = trim(raw);
    if (t.rfind("### P1", 0) == 0)
      return "P1";
    if (t.rfind("### P2", 0) == 0)
      return "P2";
    if (t.rfind("### P3", 0) == 0)
      return "P3";
    return "";
  }
}

std::string TaskModel::to_display(const std::string &text)
{
  // Drop the ✅ done date entirely: the filled checkbox already shows "done",
  // and the visible list only ever holds tasks completed today.
  std::string s = strip_done_date(text);
  // Date emoji -> bracketed ASCII tag (pad; source may jam them together).
  replace_all(s, EMOJI_START, " [ST] ");
  replace_all(s, EMOJI_SCHED, " [SD] ");
  replace_all(s, EMOJI_DUE, " [DD] ");
  replace_all(s, EMOJI_CREATED, " [CD] ");
  // Priority emoji -> drop (the row's P1/P2/P3 indicator already shows it).
  replace_all(s, EMOJI_HIGH, " ");
  replace_all(s, EMOJI_MEDIUM, " ");
  replace_all(s, EMOJI_LOW, " ");
  // Collapse runs of spaces, then trim.
  std::string out;
  bool prev_space = false;
  for (char c : s)
  {
    if (c == ' ')
    {
      if (!prev_space)
      {
        out += ' ';
      }
      prev_space = true;
    }
    else
    {
      out += c;
      prev_space = false;
    }
  }
  return trim(out);
}

// Rebuild the VISIBLE task index. Walks every line tracking the current
// "### Px:" section; a task is visible unless it is done with a ✅ date other
// than m_today (when m_today is set). Records each visible task's section.
void TaskModel::reindex()
{
  m_task_index.clear();
  m_task_sec.clear();
  std::string section;
  for (size_t i = 0; i < m_lines.size(); i++)
  {
    const TaskLine &L = m_lines[i];
    if (!L.is_task)
    {
      std::string lbl = heading_label(L.raw);
      if (!lbl.empty())
      {
        section = lbl;
      }
      continue;
    }
    bool visible;
    if (m_mode == FilterMode::Day)
    {
      // Historical: any task touching the selected day (started/sched/done).
      const std::string &d = m_filter_date;
      visible = !d.empty() &&
                (start_date(L.text) == d || scheduled_date(L.text) == d ||
                 done_date(L.text) == d);
    }
    else if (!m_today.empty() && L.done)
    {
      // Working view: hide done tasks not completed today.
      visible = (done_date(L.text) == m_today);
    }
    else
    {
      visible = true;
    }
    if (visible)
    {
      m_task_index.push_back(i);
      m_task_sec.push_back(section);
    }
  }
}

void TaskModel::set_today(const std::string &today_iso)
{
  m_today = today_iso;
  m_mode = FilterMode::Today;
  reindex();
}

void TaskModel::set_filter_day(const std::string &iso)
{
  m_filter_date = iso;
  m_mode = FilterMode::Day;
  reindex();
}

void TaskModel::load(const std::string &markdown)
{
  m_lines = TaskParser::parse(markdown);
  reindex();
  m_dirty = false;
}

std::string TaskModel::to_markdown() const
{
  return TaskParser::serialize(m_lines);
}

bool TaskModel::is_done(size_t task_i) const
{
  if (task_i >= m_task_index.size())
  {
    return false;
  }
  return m_lines[m_task_index[task_i]].done;
}

const std::string &TaskModel::text(size_t task_i) const
{
  if (task_i >= m_task_index.size())
  {
    return EMPTY_STR;
  }
  return m_lines[m_task_index[task_i]].text;
}

std::string TaskModel::display_text(size_t task_i) const
{
  if (task_i >= m_task_index.size())
  {
    return EMPTY_STR;
  }
  return to_display(m_lines[m_task_index[task_i]].text);
}

const std::string &TaskModel::section_label(size_t task_i) const
{
  if (task_i >= m_task_sec.size())
  {
    return EMPTY_STR;
  }
  return m_task_sec[task_i];
}

void TaskModel::toggle(size_t task_i, const std::string &today_iso)
{
  if (task_i >= m_task_index.size())
  {
    return;
  }
  TaskLine &tl = m_lines[m_task_index[task_i]];
  tl.done = !tl.done;
  if (tl.done)
  {
    // Stamp a done date unless one is already present.
    if (!today_iso.empty() && tl.text.find(EMOJI_DONE) == std::string::npos)
    {
      std::string t = tl.text;
      rstrip(t);
      t += " ";
      t += EMOJI_DONE;
      t += " ";
      t += today_iso;
      tl.text = t;
    }
  }
  else
  {
    tl.text = strip_done_date(tl.text);
  }
  m_dirty = true;
}

void TaskModel::add(const std::string &text)
{
  TaskLine tl;
  tl.is_task = true;
  tl.done = false;
  tl.indent = "";
  tl.text = text;
  m_lines.push_back(tl);
  reindex();
  m_dirty = true;
}

// Index in m_lines at which to insert a new task for `prio`: the end of that
// priority's section (after its last task, before any trailing blank). Creates
// the section heading at end-of-file when it does not yet exist.
size_t TaskModel::section_insert_index(TaskPriority prio)
{
  const std::string prefix = std::string("### ") + section_token(prio);

  int hi = -1;
  for (size_t i = 0; i < m_lines.size(); i++)
  {
    if (!m_lines[i].is_task && trim(m_lines[i].raw).rfind(prefix, 0) == 0)
    {
      hi = (int)i;
      break;
    }
  }

  if (hi < 0)
  {
    // Section missing: append a blank separator (unless the file already ends
    // blank) + the heading, then put the task right after the heading.
    if (!m_lines.empty())
    {
      const TaskLine &last = m_lines.back();
      bool last_blank = !last.is_task && trim(last.raw).empty();
      if (!last_blank)
      {
        TaskLine blank;
        blank.is_task = false;
        blank.raw = "";
        m_lines.push_back(blank);
      }
    }
    TaskLine h;
    h.is_task = false;
    h.raw = section_heading(prio);
    m_lines.push_back(h);
    return m_lines.size(); // task appended after the new heading
  }

  // Walk the section; insert after its last task, stopping at the next heading.
  size_t insert = (size_t)hi + 1;
  for (size_t i = (size_t)hi + 1; i < m_lines.size(); i++)
  {
    const TaskLine &L = m_lines[i];
    if (!L.is_task)
    {
      if (trim(L.raw).rfind("###", 0) == 0)
      {
        break; // next section heading
      }
      continue; // blank / prose: leave the insert point before it
    }
    insert = i + 1; // a task in this section -> insert after it
  }
  return insert;
}

void TaskModel::add(const NewTask &task)
{
  TaskLine tl;
  tl.is_task = true;
  tl.done = false;
  tl.indent = "";
  tl.text = compose_text(task);

  size_t at = section_insert_index(task.priority);
  if (at > m_lines.size())
  {
    at = m_lines.size();
  }
  m_lines.insert(m_lines.begin() + at, tl);
  reindex();
  m_dirty = true;
}
