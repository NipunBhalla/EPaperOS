#pragma once

#include "Battery.h"
#include "BQ27220Battery.h"
#include "BQ25896Battery.h"

// Combined battery driver for LilyGo T5 4.7" S3 Pro / Pro Lite.
// BQ27220 (fuel gauge, 0x55) — primary metrics: %, voltage, current, capacity, health, temp.
// BQ25896 (charger, 0x6B)   — charger info: VBUS, VSYS, charge current, status strings.
class S3Battery : public Battery
{
private:
  BQ27220Battery m_gauge;
  BQ25896Battery m_charger;

public:
  void setup() override
  {
    m_gauge.setup();
    m_charger.setup();
  }

  // Primary metrics from BQ27220
  float get_voltage() override          { return m_gauge.get_voltage(); }
  int   get_percentage() override       { return m_gauge.get_percentage(); }
  int   get_current_ma() override       { return m_gauge.get_current_ma(); }
  int   get_remaining_mah() override    { return m_gauge.get_remaining_mah(); }
  int   get_full_capacity_mah() override{ return m_gauge.get_full_capacity_mah(); }
  int   get_health_percent() override   { return m_gauge.get_health_percent(); }
  int   get_temperature_celsius() override { return m_gauge.get_temperature_celsius(); }
  const char *get_charging_status() override { return m_gauge.get_charging_status(); }
  bool  has_fuel_gauge() override       { return true; }

  // Charger info from BQ25896
  float get_vbus_voltage() override     { return m_charger.get_vbus_voltage(); }
  float get_vsys_voltage() override     { return m_charger.get_vsys_voltage(); }
  float get_charge_current_ma() override{ return m_charger.get_charge_current_ma(); }
  const char *get_vbus_status() override{ return m_charger.get_vbus_status(); }
  const char *get_ntc_status() override { return m_charger.get_ntc_status(); }

  // Ship mode is a charger (BQ25896) function.
  void shutdown() override              { m_charger.shutdown(); }

  void prepare_for_deep_sleep() override { m_charger.prepare_for_deep_sleep(); }
};
