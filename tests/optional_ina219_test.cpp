#include "components/optional_ina219/optional_ina219.h"

#include "esphome/core/hal.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>

using esphome::optional_ina219::OptionalINA219Component;

namespace {
int failures = 0;
void expect(bool condition, const char *message) {
  if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}

// Only the electrical bus is simulated: calibration and current conversion use
// the installed native INA219 implementation, including its early error returns.
class RegisterBus : public esphome::i2c::I2CBus {
 public:
  bool connected{true};
  bool reject_writes{false};
  int failed_read_register{-1};
  int failed_write_number{-1};
  int write_attempts{0};
  int transactions{0};
  int reset_attempts{0};
  std::array<uint16_t, 6> registers{};
  void reset_chip() { registers = {0x399f, 0, 0, 0, 0, 0}; }
  void complete_conversion(int16_t current = 1000) {
    registers[2] = 24000 | 2;  // CNVR becomes set only after a full conversion.
    registers[4] = static_cast<uint16_t>(current);
  }
  esphome::i2c::ErrorCode write_readv(uint8_t address, const uint8_t *write_data, size_t write_count,
                                   uint8_t *read_data, size_t read_count) override {
    ++transactions;
    if (address != 0x41 || !connected) return esphome::i2c::ERROR_NOT_ACKNOWLEDGED;
    if (write_count == 0 && read_count == 0) return esphome::i2c::ERROR_OK;
    if (write_count == 3 && read_count == 0) {
      const auto value = static_cast<uint16_t>((write_data[1] << 8) | write_data[2]);
      if (write_data[0] == 0 && value == 0x8000) ++reset_attempts;
      if (++write_attempts == failed_write_number || reject_writes) return esphome::i2c::ERROR_NOT_ACKNOWLEDGED;
      if (write_data[0] == 0 && value == 0x8000) reset_chip();
      else registers.at(write_data[0]) = value;
      return esphome::i2c::ERROR_OK;
    }
    if (write_count == 1 && read_count == 2 && write_data[0] != failed_read_register) {
      const auto value = registers.at(write_data[0]);
      read_data[0] = value >> 8;
      read_data[1] = value & 0xff;
      return esphome::i2c::ERROR_OK;
    }
    return esphome::i2c::ERROR_NOT_ACKNOWLEDGED;
  }
};

struct Fixture {
  RegisterBus bus;
  esphome::sensor::Sensor current;
  OptionalINA219Component monitor;
  Fixture() {
    esphome::app_error = false;
    esphome::test_millis = 0;
    monitor.set_i2c_bus(&bus);
    monitor.set_i2c_address(0x41);
    monitor.set_shunt_resistance_ohm(0.1f);
    monitor.set_max_current_a(3.2f);
    monitor.set_max_voltage_v(32.0f);
    monitor.set_current_sensor(&current);
  }
};

void absent_startup_is_clean_and_does_not_hotplug() {
  Fixture f;
  f.bus.connected = false;
  f.monitor.setup();
  expect(f.bus.transactions == 1, "absent setup must perform only the address probe");
  expect(!f.monitor.is_present(), "missing startup sensor must remain absent");
  expect(!f.monitor.is_failed() && !esphome::app_error, "absence must not mark firmware failed");
  const auto after_setup = f.bus.transactions;
  f.bus.connected = true;
  f.monitor.update();
  f.monitor.on_powerdown();
  expect(!f.monitor.is_present(), "hotplug must require reboot");
  expect(f.bus.transactions == after_setup, "absent component must stop touching the bus");
  expect(std::isnan(f.current.state), "absence cannot publish a current");
}

void native_calibration_and_signed_conversion_are_preserved() {
  Fixture f;
  f.monitor.setup();
  f.bus.complete_conversion();
  f.monitor.update();
  expect(f.monitor.is_present(), "ACK must latch presence");
  expect(std::fabs(f.current.state - 0.098f) < 0.000001f, "native calibrated current should be 0.098 A");
  f.bus.registers[4] = static_cast<uint16_t>(-1000);
  f.monitor.update();
  expect(std::fabs(f.current.state + 0.098f) < 0.000001f, "native signed current must be preserved");
}

void acknowledged_initialization_failure_stays_present_and_recovers() {
  // Native setup writes reset, configuration, then calibration. A failure at
  // any stage must recover, including after partially configuring the chip.
  for (int failed_write : {1, 2, 3}) {
    Fixture f;
    f.bus.failed_write_number = failed_write;
    f.monitor.setup();
    expect(!f.current.samples.empty() && std::isnan(f.current.state), "init failure must publish NAN during setup");
    f.monitor.update();
    expect(f.monitor.is_present(), "init failure after ACK must not select single-sensor fallback");
    expect(!f.current.samples.empty() && std::isnan(f.current.state), "init failure must publish NAN");
    f.bus.failed_write_number = -1;
    f.monitor.update();
    expect(std::isnan(f.current.state), "failed setup must remain invalid during retry cooldown");
    expect(f.bus.reset_attempts == 1, "failed setup must not retry every poll");
    esphome::test_millis += 5000;
    f.monitor.update();
    expect(f.bus.reset_attempts == 2 && f.bus.registers[5] != 0, "detected device must retry native calibration");
    expect(std::isnan(f.current.state), "reinitialization must wait for a new conversion");
    f.bus.complete_conversion();
    f.monitor.update();
    expect(std::isfinite(f.current.state) && !f.monitor.status_has_warning(), "failed setup must recover without reboot");
    expect(!esphome::app_error, "reinitialization must clear the delegate failure status");
  }
}

void read_failures_invalidate_immediately_and_recover() {
  for (int failed_register : {2, 4, 5}) {
    Fixture f;
    f.monitor.setup();
    f.bus.complete_conversion();
    f.monitor.update();
    f.bus.failed_read_register = failed_register;
    const auto samples = f.current.samples.size();
    f.monitor.update();
    expect(f.monitor.is_present(), "read failure must retain boot presence");
    expect(f.current.samples.size() > samples && std::isnan(f.current.state), "failed read must invalidate stale current");
    f.bus.failed_read_register = -1;
    f.monitor.update();
    expect(std::isfinite(f.current.state), "transient read failure must recover");
    expect(!f.monitor.status_has_warning(), "successful current read must clear wrapper warning");
  }
  Fixture f;
  f.monitor.setup();
  f.bus.complete_conversion();
  f.monitor.update();
  f.bus.connected = false;
  f.monitor.update();
  expect(f.monitor.is_present() && std::isnan(f.current.state), "disconnection must invalidate without fallback");
}

void overflow_and_lost_calibration_cannot_publish_plausible_current() {
  Fixture f;
  f.monitor.setup();
  f.bus.complete_conversion();
  f.monitor.update();
  f.bus.registers[2] |= 1;
  f.monitor.update();
  expect(std::isnan(f.current.state), "ADC overflow must invalidate current before native signed wrapping");
  f.bus.registers[2] &= ~1;
  f.bus.registers[5] = 0;
  f.monitor.update();
  expect(std::isnan(f.current.state), "lost calibration must invalidate a plausible zero current");
}

void first_conversion_must_be_ready() {
  Fixture f;
  f.monitor.setup();
  f.monitor.update();
  expect(std::isnan(f.current.state), "reset current zero must not be treated as a completed measurement");
  expect(f.monitor.status_has_warning(), "waiting for conversion must keep measurement unhealthy");
  // Even after the retry interval, waiting for conversion must not reset the ADC.
  esphome::test_millis += 10000;
  f.monitor.update();
  expect(f.bus.reset_attempts == 1, "waiting for conversion must not restart native setup");
  f.bus.complete_conversion(0);
  f.monitor.update();
  expect(f.current.state == 0 && !f.monitor.status_has_warning(), "a completed zero-current reading is valid");
}

void sensor_reset_recovers_without_fallback_or_premature_current() {
  Fixture f;
  f.monitor.setup();
  f.bus.complete_conversion();
  f.monitor.update();
  const auto calibration = f.bus.registers[5];
  f.bus.reset_chip();  // Both calibration and CNVR clear on loss of chip power.
  f.monitor.update();
  expect(f.monitor.is_present() && std::isnan(f.current.state), "reset must invalidate without main-only fallback");
  expect(f.bus.reset_attempts == 1, "rapid resets must respect the retry cooldown");
  esphome::test_millis += 5000;
  f.monitor.update();
  expect(f.bus.registers[5] == calibration, "reset must restore native calibration even with CNVR clear");
  expect(std::isnan(f.current.state), "restored calibration alone cannot validate reset current zero");
  f.bus.complete_conversion(-1000);
  f.monitor.update();
  expect(std::fabs(f.current.state + 0.098f) < 0.000001f, "conversion after reset must restore signed current");
  expect(!f.monitor.status_has_warning(), "recovery must clear unhealthy status");
}

void persistent_setup_faults_have_bounded_retries_across_clock_wrap() {
  Fixture f;
  esphome::test_millis = UINT32_MAX - 2000;
  f.bus.reject_writes = true;
  f.monitor.setup();
  for (int poll = 0; poll < 4; ++poll) {
    esphome::test_millis += 1000;
    f.monitor.update();
  }
  expect(f.bus.reset_attempts == 1, "persistent faults must wait five seconds, including clock rollover");
  esphome::test_millis += 1000;
  f.monitor.update();
  expect(f.bus.reset_attempts == 2, "persistent fault must retry after cooldown across clock rollover");
  f.monitor.update();
  expect(f.bus.reset_attempts == 2 && std::isnan(f.current.state), "failed retry must restart cooldown and remain invalid");
}

void present_device_uses_native_powerdown() {
  Fixture f;
  f.monitor.setup();
  f.monitor.on_powerdown();
  expect(f.bus.registers[0] == 0, "powerdown must preserve native device shutdown");
}
}  // namespace

int main() {
  absent_startup_is_clean_and_does_not_hotplug();
  native_calibration_and_signed_conversion_are_preserved();
  acknowledged_initialization_failure_stays_present_and_recovers();
  read_failures_invalidate_immediately_and_recover();
  overflow_and_lost_calibration_cannot_publish_plausible_current();
  first_conversion_must_be_ready();
  sensor_reset_recovers_without_fallback_or_premature_current();
  persistent_setup_faults_have_bounded_retries_across_clock_wrap();
  present_device_uses_native_powerdown();
  if (failures != 0) return EXIT_FAILURE;
  std::cout << "optional INA219 driver tests passed\n";
}
