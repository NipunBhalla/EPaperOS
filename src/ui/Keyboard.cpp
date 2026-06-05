#include "Keyboard.h"

#include <cctype>

namespace ui
{
namespace
{
Key make_char(char c)
{
  Key k;
  k.type = KeyType::CHAR;
  k.ch = c;
  return k;
}

Key make_special(KeyType type, const char *label)
{
  Key k;
  k.type = type;
  k.label = label;
  return k;
}
} // namespace

KeyboardModel::KeyboardModel()
{
  rebuild_layout();
}

void KeyboardModel::begin(const std::string &initial, bool masked)
{
  m_buffer = initial;
  m_masked = masked;
  m_layer = 0;
  m_shift = false;
  m_done = false;
  m_cancel = false;
  rebuild_layout();
}

std::string KeyboardModel::display() const
{
  if (!m_masked)
  {
    return m_buffer;
  }
  return std::string(m_buffer.size(), '*');
}

void KeyboardModel::press(const Key &k)
{
  switch (k.type)
  {
  case KeyType::CHAR:
    m_buffer.push_back(k.ch);
    break;
  case KeyType::SPACE:
    m_buffer.push_back(' ');
    break;
  case KeyType::BACKSPACE:
    if (!m_buffer.empty())
    {
      m_buffer.pop_back();
    }
    break;
  case KeyType::SHIFT:
    m_shift = !m_shift;
    rebuild_layout();
    break;
  case KeyType::LAYER:
    m_layer = (m_layer == 0) ? 1 : 0;
    m_shift = false;
    rebuild_layout();
    break;
  case KeyType::DONE:
    m_done = true;
    break;
  case KeyType::CANCEL:
    m_cancel = true;
    break;
  case KeyType::NONE:
  default:
    break;
  }
}

Key KeyboardModel::key_at(const Rect &area, int x, int y) const
{
  Key none;
  if (!area.contains(x, y))
  {
    return none;
  }
  int nrows = static_cast<int>(m_layout.size());
  if (nrows <= 0)
  {
    return none;
  }
  int rh = area.h / nrows;
  if (rh <= 0)
  {
    return none;
  }
  int row = (y - area.y) / rh;
  if (row >= nrows)
  {
    row = nrows - 1;
  }
  const std::vector<Key> &keys = m_layout[row];
  int ncols = static_cast<int>(keys.size());
  if (ncols <= 0)
  {
    return none;
  }
  int cw = area.w / ncols;
  if (cw <= 0)
  {
    return none;
  }
  int col = (x - area.x) / cw;
  if (col >= ncols)
  {
    col = ncols - 1;
  }
  return keys[col];
}

void KeyboardModel::rebuild_layout()
{
  m_layout.clear();

  if (m_layer == 0)
  {
    // QWERTY letters. Bottom letter row carries SHIFT + BACKSPACE.
    const char *rows[3] = {"qwertyuiop", "asdfghjkl", "zxcvbnm"};
    for (int r = 0; r < 3; r++)
    {
      std::vector<Key> row;
      if (r == 2)
      {
        row.push_back(make_special(KeyType::SHIFT, m_shift ? "v" : "^"));
      }
      for (const char *p = rows[r]; *p; ++p)
      {
        char c = m_shift ? static_cast<char>(std::toupper(*p)) : *p;
        row.push_back(make_char(c));
      }
      if (r == 2)
      {
        row.push_back(make_special(KeyType::BACKSPACE, "<x"));
      }
      m_layout.push_back(row);
    }
  }
  else
  {
    // Symbols layer: digits + the punctuation needed for URLs and tokens.
    const char *rows[3] = {"1234567890", ".:/-_@?&=%", "#!$()+,;'\""};
    for (int r = 0; r < 3; r++)
    {
      std::vector<Key> row;
      for (const char *p = rows[r]; *p; ++p)
      {
        row.push_back(make_char(*p));
      }
      if (r == 2)
      {
        row.push_back(make_special(KeyType::BACKSPACE, "<x"));
      }
      m_layout.push_back(row);
    }
  }

  // Function row, common to both layers.
  std::vector<Key> fr;
  fr.push_back(make_special(KeyType::LAYER, m_layer == 0 ? "123" : "ABC"));
  fr.push_back(make_special(KeyType::SPACE, "space"));
  fr.push_back(make_special(KeyType::DONE, "Done"));
  fr.push_back(make_special(KeyType::CANCEL, "Cancel"));
  m_layout.push_back(fr);
}
} // namespace ui
