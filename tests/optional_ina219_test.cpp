#include "components/optional_ina219/optional_ina219.h"

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
  int transactions{0};
  std::array<uint16_t, 6> registers{0, 100, 24000, 10, 1000, 0};
  esphome::i2c::ErrorCode write_readv(uint8_t address, const uint8_t *write_data, size_t write_count,
                                   uint8_t *read_data, size_t read_count) override {
    ++transactions;
    if (address != 0x41 || !connected) return esphome::i2c::ERROR_NOT_ACKNOWLEDGED;
    if (write_count == 0 && read_count == 0) return esphome::i2c::ERROR_OK;
    if (write_count == 3 && read_count == 0 && !reject_writes) {
      registers.at(write_data[0]) = static_cast<uint16_t>((write_data[1] << 8) | write_data[2]);
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
  f.monitor.update();
  expect(f.monitor.is_present(), "ACK must latch presence");
  expect(std::fabs(f.current.state - 0.098f) < 0.000001f, "native calibrated current should be 0.098 A");
  f.bus.registers[4] = static_cast<uint16_t>(-1000);
  f.monitor.update();
  expect(std::fabs(f.current.state + 0.098f) < 0.000001f, "native signed current must be preserved");
}

void acknowledged_initialization_failure_stays_present() {
  Fixture f;
  f.bus.reject_writes = true;
  f.monitor.setup();
  expect(!f.current.samples.empty() && std::isnan(f.current.state), "init failure must publish NAN during setup");
  f.monitor.update();
  expect(f.monitor.is_present(), "init failure after ACK must not select single-sensor fallback");
  expect(!f.current.samples.empty() && std::isnan(f.current.state), "init failure must publish NAN");
  f.bus.reject_writes = false;
  f.monitor.update();
  expect(std::isnan(f.current.state), "failed native setup must remain invalid until reboot");
}

void read_failures_invalidate_immediately_and_recover() {
  for (int failed_register : {2, 4, 5}) {
    Fixture f;
    f.monitor.setup();
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
  f.monitor.update();
  f.bus.connected = false;
  f.monitor.update();
  expect(f.monitor.is_present() && std::isnan(f.current.state), "disconnection must invalidate without fallback");
}

void overflow_and_lost_calibration_cannot_publish_plausible_current() {
  Fixture f;
  f.monitor.setup();
  f.monitor.update();
  f.bus.registers[2] |= 1;
  f.monitor.update();
  expect(std::isnan(f.current.state), "ADC overflow must invalidate current before native signed wrapping");
  f.bus.registers[2] &= ~1;
  f.bus.registers[5] = 0;
  f.monitor.update();
  expect(std::isnan(f.current.state), "lost calibration must invalidate a plausible zero current");
}

void native_sensor_faults_invalidate_all_outputs() {
  for (int failed_register : {1, 3}) {
    Fixture f;
    esphome::sensor::Sensor voltage;
    esphome::sensor::Sensor shunt;
    esphome::sensor::Sensor power;
    f.monitor.set_bus_voltage_sensor(&voltage);
    f.monitor.set_shunt_voltage_sensor(&shunt);
    f.monitor.set_power_sensor(&power);
    f.monitor.setup();
    f.monitor.update();
    expect(std::isfinite(f.current.state) && std::isfinite(power.state), "configured native sensors must publish");
    f.bus.failed_read_register = failed_register;
    f.monitor.update();
    expect(std::isnan(voltage.state) && std::isnan(shunt.state) && std::isnan(f.current.state) &&
           std::isnan(power.state), "a native early return must invalidate every configured output");
    f.bus.failed_read_register = -1;
    f.monitor.update();
    expect(std::isfinite(f.current.state), "a later successful full native update must recover");
  }
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
  acknowledged_initialization_failure_stays_present();
  read_failures_invalidate_immediately_and_recover();
  overflow_and_lost_calibration_cannot_publish_plausible_current();
  native_sensor_faults_invalidate_all_outputs();
  present_device_uses_native_powerdown();
  if (failures != 0) return EXIT_FAILURE;
  std::cout << "optional INA219 driver tests passed\n";
}
