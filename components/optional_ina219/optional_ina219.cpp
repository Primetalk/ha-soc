#include "optional_ina219.h"

#include <cmath>
#include "esphome/core/log.h"

namespace esphome::optional_ina219 {

static const char *const TAG = "optional_ina219";

void OptionalINA219Component::setup() {
  // Boot-only presence detection: a missing optional device is not a failed
  // component. Once acknowledged, later faults must never select the fallback.
  if (this->write(nullptr, 0) != i2c::ERROR_OK) {
    ESP_LOGI(TAG, "Optional INA219 at 0x%02X absent; reboot to detect additions", this->address_);
    return;
  }
  this->present_ = true;
  this->delegate_.set_i2c_bus(this->bus_);
  this->delegate_.set_i2c_address(this->address_);
  this->delegate_.setup();
  if (this->delegate_.is_failed()) this->publish_invalid_();
}

void OptionalINA219Component::dump_config() {
  ESP_LOGCONFIG(TAG, "Optional INA219 at 0x%02X: %s", this->address_, this->present_ ? "present" : "absent");
  LOG_UPDATE_INTERVAL(this);
}

void OptionalINA219Component::update() {
  if (!this->present_) return;
  if (this->delegate_.is_failed()) {
    this->publish_invalid_();
    return;
  }

  // Native INA219 update leaves stale sensor state on a failed read and does
  // not check OVF or reset calibration. Reject these before publishing values.
  uint16_t bus_voltage;
  uint16_t calibration;
  if (!this->read_byte_16(0x02, &bus_voltage) || (bus_voltage & 0x01) != 0 ||
      !this->read_byte_16(0x05, &calibration) || calibration == 0) {
    this->publish_invalid_();
    return;
  }
  this->delegate_.update();
  if (this->delegate_.status_has_warning()) {
    this->publish_invalid_();
    return;
  }
  this->status_clear_warning();
}

void OptionalINA219Component::on_powerdown() {
  if (this->present_ && !this->delegate_.is_failed()) this->delegate_.on_powerdown();
}

void OptionalINA219Component::publish_invalid_() {
  this->status_set_warning();
  for (auto *sensor : {this->bus_voltage_sensor_, this->shunt_voltage_sensor_, this->current_sensor_, this->power_sensor_}) {
    if (sensor != nullptr) sensor->publish_state(NAN);
  }
}

}  // namespace esphome::optional_ina219
