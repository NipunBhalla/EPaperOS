#include "ObsidianUrl.h"

#include <cctype>

namespace tasks
{
namespace
{
bool is_unreserved(unsigned char c)
{
  // RFC 3986 unreserved characters, kept verbatim.
  return std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~';
}

std::string encode_path(const std::string &path)
{
  static const char *hex = "0123456789ABCDEF";
  std::string out;
  out.reserve(path.size());
  for (unsigned char c : path)
  {
    if (is_unreserved(c) || c == '/')
    {
      out.push_back(static_cast<char>(c));
    }
    else
    {
      out.push_back('%');
      out.push_back(hex[(c >> 4) & 0xF]);
      out.push_back(hex[c & 0xF]);
    }
  }
  return out;
}
} // namespace

std::string build_vault_url(const std::string &base, const std::string &note_path)
{
  std::string b = base;
  while (!b.empty() && b.back() == '/')
  {
    b.pop_back();
  }
  std::string p = note_path;
  size_t start = 0;
  while (start < p.size() && p[start] == '/')
  {
    start++;
  }
  p = p.substr(start);
  return b + "/vault/" + encode_path(p);
}
} // namespace tasks
