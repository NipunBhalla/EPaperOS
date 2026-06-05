#include "PaperS3.h"
#include <Renderer/EpdiyRenderer.h>
#include <regular_font.h>
#include <bold_font.h>
#include <italic_font.h>
#include <bold_italic_font.h>
#include <hourglass.h>
#include "controls/ButtonControls.h"
#include "controls/PaperS3TouchControls.h"
#include "battery/S3Battery.h"
#include "battery/ADCBattery.h"
#include <esp_sleep.h>
#include <esp_timer.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <driver/i2c.h>
#include <driver/ledc.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

// The boot button (GPIO0) is the single sleep/wake control. It idles HIGH via
// its pull-up and reads LOW while pressed; GPIO0 is RTC-capable so ext0 can
// wake the chip from deep sleep on that LOW level. Touch is deliberately NOT a
// wake source so a bag/pocket tap can't wake the device.
#define BOOT_BUTTON_GPIO GPIO_NUM_0

// --- Frontlight + IO48 user button -----------------------------------------
// Frontlight enable (BOARD_BL_EN) = GPIO11, a plain on/off output (LilyGo's
// factory firmware does pinMode(BL_EN, OUTPUT) + digitalWrite — no PWM dimming).
// The side "IO48" user button is on the XL9555 I2C expander (addr 0x20) input
// IO1_2 (port 1, bit 2), active-LOW, read over I2C_NUM_0 (shared with touch).
//
// epdiy's board init claims the XL9555 port-1 pins (EP_OE/EP_MODE/TPS...) as
// OUTPUTS, including IO1_2, so the button reads as a driven 0. We must flip
// IO1_2 back to INPUT in the expander's config register before it can be read.
//
// All of this lives in its OWN task that self-delays before touching I2C, so a
// stall here can never take the BOOT sleep/wake task down.
#define BL_GPIO GPIO_NUM_11
// GPIO11 confirmed active-HIGH (high = light on). Driven by LEDC PWM so a single
// press cycles brightness. 8-bit duty per level: Off, Low, Med, High.
#define BL_LEDC_TIMER LEDC_TIMER_0
#define BL_LEDC_CHANNEL LEDC_CHANNEL_0
#define BL_LEDC_MODE LEDC_LOW_SPEED_MODE
#define BL_INVERT 0 // set 1 if a higher duty makes the light dimmer
// Off + a perceptually-spaced ramp topping out at ~50% duty (anything brighter
// is uncomfortable on this frontlight). Low end is finely stepped since the eye
// is most sensitive there.
static const uint8_t BL_LEVELS[] = {0, 8, 12, 24, 64, 128};
static const int BL_LEVEL_COUNT = sizeof(BL_LEVELS) / sizeof(BL_LEVELS[0]);

#define XL9555_ADDR 0x20
#define XL9555_IO12_MASK 0x04   // port-1 bit 2 (IO1_2)
#define XL9555_CFG_PORT1 0x07   // configuration (direction) register, port 1
#define XL9555_INPUT_PORT1 0x01 // input register, port 1

static bool xl9555_read_reg(uint8_t reg, uint8_t *val)
{
  i2c_cmd_handle_t cmd = i2c_cmd_link_create();
  if (!cmd)
  {
    return false;
  }
  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (XL9555_ADDR << 1) | I2C_MASTER_WRITE, true);
  i2c_master_write_byte(cmd, reg, true);
  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (XL9555_ADDR << 1) | I2C_MASTER_READ, true);
  i2c_master_read_byte(cmd, val, I2C_MASTER_NACK);
  i2c_master_stop(cmd);
  esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(100));
  i2c_cmd_link_delete(cmd);
  return ret == ESP_OK;
}

static bool xl9555_write_reg(uint8_t reg, uint8_t val)
{
  i2c_cmd_handle_t cmd = i2c_cmd_link_create();
  if (!cmd)
  {
    return false;
  }
  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (XL9555_ADDR << 1) | I2C_MASTER_WRITE, true);
  i2c_master_write_byte(cmd, reg, true);
  i2c_master_write_byte(cmd, val, true);
  i2c_master_stop(cmd);
  esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(100));
  i2c_cmd_link_delete(cmd);
  return ret == ESP_OK;
}

// Flip IO1_2 to input (set its config bit) without disturbing the EP/TPS output
// pins that epdiy drives on the same port.
static void xl9555_make_io12_input()
{
  uint8_t cfg = 0;
  if (xl9555_read_reg(XL9555_CFG_PORT1, &cfg))
  {
    xl9555_write_reg(XL9555_CFG_PORT1, cfg | XL9555_IO12_MASK);
  }
}

