#pragma once

#include "TouchControls.h"
#include "Actions.h"

#include <stdint.h>
#include <functional>

class Renderer;

// Touch controls implementation for the Paper S3 using the GT911
// capacitive touch controller. This uses the "old" ESP-IDF I2C
// driver APIs (i2c_driver_install / i2c_master_*) to avoid
// conflicts with epdiy's use of the legacy I2C driver.
class PaperS3TouchControls : public TouchControls
{
public:
  PaperS3TouchControls(Renderer *renderer, ActionCallback_t on_action);

  // Update gesture sensitivity profile (0=low,1=medium,2=high).
  static void set_gesture_profile(int profile_index);

  // draw any visual touch hints (currently a no-op)
  virtual void render(Renderer *renderer) override;

  // show pressed state feedback (currently a no-op)
  virtual void renderPressedState(Renderer *renderer, UIAction action, bool state = true) override;

  // send GT911 into hardware sleep mode (~100µA) before deep sleep
  virtual void prepare_for_deep_sleep() override;

private:
  static void touchTask(void *param);
  void loop();
  // Emit an action through on_action, honoring the shared emit-debounce window.
  void emitAction(UIAction action);
  bool readTouchPoint(uint16_t *x, uint16_t *y, uint8_t *points, bool *home_key);
  UIAction mapTapToAction(uint16_t x, uint16_t y);
  UIAction mapSwipeUpToAction(uint16_t start_x, uint16_t start_y, uint16_t end_x, uint16_t end_y, uint8_t max_points);
  UIAction mapSwipeDownToAction(uint16_t start_x, uint16_t start_y, uint16_t end_x, uint16_t end_y, uint8_t max_points);
  UIAction mapLongPressToAction(uint16_t x, uint16_t y);

  ActionCallback_t on_action;
  Renderer *renderer;

  bool touch_active = false;
  // Capacitive home key (GT911 status bit 0x10): emit GO_HOME on the press edge.
  // Long-press on this key was abandoned -- the bit is latched and the GT911
  // re-asserts it too inconsistently after a clear to time a hold reliably (it
  // mostly read as a ~0 ms hold). FULL_REFRESH lives on the IO48 double-click
  // instead (see PaperS3.cpp). Sampled on a slow cadence; the latch is cleared
  // each sample so a lingering bit can't double-fire or wedge input.
  bool home_key_down = false;
  bool home_key_longfired = false; // kept false; preserves readTouchPoint latch path
  uint32_t home_key_sample_tick = 0;
  // Key sampling period. Must exceed the GT911's ~120 ms bit-0x10 re-assert gap.
  static const uint32_t HOME_SAMPLE_MS = 150;
  bool driver_ok = false;
  volatile bool m_stop_polling = false;
  UIAction last_action = NONE;
  uint8_t i2c_addr = 0x14; // default GT911 address, will probe 0x14/0x5D
  uint32_t touch_start_tick = 0;
  bool long_press_handled = false;
  // Debounce: drop a second emitted action that lands within this window of the
  // previous one, so a tap straddling a panel-refresh blackout fires once.
  uint32_t m_last_emit_tick = 0;
  static const uint32_t EMIT_DEBOUNCE_MS = 150;
};
