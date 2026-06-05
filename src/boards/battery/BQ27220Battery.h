#pragma once

#include "Battery.h"
#include <driver/i2c.h>
#include <esp_log.h>
#include <math.h>

// Fuel gauge for LilyGo T5 4.7" S3 Pro / Pro Lite.
// Sits on I2C_NUM_0 (SDA=39, SCL=40), addr 0x55 — same bus as BQ25896.
// Gives accurate SoC, health, current, capacity, temperature without curve-fit.
class BQ27220Battery : public Battery
{
private:
  static constexpr i2c_port_t I2C_PORT  = I2C_NUM_0;
  static constexpr uint8_t    BQ_ADDR   = 0x55;

  // Standard commands (16-bit little-endian reads)
  static constexpr uint8_t CMD_TEMPERATURE      = 0x06; // K * 10
  static constexpr uint8_t CMD_VOLTAGE          = 0x08; // mV
  static constexpr uint8_t CMD_BATT_STATUS      = 0x0A;
  static constexpr uint8_t CMD_CURRENT          = 0x0C; // mA signed
  static constexpr uint8_t CMD_REMAIN_CAP       = 0x10; // mAh
  static constexpr uint8_t CMD_FULL_CAP         = 0x12; // mAh
  static constexpr uint8_t CMD_STATE_OF_CHARGE  = 0x2C; // %
  static constexpr uint8_t CMD_STATE_OF_HEALTH  = 0x2E; // %

  // BatteryStatus bits
  static constexpr uint16_t BIT_DSG = (1 << 0);  // discharging
  static constexpr uint16_t BIT_FC  = (1 << 9);  // full charge

  static const char *TAG() { return "BQ27220"; }

  esp_err_t read_reg16(uint8_t reg, uint16_t *val)
  {
    uint8_t data[2] = {};
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (!cmd) return ESP_FAIL;
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (BQ_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (BQ_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read_byte(cmd, &data[0], I2C_MASTER_ACK);
    i2c_master_read_byte(cmd, &data[1], I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    if (ret == ESP_OK)
    {
      *val = (uint16_t)data[0] | ((uint16_t)data[1] << 8);
    }
    return ret;
  }

  uint16_t read16(uint8_t reg)
  {
    uint16_t v = 0;
    read_reg16(reg, &v);
    return v;
  }

  uint16_t batt_status() { return read16(CMD_BATT_STATUS); }

public:
  void setup() override
  {
    uint16_t v = 0;
    if (read_reg16(CMD_VOLTAGE, &v) != ESP_OK)
    {
      ESP_LOGW(TAG(), "BQ27220 not responding on I2C (addr 0x%02X)", BQ_ADDR);
    }
    else
    {
      ESP_LOGI(TAG(), "BQ27220 found, voltage=%u mV", v);
    }
  }

  float get_voltage() override
  {
    return (float)read16(CMD_VOLTAGE); // already mV
  }

  int get_percentage() override
  {
    uint16_t soc = read16(CMD_STATE_OF_CHARGE);
    if (soc > 100) soc = 100;
    return (int)soc;
  }

  int get_current_ma() override
  {
    return (int)(int16_t)read16(CMD_CURRENT);
  }

  int get_remaining_mah() override
  {
    return (int)read16(CMD_REMAIN_CAP);
  }

  int get_full_capacity_mah() override
  {
    return (int)read16(CMD_FULL_CAP);
  }

  int get_health_percent() override
  {
    return (int)read16(CMD_STATE_OF_HEALTH);
  }

  int get_temperature_celsius() override
  {
    // BQ27220 returns K * 10
    int raw = (int)read16(CMD_TEMPERATURE);
    return (raw - 2731) / 10;
  }

  const char *get_charging_status() override
  {
    uint16_t st = batt_status();
    if (st & BIT_FC)  return "Charge done";
    if (!(st & BIT_DSG)) return "Charging";
    return "Discharging";
  }

  bool has_fuel_gauge() override { return true; }
};