// Button controls for the Paper S3. Two physical buttons drive everything:
//   Boot (GPIO0, fast direct read) and IO48 (XL9555 IO1_2, slow I2C read).
//
//   Boot short        -> DOWN  (next page / menu down)
//   Boot double-tap   -> SELECT (open reader menu / activate)
//   Boot long (hold)  -> REQUEST_SLEEP
//   Boot (asleep)     -> wake (ext0, see setup_deep_sleep)
//   IO48 short        -> UP    (prev page / menu up)
//   IO48 double-tap   -> FULL_REFRESH (full-screen de-ghost redraw)
//   IO48 long (hold)  -> frontlight OFF (local)
//   IO48 held + Boot  -> frontlight cycle (local); suppresses both normal
//                        actions for that press
//
// One unified task owns both inputs so combo state (is IO48 held while Boot is
// tapped?) is coherent. Boot is serviced from t=0 for instant wake response;
// the I2C frontlight/IO48 side self-initialises a few seconds in (the touch
// driver must install I2C_NUM_0 first) and is gated behind an io48-ready flag.
class NoButtonControls : public ButtonControls
{
public:
  explicit NoButtonControls(QueueHandle_t ui_queue) : m_ui_queue(ui_queue)
  {
    gpio_config_t io = {};
    io.pin_bit_mask = 1ULL << BOOT_BUTTON_GPIO;
    io.mode = GPIO_MODE_INPUT;
    io.pull_up_en = GPIO_PULLUP_ENABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&io);
    xTaskCreate(buttonTask, "btn", 4096, this, 5, nullptr);
  }

  bool did_wake_from_deep_sleep() override
  {
    // On Paper S3, we use deep sleep as a low-power "screen off" and
    // want to resume into the previous reading session rather than
    // treating every wake as a cold boot. Consider any non-undefined
    // wakeup cause as a deep-sleep resume.
    return esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_UNDEFINED;
  }
  UIAction get_deep_sleep_action() override { return UIAction::NONE; }
  void setup_deep_sleep() override
  {
    // Same physical button sleeps and wakes: if it is still held down when we
    // arm the ext0 LOW-level wake, the device wakes again immediately. Wait for
    // release (HIGH) plus a debounce settle before arming.
    while (gpio_get_level(BOOT_BUTTON_GPIO) == 0)
    {
      vTaskDelay(pdMS_TO_TICKS(10));
    }
    vTaskDelay(pdMS_TO_TICKS(50));
    // The digital pull-up is dropped in deep sleep; hold GPIO0 HIGH via the RTC
    // pull-up so a press still produces a clean HIGH->LOW edge for ext0.
    rtc_gpio_pullup_en(BOOT_BUTTON_GPIO);
    rtc_gpio_pulldown_dis(BOOT_BUTTON_GPIO);
    esp_sleep_enable_ext0_wakeup(BOOT_BUTTON_GPIO, 0); // wake on LOW (press)
  }

