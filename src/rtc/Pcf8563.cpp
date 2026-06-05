#include "Pcf8563.h"

#include <driver/i2c.h>
#include <esp_log.h>

// Register map + masks mirror LilyGo's SensorPCF8563 (the chip the rtc_pcf8563
// example drives on this board). Time registers are BCD starting at 0x02.
namespace
{
const char *TAG = "PCF8563";

const i2c_port_t RTC_I2C_PORT = I2C_NUM_0;
const uint8_t RTC_ADDR = 0x51;

const uint8_t REG_STAT1 = 0x00;
const uint8_t REG_SEC = 0x02; // VL_seconds: bit7 = clock integrity lost
const uint8_t VOL_LOW_MASK = 0x80;
const uint8_t CENTURY_MASK = 0x80; // month reg bit7: set=1900s, clear=2000s

uint8_t dec2bcd(int v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }
int bcd2dec(uint8_t v) { return ((v >> 4) * 10) + (v & 0x0F); }

esp_err_t read_regs(uint8_t reg, uint8_t *data, size_t len)
{
  i2c_cmd_handle_t cmd = i2c_cmd_link_create();
  if (!cmd)
  {
    return ESP_FAIL;
  }
  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (RTC_ADDR << 1) | I2C_MASTER_WRITE, true);
  i2c_master_write_byte(cmd, reg, true);
  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (RTC_ADDR << 1) | I2C_MASTER_READ, true);
  if (len > 1)
  {
    i2c_master_read(cmd, data, len - 1, I2C_MASTER_ACK);
  }
  i2c_master_read_byte(cmd, data + len - 1, I2C_MASTER_NACK);
  i2c_master_stop(cmd);
  esp_err_t ret = i2c_master_cmd_begin(RTC_I2C_PORT, cmd, pdMS_TO_TICKS(100));
  i2c_cmd_link_delete(cmd);
  return ret;
}

esp_err_t write_regs(uint8_t reg, const uint8_t *data, size_t len)
{
  i2c_cmd_handle_t cmd = i2c_cmd_link_create();
  if (!cmd)
  {
    return ESP_FAIL;
  }
  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (RTC_ADDR << 1) | I2C_MASTER_WRITE, true);
  i2c_master_write_byte(cmd, reg, true);
  i2c_master_write(cmd, (uint8_t *)data, len, true);
  i2c_master_stop(cmd);
  esp_err_t ret = i2c_master_cmd_begin(RTC_I2C_PORT, cmd, pdMS_TO_TICKS(100));
  i2c_cmd_link_delete(cmd);
  return ret;
}
} // namespace

namespace rtc
{
bool begin(bool *was_stopped)
{
  uint8_t v = 0;
  esp_err_t err = read_regs(REG_STAT1, &v, 1);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "RTC not responding on I2C 0x%02X (err %d)", RTC_ADDR, err);
    return false;
  }
  // Ensure the oscillator is RUNNING. Control_status_1 (0x00) bit5 = STOP; if it
  // is set the clock is frozen and every wake re-seeds the system time from a
  // stale value (looks like time only advances on NTP sync). Clear STOP + TEST
  // by writing 0. Idempotent; preserves the BCD time registers.
  bool stopped = (v & 0x20) != 0;
  if (was_stopped)
  {
    *was_stopped = stopped;
  }
  if (stopped)
  {
    ESP_LOGW(TAG, "RTC STOP bit was set; time registers are stale, starting oscillator");
  }
  uint8_t zero = 0;
  write_regs(REG_STAT1, &zero, 1);
  ESP_LOGI(TAG, "PCF8563 detected at 0x%02X (ctl1=0x%02X)", RTC_ADDR, v);
  return true;
}

bool read(struct tm *out, bool *valid)
{
  if (!out)
  {
    return false;
  }
  uint8_t b[7] = {0};
  if (read_regs(REG_SEC, b, 7) != ESP_OK)
  {
    return false;
  }
  if (valid)
  {
    *valid = (b[0] & VOL_LOW_MASK) == 0;
  }
  out->tm_sec = bcd2dec(b[0] & 0x7F);
  out->tm_min = bcd2dec(b[1] & 0x7F);
  out->tm_hour = bcd2dec(b[2] & 0x3F);
  out->tm_mday = bcd2dec(b[3] & 0x3F);
  // b[4] = weekday (0-6); let mktime/localtime recompute it.
  out->tm_mon = bcd2dec(b[5] & 0x1F) - 1;
  int year = bcd2dec(b[6]);
  year += (b[5] & CENTURY_MASK) ? 1900 : 2000;
  out->tm_year = year - 1900;
  out->tm_wday = 0;
  out->tm_yday = 0;
  out->tm_isdst = -1;
  return true;
}

bool write(const struct tm *in)
{
  if (!in)
  {
    return false;
  }
  int year = in->tm_year + 1900;
  uint8_t b[7];
  b[0] = dec2bcd(in->tm_sec) & 0x7F;
  b[1] = dec2bcd(in->tm_min);
  b[2] = dec2bcd(in->tm_hour);
  b[3] = dec2bcd(in->tm_mday);
  b[4] = (uint8_t)(in->tm_wday & 0x07);
  b[5] = dec2bcd(in->tm_mon + 1);
  if (year < 2000)
  {
    b[5] |= CENTURY_MASK; // century bit set = 1900s
  }
  b[6] = dec2bcd(year % 100);
  return write_regs(REG_SEC, b, 7) == ESP_OK;
}
} // namespace rtc
