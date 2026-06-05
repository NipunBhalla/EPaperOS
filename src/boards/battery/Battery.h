#pragma once

class Battery
{
public:
  virtual void setup() = 0;
  virtual float get_voltage() = 0;       // millivolts
  virtual int get_percentage() = 0;      // 0-100

  // Extended — default no-op for boards without full fuel gauge
  virtual const char *get_charging_status() { return "N/A"; }
  virtual int get_current_ma()           { return 0; }   // mA, negative = discharging
  virtual int get_remaining_mah()        { return 0; }   // mAh
  virtual int get_full_capacity_mah()    { return 0; }   // mAh
  virtual int get_health_percent()       { return 0; }   // 0-100
  virtual int get_temperature_celsius()  { return 0; }   // °C
  virtual float get_vbus_voltage()       { return 0.0f; } // V
  virtual float get_vsys_voltage()       { return 0.0f; } // V
  virtual float get_charge_current_ma()  { return 0.0f; } // mA
  virtual const char *get_vbus_status()  { return "N/A"; }
  virtual const char *get_ntc_status()   { return "N/A"; }
  virtual bool has_fuel_gauge()          { return false; }

  // Enter hardware ship mode (cut battery from the system) if the charger IC
  // supports it. Default no-op for boards without a controllable PMIC.
  virtual void shutdown()                {}

  // Reduce charger IC power consumption before deep sleep. Default no-op.
  virtual void prepare_for_deep_sleep()  {}
};
