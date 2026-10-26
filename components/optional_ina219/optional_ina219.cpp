#include "optional_ina219.h"

#include <cmath>
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome::optional_ina219 {

static const char *const TAG = "optional_ina219";
static constexpr uint8_t REGISTER_BUS_VOLTAGE = 0x02;
static constexpr uint8_t REGISTER_CALIBRATION = 0x05;
static constexpr uint16_t BUS_OVERFLOW = 0x01;
static constexpr uint16_t BUS_CONVERSION_READY = 0x02;
static constexpr uint32_t INITIALIZATION_RETRY_MS = 5000;

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
  this->initialize_();
}

void OptionalINA219Component::initialize_() {
  this->last_initialization_ms_ = millis();
  ESP_LOGI(TAG, "Initializing detected auxiliary INA219 at 0x%02X", this->address_);
  this->delegate_.reset_to_construction_state();
  this->delegate_.setup();
  this->publish_invalid_(this->delegate_.is_failed() ? "Initialization failed; will retry" : "Waiting for conversion");
}

void OptionalINA219Component::retry_initialization_() {
  if (millis() - this->last_initialization_ms_ >= INITIALIZATION_RETRY_MS) this->initialize_();
}

void OptionalINA219Component::dump_config() {
  ESP_LOGCONFIG(TAG, "Optional INA219 at 0x%02X: %s", this->address_, this->present_ ? "present" : "absent");
  if (this->present_) {
    ESP_LOGCONFIG(TAG, "  Measurement: %s", this->fault_reason_ == nullptr ? "healthy" : this->fault_reason_);
    ESP_LOGCONFIG(TAG, "  Initialization retry interval: %u ms", INITIALIZATION_RETRY_MS);
  }
  LOG_SENSOR("  ", "Current", this->current_sensor_);
  LOG_UPDATE_INTERVAL(this);
}

void OptionalINA219Component::update() {
  if (!this->present_) return;
  if (this->delegate_.is_failed()) {
    this->publish_invalid_("Initialization failed; will retry");
    this->retry_initialization_();
    return;
  }

  // Native INA219 update leaves stale state on a failed read. Check status and
  // calibration first, then let its single current read publish or fail.
  uint16_t bus_voltage;
  uint16_t calibration;
  if (!this->read_byte_16(REGISTER_BUS_VOLTAGE, &bus_voltage) ||
      !this->read_byte_16(REGISTER_CALIBRATION, &calibration)) {
    this->publish_invalid_("Status/calibration read failed");
    return;
  }
  // A chip reset clears both calibration and CNVR. Recover calibration before
  // checking readiness, or a reset chip could wait for conversion forever.
  if (calibration == 0) {
    this->publish_invalid_("Calibration lost; will reinitialize");
    this->retry_initialization_();
    return;
  }
  if ((bus_voltage & BUS_OVERFLOW) != 0) {
    this->publish_invalid_("Measurement overflow");
    return;
  }
  if ((bus_voltage & BUS_CONVERSION_READY) == 0) {
    this->publish_invalid_("Waiting for conversion");
    return;
  }
  this->delegate_.update();
  if (this->delegate_.status_has_warning()) {
    this->publish_invalid_("Current read failed");
    return;
  }
  if (this->fault_reason_ != nullptr) ESP_LOGI(TAG, "Auxiliary current measurement ready");
  this->fault_reason_ = nullptr;
  this->status_clear_warning();
}

void OptionalINA219Component::on_powerdown() {
  if (this->present_ && !this->delegate_.is_failed()) this->delegate_.on_powerdown();
}

void OptionalINA219Component::publish_invalid_(const char *reason) {
  if (this->fault_reason_ != reason) ESP_LOGW(TAG, "Auxiliary INA219 at 0x%02X: %s", this->address_, reason);
  this->fault_reason_ = reason;
  this->status_set_warning();
  if (this->current_sensor_ != nullptr) this->current_sensor_->publish_state(NAN);
}

}  // namespace esphome::optional_ina219