private:
  void emit(UIAction a)
  {
    if (m_ui_queue)
    {
      xQueueSend(m_ui_queue, &a, 0);
    }
  }

  void cycle_backlight()
  {
    int next = (m_bl_level + 1) % BL_LEVEL_COUNT;
    apply_backlight(next);
    printf("[bl] cycle -> %d (duty %d)\n", next, BL_LEVELS[m_bl_level]);
  }

  // Unified button state machine. Boot (GPIO0) is polled every tick from a fast
  // direct read; IO48 (XL9555 over I2C, shared with touch) is polled more slowly
  // from a cached read and only after the frontlight side has initialised.
  //
  // A Boot short press cannot fire until the double-tap window closes (it might
  // be the first of a Boot double-tap -> SELECT), so next-page has ~DOUBLE_GAP_MS
  // of intentional latency. Holding Boot past BOOT_LONG_MS sleeps. While IO48 is
  // held, a Boot tap means "frontlight cycle" and both buttons' normal actions
  // are suppressed for that press.
  static void buttonTask(void *param)
  {
    NoButtonControls *self = static_cast<NoButtonControls *>(param);

    const int TICK_MS = 20;
    const int DEBOUNCE_MS = 30;
    const int BOOT_LONG_MS = 700;        // hold -> sleep
    const int DOUBLE_GAP_MS = 250;       // max gap between taps for double-tap
    const int IO48_LONG_MS = 600;        // hold -> frontlight off
    const int IO48_POLL_MS = 60;         // how often to read the I2C expander
    const int FRONTLIGHT_DELAY_MS = 3000; // wait for touch to install I2C_NUM_0

    int64_t start = esp_timer_get_time();
    bool io48_ready = false;

    // Boot edge/timing state.
    bool boot_prev = false;
    int64_t boot_press_t = 0;
    bool boot_consumed = false; // press already became sleep/combo; no tap on release
    bool pending_tap = false;   // a Boot short press awaiting a possible second tap
    int64_t pending_deadline = 0;

    // IO48 edge/timing state (cached value refreshed every IO48_POLL_MS).
    bool io48_prev = false;
    bool io48_now = false;
    int64_t io48_press_t = 0;
    bool io48_consumed = false; // press already became OFF/combo; no UP on release
    int64_t io48_last_poll = 0;
    bool io48_pending_tap = false; // a short IO48 press awaiting a possible second
    int64_t io48_pending_deadline = 0;

    while (true)
    {
      int64_t now = esp_timer_get_time();

      // One-shot frontlight/IO48 bring-up once the I2C bus is up.
      if (!io48_ready && (now - start) >= (int64_t)FRONTLIGHT_DELAY_MS * 1000)
      {
        self->init_frontlight();
        io48_ready = true;
      }

      // --- read inputs ---
      bool boot_now = (gpio_get_level(BOOT_BUTTON_GPIO) == 0);
      if (io48_ready && (now - io48_last_poll) >= (int64_t)IO48_POLL_MS * 1000)
      {
        io48_now = io48_pressed();
        io48_last_poll = now;
      }

      // ---------- IO48 ----------
      if (io48_now && !io48_prev) // pressed
      {
        io48_press_t = now;
        io48_consumed = false;
      }
      else if (io48_now && io48_prev) // held
      {
        if (!io48_consumed &&
            (now - io48_press_t) >= (int64_t)IO48_LONG_MS * 1000)
        {
          self->apply_backlight(0); // long hold -> frontlight OFF
          printf("[bl] long press -> OFF\n");
          io48_consumed = true;
        }
      }
      else if (!io48_now && io48_prev) // released
      {
        if (!io48_consumed &&
            (now - io48_press_t) >= (int64_t)DEBOUNCE_MS * 1000)
        {
          if (io48_pending_tap) // second tap of a double -> full refresh
          {
            io48_pending_tap = false;
            self->emit(FULL_REFRESH);
          }
          else // first tap: hold briefly to see if a second arrives
          {
            io48_pending_tap = true;
            io48_pending_deadline = now + (int64_t)DOUBLE_GAP_MS * 1000;
          }
        }
        io48_consumed = false;
      }
      io48_prev = io48_now;

      // ---------- resolve a lone IO48 tap as prev-page ----------
      if (io48_pending_tap && now >= io48_pending_deadline)
      {
        io48_pending_tap = false;
        self->emit(UP); // short -> prev page
      }

      // ---------- BOOT ----------
      if (boot_now && !boot_prev) // pressed
      {
        if (io48_now) // combo: IO48 held + Boot tap -> frontlight cycle
        {
          self->cycle_backlight();
          boot_consumed = true;
          io48_consumed = true; // suppress IO48's UP / OFF for this hold
        }
        else
        {
          boot_press_t = now;
          boot_consumed = false;
        }
      }
      else if (boot_now && boot_prev) // held
      {
        if (!boot_consumed && !io48_now &&
            (now - boot_press_t) >= (int64_t)BOOT_LONG_MS * 1000)
        {
          self->emit(REQUEST_SLEEP);
          boot_consumed = true;
        }
      }
      else if (!boot_now && boot_prev) // released
      {
        if (!boot_consumed &&
            (now - boot_press_t) >= (int64_t)DEBOUNCE_MS * 1000)
        {
          if (pending_tap) // this is the second tap of a double
          {
            pending_tap = false;
            self->emit(SELECT);
          }
          else // first tap: hold it briefly to see if a second arrives
          {
            pending_tap = true;
            pending_deadline = now + (int64_t)DOUBLE_GAP_MS * 1000;
          }
        }
        boot_consumed = false;
      }
      boot_prev = boot_now;

      // ---------- resolve a lone Boot tap as next-page ----------
      if (pending_tap && now >= pending_deadline)
      {
        pending_tap = false;
        self->emit(DOWN); // short -> next page
      }

      vTaskDelay(pdMS_TO_TICKS(TICK_MS));
    }
  }

  void apply_backlight(int level)
  {
    if (level < 0) level = 0;
    if (level >= BL_LEVEL_COUNT) level = BL_LEVEL_COUNT - 1;
    m_bl_level = level;
    uint8_t duty = BL_LEVELS[level];
#if BL_INVERT
    duty = 255 - duty;
#endif
    ledc_set_duty(BL_LEDC_MODE, BL_LEDC_CHANNEL, duty);
    ledc_update_duty(BL_LEDC_MODE, BL_LEDC_CHANNEL);
  }

  // Return true if IO48 (XL9555 IO1_2) currently reads pressed (active-LOW).
  static bool io48_pressed()
  {
    uint8_t p1 = 0;
    if (!xl9555_read_reg(XL9555_INPUT_PORT1, &p1))
    {
      return false;
    }
    return (p1 & XL9555_IO12_MASK) == 0; // active low
  }

  // Bring up the frontlight PWM (GPIO11) and make the IO48 button readable.
  // Called once from buttonTask after the touch driver has installed I2C_NUM_0.
  void init_frontlight()
  {
    // Frontlight PWM on GPIO11.
    ledc_timer_config_t lt = {};
    lt.speed_mode = BL_LEDC_MODE;
    lt.duty_resolution = LEDC_TIMER_8_BIT;
    lt.timer_num = BL_LEDC_TIMER;
    lt.freq_hz = 20000; // above audible / flicker
    lt.clk_cfg = LEDC_AUTO_CLK;
    ledc_timer_config(&lt);
    ledc_channel_config_t lc = {};
    lc.gpio_num = BL_GPIO;
    lc.speed_mode = BL_LEDC_MODE;
    lc.channel = BL_LEDC_CHANNEL;
    lc.timer_sel = BL_LEDC_TIMER;
    lc.duty = 0;
    lc.hpoint = 0;
    ledc_channel_config(&lc);
    apply_backlight(0); // start off

    xl9555_make_io12_input(); // make the button readable

    uint8_t cfg = 0xFF, in = 0xFF;
    xl9555_read_reg(XL9555_CFG_PORT1, &cfg);
    xl9555_read_reg(XL9555_INPUT_PORT1, &in);
    printf("[bl] frontlight up. port1 cfg=0x%02X input=0x%02X (IO12 idle bit=%d)\n",
           cfg, in, (in & XL9555_IO12_MASK) ? 1 : 0);
  }

  QueueHandle_t m_ui_queue;
  int m_bl_level = 0;
};

