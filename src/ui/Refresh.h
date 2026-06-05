#pragma once

namespace ui
{
// Shared ghosting budget across ALL partial-refresh sources (tab bar, keyboard,
// task lines, scroll). After `threshold` partial updates a full flash is due to
// clear accumulated e-ink ghosting. This is a global counter, NOT per-widget:
// construct ONE RefreshPolicy and share it across the UI.
class RefreshPolicy
{
public:
  explicit RefreshPolicy(int threshold = 20);

  // Record a partial refresh. Returns true if a full flash is now due (and
  // resets the counter). The caller is responsible for performing the flush.
  bool note_partial();

  // Record that a full flash happened, resetting the counter.
  void note_full();

  int count() const { return m_count; }
  int threshold() const { return m_threshold; }

private:
  int m_count;
  int m_threshold;
};
} // namespace ui
