#pragma once

#include <string>
#include <vector>
#include "HitTest.h"

namespace ui
{
enum class KeyType
{
  NONE,
  CHAR,
  BACKSPACE,
  SPACE,
  SHIFT,
  LAYER, // toggle letters <-> symbols
  DONE,
  CANCEL
};

struct Key
{
  KeyType type;
  char ch;            // populated (already shifted) for CHAR keys
  const char *label;  // display label for non-CHAR keys

  // Constructor (not default member initializers) so brace init stays valid
  // under -std=c++11 (used by the native test env).
  Key(KeyType t = KeyType::NONE, char c = 0, const char *l = nullptr)
      : type(t), ch(c), label(l) {}
};

// On-screen keyboard LOGIC: layer/shift state, edit buffer, key layout, and
// coordinate hit-testing. Pure (no Renderer dependency) so it unit-tests on the
// host; a device-side view draws m_layout and the edit line. Modal: the caller
// opens it via begin(), polls done()/cancelled(), then reads buffer().
class KeyboardModel
{
public:
  KeyboardModel();

  // Open the keyboard seeded with `initial`. masked=true for password / token
  // fields (display() returns asterisks; buffer() still returns the real text).
  void begin(const std::string &initial, bool masked);

  const std::string &buffer() const { return m_buffer; }
  std::string display() const; // masked -> one '*' per character
  bool masked() const { return m_masked; }
  int layer() const { return m_layer; } // 0 = letters, 1 = symbols
  bool shift() const { return m_shift; }
  bool done() const { return m_done; }
  bool cancelled() const { return m_cancel; }

  // Rows of keys for the current layer + shift state (top to bottom).
  const std::vector<std::vector<Key>> &layout() const { return m_layout; }

  // Apply a key press, mutating buffer / layer / shift / done / cancel.
  void press(const Key &k);

  // Hit-test: which key sits under (x,y) within keyboard `area`? Returns a key
  // with type NONE on a miss. Rows are equal height; keys within a row are
  // equal width.
  Key key_at(const Rect &area, int x, int y) const;

private:
  void rebuild_layout();

  std::string m_buffer;
  bool m_masked = false;
  int m_layer = 0;
  bool m_shift = false;
  bool m_done = false;
  bool m_cancel = false;
  std::vector<std::vector<Key>> m_layout;
};
} // namespace ui
