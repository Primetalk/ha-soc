"""Execute production measurement lambdas with a host clock and sensor recorder."""
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

import yaml

ROOT = Path(__file__).resolve().parents[1]


class LambdaLoader(yaml.SafeLoader):
    pass


LambdaLoader.add_constructor("!lambda", lambda loader, node: loader.construct_scalar(node))


def configuration(name):
    return yaml.load((ROOT / name).read_text(), Loader=LambdaLoader)


def resolve(code):
    defaults = configuration("packages/battery-config.yaml")["substitutions"]
    return re.sub(r"\$\{([^}]+)\}", lambda match: str(defaults[match[1]]), code)


def script_body(config, name):
    return resolve(next(script for script in config["script"] if script["id"] == name)["then"][0]["lambda"])


class MeasurementPipelineTest(unittest.TestCase):
    def test_production_accounting_fault_staleness_and_recovery(self):
        measurement = configuration("packages/measurement.yaml")
        auxiliary = next(sensor for sensor in measurement["sensor"] if sensor.get("id") == "ina219_aux_monitor")
        power = next(sensor for sensor in measurement["sensor"] if sensor.get("id") == "battery_power")
        bodies = {
            "INVALIDATE": script_body(measurement, "invalidate_combined_measurement"),
            "PROCESS": script_body(measurement, "process_battery_measurement"),
            "INTEGRATE": script_body(configuration("packages/soc.yaml"), "integrate_soc_sample"),
            "POWER": resolve(power["on_value"]["then"][0]["lambda"]),
            "AUX_FILTER": resolve(auxiliary["current"]["filters"][0]["lambda"]),
            "STALE": resolve(measurement["interval"][-1]["then"][0]["lambda"]),
        }
        source = r'''
#define BATTERY_MONITOR_HOST_TEST
#include "include/battery_monitor_types.h"
#include <functional>
#include <optional>
#include <iostream>
#include <cstdlib>
#define id(value) value
#define ESP_LOGW(...) do {} while (0)
struct Sensor {
  float state = NAN;
  bool valid = false;
  std::function<void(float)> callback;
  bool has_state() const { return valid; }
  void publish_state(float value) { state = value; valid = true; if (callback) callback(value); }
  void invalidate_state() { valid = false; }
};
namespace battery_monitor {
void invalidate_sensor_state(Sensor &sensor) { sensor.invalidate_state(); }
}
struct Action { std::function<void()> run; void execute() { if (run) run(); } };
struct CurrentAction { std::function<void(float)> run; void execute(float value) { run(value); } };
struct Auxiliary { bool present = false; bool is_present() { return present; } } ina219_aux_monitor;
Sensor battery_current, battery_power, battery_voltage, auxiliary_current;
Sensor battery_charging, battery_discharging, battery_idle, ina219_current_raw;
Sensor ina219_power_raw, ina219_shunt_voltage_raw, shunt_voltage_mv, charge_efficiency_percent;
uint32_t clock_ms = 1000, last_measurement_ms = 0, last_main_current_ms = 0;
uint32_t last_auxiliary_current_ms = 0, last_voltage_ms = 0, last_integration_ms = 0;
uint32_t energy_previous_ms = 0, discarded_integration_gaps = 0;
bool energy_has_baseline = false;
double soc_remaining_ah_live = 300, energy_charged_live = 0, energy_discharged_live = 0;
int charge_resets = 0;
uint32_t millis() { return clock_ms; }
void invalidate(); void process(float); void integrate(float); void integrate_power(float);
Action invalidate_combined_measurement{invalidate};
Action reset_charge_detection{[] { ++charge_resets; }};
Action evaluate_charge_detection;
CurrentAction integrate_soc_sample{integrate};
void invalidate() { @INVALIDATE@ }
void process(float main_current_a) { @PROCESS@ }
void integrate(float current_a) { @INTEGRATE@ }
void integrate_power(float x) { @POWER@ }
std::optional<float> auxiliary_filter(float x) { @AUX_FILTER@ }
void auxiliary_sample(float value) {
  const auto filtered = auxiliary_filter(value);
  if (filtered) { auxiliary_current.publish_state(*filtered); last_auxiliary_current_ms = millis(); }
}
void main_sample(float value) {
  last_voltage_ms = last_main_current_ms = millis();
  battery_voltage.publish_state(12.8f);
  process(value);
}
void stale_tick() { @STALE@ }
void expect(bool condition, const char *message) {
  if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}
void expect_near(double actual, double expected, double tolerance, const char *message) {
  expect(std::isfinite(actual) && std::abs(actual - expected) < tolerance, message);
}
int main() {
  battery_power.callback = integrate_power;
  charge_efficiency_percent.publish_state(99.0f);
  main_sample(10.0f);  // Optional hardware absent; no auxiliary data needed.
  expect_near(battery_current.state, 10.0, 1e-6, "absent auxiliary must preserve main current");
  ina219_aux_monitor.present = true;
  clock_ms = 2000;
  main_sample(0.0f);  // Present, but still awaiting its first reading.
  expect(!battery_current.has_state(), "detected auxiliary must be measured before publishing net current");
  auxiliary_sample(0.06f);
  main_sample(0.0f);  // New baseline after invalidation.
  const double baseline = soc_remaining_ah_live;
  clock_ms = 3000;
  auxiliary_sample(0.06f);
  main_sample(0.0f);
  expect_near(battery_current.state, -0.06, 1e-7, "zero main current must include auxiliary consumption");
  expect_near(soc_remaining_ah_live, baseline - 0.06 / 3600.0, 1e-9, "production SOC lambda must count auxiliary current");
  expect_near(energy_discharged_live, 0.768 / 3600000.0, 1e-12, "production power integration must include auxiliary consumption");

  // Seed a previously settled indication so fault invalidation is observable;
  // the short integration scenario has not exercised the separate 10 s dwell.
  battery_charging.publish_state(false);
  battery_discharging.publish_state(false);
  battery_idle.publish_state(true);
  expect(battery_charging.has_state() && battery_discharging.has_state() && battery_idle.has_state(), "fault test must start with available direction indicators");
  clock_ms = 4000;
  auxiliary_sample(NAN);
  expect(!battery_current.has_state() && !battery_power.has_state(), "auxiliary read fault must invalidate net current and power immediately");
  expect(!battery_charging.has_state() && !battery_discharging.has_state() && !battery_idle.has_state(), "fault must invalidate direction indicators");
  expect(last_integration_ms == 0 && !energy_has_baseline && charge_resets > 0, "fault must reset integration and charging history");
  const double before_gap = soc_remaining_ah_live, energy_before_gap = energy_discharged_live;
  clock_ms = 7000;
  auxiliary_sample(0.06f);
  main_sample(0.0f);
  expect_near(soc_remaining_ah_live, before_gap, 1e-12, "recovery must not integrate the unavailable interval");
  expect_near(energy_discharged_live, energy_before_gap, 1e-12, "energy recovery must not integrate the unavailable interval");
  clock_ms = 8000;
  main_sample(0.0f);
  expect_near(soc_remaining_ah_live, before_gap - 0.06 / 3600.0, 1e-9, "counting must resume after the new baseline");

  clock_ms = 12001;  // Auxiliary sample is now just beyond the 5 s limit.
  last_main_current_ms = clock_ms;
  stale_tick();
  expect(!battery_current.has_state() && !auxiliary_current.has_state(), "stale timer must invalidate both auxiliary and combined readings");
  const double before_stale_gap = soc_remaining_ah_live;
  clock_ms = 13000;
  auxiliary_sample(0.06f);
  main_sample(0.0f);
  expect_near(soc_remaining_ah_live, before_stale_gap, 1e-12, "stale recovery must also establish a new SOC baseline");
  clock_ms = 14000;
  auxiliary_sample(0.06f);
  main_sample(10.0f);
  expect_near(battery_current.state, 9.94, 1e-6, "charging must use the difference between branches");
  clock_ms = 19001;
  auxiliary_sample(0.06f);
  stale_tick();
  expect(!battery_current.has_state() && !battery_voltage.has_state(), "main-branch staleness must invalidate net measurements despite fresh auxiliary data");
  std::cout << "Production measurement pipeline tests passed\n";
}
'''
        for name, body in bodies.items():
            source = source.replace("@" + name + "@", body)
        with tempfile.TemporaryDirectory(prefix="battery-pipeline-") as tmp:
            cpp, binary = Path(tmp) / "pipeline.cpp", Path(tmp) / "pipeline"
            cpp.write_text(source)
            subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-I" + str(ROOT), str(cpp), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
