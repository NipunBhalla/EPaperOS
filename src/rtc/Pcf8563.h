#pragma once

#include <time.h>
#include <stdbool.h>

// Minimal PCF8563-compatible RTC driver (LilyGo T5 4.7" S3 Pro, I2C 0x51).
//
// The RTC shares I2C bus 0 (SDA=39, SCL=40) with the GT911 touch controller.
// On this board epdiy already brings the I2C peripheral up, so this driver does
// NOT install the i2c driver -- it only issues transactions on I2C_NUM_0, the
// same way PaperS3TouchControls talks to the GT911. The legacy i2c driver
// serializes concurrent access (touch task + main task) via its per-port mutex.
namespace rtc
{
// Probe the RTC (read a control register). Returns true if it ACKs.
// If the oscillator was stopped (STOP bit set), *was_stopped is set true and
// the time registers are stale — caller should not trust the stored time.
bool begin(bool *was_stopped = nullptr);

// Read the current date/time into *out (broken-down, local wall-clock as stored
// in the RTC). *valid is set false if the RTC flags low-voltage / clock
// integrity loss (its contents are then meaningless). Returns false on I2C error.
bool read(struct tm *out, bool *valid);

// Write a broken-down local date/time to the RTC. Returns false on I2C error.
bool write(const struct tm *in);
} // namespace rtc
