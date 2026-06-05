#include "TaskParser.h"

#include <cctype>

namespace
{
  // Try to read a checkbox line. Accepts "- [ ] ", "- [x] ", "* [X] ",
  // "+ [ ] " with any leading indent. Returns true and fills the TaskLine
  // task fields on success.
  bool parse_checkbox(const std::string &line, TaskLine &out)
  {
    size_t i = 0;
    // leading whitespace
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t'))
    {
      i++;
    }
    size_t indent_end = i;
    // bullet marker
    if (i >= line.size() || (line[i] != '-' && line[i] != '*' && line[i] != '+'))
    {
      return false;
    }
    i++;
    // exactly one space, then "["
    if (i >= line.size() || line[i] != ' ')
    {
      return false;
    }
    i++;
    if (i + 2 >= line.size() || line[i] != '[' || line[i + 2] != ']')
    {
      return false;
    }
    char mark = line[i + 1];
    bool done;
    if (mark == ' ')
    {
      done = false;
    }
    else if (mark == 'x' || mark == 'X')
    {
      done = true;
    }
    else
    {
      // some other status char (e.g. "[/]") - treat as not a plain task,
      // keep verbatim so we don't mangle it.
      return false;
    }
    i += 3; // past "]"
    // optional single space after "]"
    if (i < line.size() && line[i] == ' ')
    {
      i++;
    }

    out.is_task = true;
    out.done = done;
    out.indent = line.substr(0, indent_end);
    out.text = line.substr(i);
    return true;
  }
}

namespace TaskParser
{
  std::vector<TaskLine> parse(const std::string &markdown)
  {
    std::vector<TaskLine> lines;
    size_t start = 0;
    while (start <= markdown.size())
    {
      size_t nl = markdown.find('\n', start);
      std::string raw;
      if (nl == std::string::npos)
      {
        raw = markdown.substr(start);
        start = markdown.size() + 1;
      }
      else
      {
        raw = markdown.substr(start, nl - start);
        start = nl + 1;
      }
      // strip a trailing CR (CRLF files)
      if (!raw.empty() && raw.back() == '\r')
      {
        raw.pop_back();
      }

      TaskLine tl;
      if (!parse_checkbox(raw, tl))
      {
        tl.is_task = false;
        tl.raw = raw;
      }
      lines.push_back(tl);

      if (nl == std::string::npos)
      {
        break;
      }
    }
    return lines;
  }

  std::string serialize(const std::vector<TaskLine> &lines)
  {
    std::string out;
    for (size_t i = 0; i < lines.size(); i++)
    {
      const TaskLine &tl = lines[i];
      if (tl.is_task)
      {
        out += tl.indent;
        out += "- [";
        out += (tl.done ? 'x' : ' ');
        out += "] ";
        out += tl.text;
      }
      else
      {
        out += tl.raw;
      }
      if (i + 1 < lines.size())
      {
        out += '\n';
      }
    }
    return out;
  }
}
