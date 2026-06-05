#pragma once

#include "Battery.h"
#include <driver/i2c.h>
#include <esp_log.h>
#include <math.h>

// Charger IC for LilyGo T5 4.7" S3 Pro / Pro Lite.
// I2C_NUM_0, SDA=39, SCL=40, addr 0x6B.
// Used standalone for VBUS/VSYS/charge-current info; in S3Battery it is
// the charger half (BQ27220 is the primary fuel gauge).
class BQ25896Battery : public Battery
{
private:
  static constexpr i2c_port_t I2C_PORT     = I2C_NUM_0;
  static constexpr uint8_t    BQ_ADDR      = 0x6B;

  static constexpr uint8_t REG_ADC_CTRL   = 0x02; // CONV_START(7), CONV_RATE(6)
  static constexpr uint8_t REG_STATUS     = 0x0B; // VBUS_STAT[7:5], CHRG_STAT[4:3]
  static constexpr uint8_t REG_FAULT      = 0x0C; // NTC_FAULT[2:0]
  static constexpr uint8_t REG_BATV       = 0x0E; // BATV[6:0]
  static constexpr uint8_t REG_SYSV       = 0x0F; // SYSV[6:0]
  static constexpr uint8_t REG_VBUSV      = 0x11; // VBUSV[6:0]
  static constexpr uint8_t REG_ICHGR      = 0x12; // ICHGR[6:0]
  static constexpr uint8_t REG_BATFET     = 0x09; // BATFET_DIS(5), BATFET_DLY(3)

  static const char *TAG() { return "BQ25896"; }

  esp_err_t read_reg(uint8_t reg, uint8_t *val)
  {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (!cmd) return ESP_FAIL;
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (BQ_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (BQ_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read_byte(cmd, val, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
  }

  esp_err_t write_reg(uint8_t reg, uint8_t val)
  {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (!cmd) return ESP_FAIL;
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (BQ_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write_byte(cmd, val, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
  }

  uint8_t read8(uint8_t reg)
  {
    uint8_t v = 0;
    read_reg(reg, &v);
    return v;
  }

public:
  void setup() override
  {
    uint8_t ctrl = 0;
    if (read_reg(REG_ADC_CTRL, &ctrl) == ESP_OK)
    {
      write_reg(REG_ADC_CTRL, ctrl | 0x40); // CONV_RATE = continuous
    }
    else
    {
      ESP_LOGW(TAG(), "BQ25896 not responding on I2C (addr 0x%02X)", BQ_ADDR);
    }
  }

  void prepare_for_deep_sleep() override
  {
    // Stop continuous ADC conversion (~2mA) before deep sleep.
    // CONV_RATE=0 means one-shot (conversion already done); ADC shuts off.
    uint8_t ctrl = 0;
    if (read_reg(REG_ADC_CTRL, &ctrl) == ESP_OK)
    {
      write_reg(REG_ADC_CTRL, ctrl & ~0x40); // CONV_RATE = one-shot
    }
  }

  float get_voltage() override
  {
    uint8_t v = read8(REG_BATV);
    return (float)(2304 + (int)(v & 0x7F) * 20); // mV
  }

  int get_percentage() override
  {
    float voltage = get_voltage() / 1000.0f;
    if (voltage >= 4.20f) return 100;
    if (voltage <= 3.50f) return 0;
    return (int)roundf(2836.9625f * powf(voltage, 4) - 43987.4889f * powf(voltage, 3) +
                       255233.8134f * powf(voltage, 2) - 656689.7123f * voltage + 632041.7303f);
  }

  const char *get_charging_status() override
  {
    switch ((read8(REG_STATUS) >> 3) & 0x03)
    {
    case 0: return "Not charging";
    case 1: return "Pre-charge";
    case 2: return "Fast charging";
    case 3: return "Charge done";
    default: return "Unknown";
    }
  }

  float get_vbus_voltage() override
  {
    uint8_t v = read8(REG_VBUSV);
    return (2600 + (int)(v & 0x7F) * 100) / 1000.0f; // V
  }

  float get_vsys_voltage() override
  {
    uint8_t v = read8(REG_SYSV);
    return (2304 + (int)(v & 0x7F) * 20) / 1000.0f; // V
  }

  float get_charge_current_ma() override
  {
    return (float)((int)(read8(REG_ICHGR) & 0x7F) * 50); // mA
  }

  const char *get_vbus_status() override
  {
    switch ((read8(REG_STATUS) >> 5) & 0x07)
    {
    case 0: return "No input";
    case 1: return "USB SDP";
    case 2: return "Adapter";
    case 3: return "OTG";
    case 7: return "OTG mode";
    default: return "Unknown";
    }
  }

  const char *get_ntc_status() override
  {
    switch (read8(REG_FAULT) & 0x07)
    {
    case 0: return "Normal";
    case 2: return "Warm";
    case 3: return "Cool";
    case 5: return "Cold";
    case 6: return "Hot";
    default: return "N/A";
    }
  }

  // Enter ship mode: open BATFET so the battery is disconnected from the system
  // (~uA draw). The board stays off until the PWR/QON button is pressed or USB
  // is plugged in. No effect while running on USB (VBUS keeps the system up).
  void shutdown() override
  {
    uint8_t v = read8(REG_BATFET); // REG09
    v &= ~0x08;                    // BATFET_DLY = 0 -> turn off immediately
    v |= 0x20;                     // BATFET_DIS = 1 -> disconnect battery
    write_reg(REG_BATFET, v);
  }
};
