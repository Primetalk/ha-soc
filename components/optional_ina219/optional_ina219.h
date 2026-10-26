#pragma once

#include "esphome/components/ina219/ina219.h"

namespace esphome::optional_ina219 {

// Only this component is registered with ESPHome. The native driver owns the
// calibration and conversions; its setup is never called for an absent device.
class OptionalINA219Component : public PollingComponent, public i2c::I2CDevice {
 public:
  void setup() override;
  void dump_config() override;
  void update() override;
  void on_powerdown() override;
  bool is_present() const { return present_; }

  void set_shunt_resistance_ohm(float value) { delegate_.set_shunt_resistance_ohm(value); }
  void set_max_current_a(float value) { delegate_.set_max_current_a(value); }
  void set_max_voltage_v(float value) { delegate_.set_max_voltage_v(value); }
  void set_current_sensor(sensor::Sensor *value) {
    current_sensor_ = value;
    delegate_.set_current_sensor(value);
  }

 protected:
  void initialize_();
  void retry_initialization_();
  void publish_invalid_(const char *reason);
  ina219::INA219Component delegate_;
  bool present_{false};
  uint32_t last_initialization_ms_{0};
  const char *fault_reason_{nullptr};
  sensor::Sensor *current_sensor_{nullptr};
};

}  // namespace esphome::optional_ina219