void PaperS3::power_up()
{
  // For PaperS3 we currently rely on the epdiy driver / board config
  // to handle display power. Nothing to do here for now.
}

void PaperS3::prepare_to_sleep()
{
  // Safety net: ensure the EPD HV rails (TPS65185) are off before deep sleep.
  // The renderer already drops them after every refresh, but if we slept right
  // after a draw without a further flush, force them off here. Leaving them on
  // drained ~20mA the whole sleep (~12%/12h).
  epd_poweroff();
}

Renderer *PaperS3::get_renderer()
{
  return new EpdiyRenderer(
      &regular_font,
      &bold_font,
      &italic_font,
      &bold_italic_font,
      hourglass_data,
      hourglass_width,
      hourglass_height);
}

ButtonControls *PaperS3::get_button_controls(QueueHandle_t ui_queue)
{
  // PaperS3 has no nav buttons; the boot button drives sleep/wake. The button
  // controls publish a REQUEST_SLEEP action onto the UI queue when pressed.
  return new NoButtonControls(ui_queue);
}

Battery *PaperS3::get_battery()
{
#if defined(USE_LILYGO_S3_BOARD)
  // LilyGo T5 4.7" S3 Pro / Pro Lite: BQ27220 fuel gauge + BQ25896 charger.
  return new S3Battery();
#elif defined(BATTERY_ADC_CHANNEL)
  // M5 Paper S3 and other ADC-based boards keep the analog path.
  return new ADCBattery(BATTERY_ADC_CHANNEL);
#else
  return nullptr;
#endif
}

TouchControls *PaperS3::get_touch_controls(Renderer *renderer, QueueHandle_t ui_queue)
{
  (void)ui_queue;
#if defined(BOARD_TYPE_PAPER_S3)
  return new PaperS3TouchControls(
      renderer,
      [ui_queue](UIAction action)
      {
        xQueueSend(ui_queue, &action, 0);
      });
#else
  // Fallback to a dummy implementation if built for a different board type.
  return new TouchControls();
#endif
}
