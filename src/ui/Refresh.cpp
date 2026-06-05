#include "Refresh.h"

namespace ui
{
RefreshPolicy::RefreshPolicy(int threshold)
    : m_count(0), m_threshold(threshold > 0 ? threshold : 1)
{
}

bool RefreshPolicy::note_partial()
{
  m_count++;
  if (m_count >= m_threshold)
  {
    m_count = 0;
    return true;
  }
  return false;
}

void RefreshPolicy::note_full()
{
  m_count = 0;
}
} // namespace ui
