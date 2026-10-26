"""Compile the optional adapter with the installed ESPHome's real INA219 driver."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

import esphome


ROOT = Path(__file__).resolve().parents[1]
ESPHOME = Path(esphome.__file__).parent


class OptionalINA219Test(unittest.TestCase):
    def configuration(self, address):
        return f"""esphome:
  name: optional-ina219-test
esp8266:
  board: d1_mini
i2c:
  id: test_bus
  sda: GPIO4
  scl: GPIO5
external_components:
  - source:
      type: local
      path: {ROOT / 'components'}
    components: [optional_ina219]
sensor:
  - platform: ina219
    id: main_monitor
    address: 0x40
    current:
      name: Main Current
  - platform: optional_ina219
    id: aux_monitor
    address: {address}
    shunt_resistance: 0.1 ohm
    max_current: 3.2 A
    max_voltage: 32 V
    update_interval: 1s
    current:
      name: Auxiliary Current
display:
  - platform: ssd1306_i2c
    model: SSD1306 128x64
    address: 0x3C
"""

    def run_configuration(self, directory, address, command="config", extra_output=None):
        config = Path(directory) / "test.yaml"
        contents = self.configuration(address)
        if extra_output:
            contents = contents.replace("    id: aux_monitor", f"    id: aux_monitor\n    {extra_output}:\n      name: Unsupported Output")
        config.write_text(contents)
        args = [sys.executable, "-m", "esphome", command, str(config)]
        if command == "compile":
            args.append("--only-generate")
        return subprocess.run(args, text=True, capture_output=True)

    def test_distinct_address_generates_with_native_schema_and_builder(self):
        with tempfile.TemporaryDirectory(prefix="optional-ina219-config-") as tmp:
            result = self.run_configuration(tmp, "0x41", "compile")
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_address_conflicts_fail_before_firmware_generation(self):
        for address in ("0x40", "0x3C"):
            with self.subTest(address=address), tempfile.TemporaryDirectory(prefix="optional-ina219-conflict-") as tmp:
                result = self.run_configuration(tmp, address)
                self.assertNotEqual(result.returncode, 0, "conflicting I2C address was accepted")
                self.assertIn("already used on this I2C bus", result.stdout + result.stderr)

    def test_only_current_output_is_supported(self):
        for output in ("bus_voltage", "shunt_voltage", "power"):
            with self.subTest(output=output), tempfile.TemporaryDirectory(prefix="optional-ina219-output-") as tmp:
                result = self.run_configuration(tmp, "0x41", extra_output=output)
                self.assertNotEqual(result.returncode, 0, "unsupported output was accepted")
                self.assertIn("only supports current", result.stdout + result.stderr)

    def test_same_address_on_an_independent_bus_is_accepted(self):
        with tempfile.TemporaryDirectory(prefix="optional-ina219-two-buses-") as tmp:
            config = self.configuration("0x40").replace(
                "  id: test_bus\n  sda: GPIO4\n  scl: GPIO5",
                "  - id: test_bus\n    sda: GPIO4\n    scl: GPIO5\n"
                "  - id: aux_bus\n    sda: GPIO12\n    scl: GPIO14",
            ).replace("    id: main_monitor", "    id: main_monitor\n    i2c_id: test_bus").replace(
                "    id: aux_monitor", "    id: aux_monitor\n    i2c_id: aux_bus"
            ).replace("    model: SSD1306 128x64", "    model: SSD1306 128x64\n    i2c_id: test_bus")
            path = Path(tmp) / "test.yaml"
            path.write_text(config)
            result = subprocess.run(
                [sys.executable, "-m", "esphome", "config", str(path)], text=True, capture_output=True
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_real_driver_with_simulated_i2c(self):
        adapter = ROOT / "components/optional_ina219/optional_ina219.cpp"
        self.assertTrue(adapter.exists(), "optional INA219 adapter is missing")
        with tempfile.TemporaryDirectory(prefix="optional-ina219-") as tmp:
            binary = Path(tmp) / "test"
            subprocess.run(
                [os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra",
                 "-Wpedantic", "-Werror", "-Wno-unused-const-variable",
                 "-I" + str(ROOT / "tests/driver_stubs"), "-I" + str(ESPHOME.parent),
                 "-I" + str(ROOT), str(ROOT / "tests/optional_ina219_test.cpp"),
                 str(adapter), str(ESPHOME / "components/ina219/ina219.cpp"),
                 "-o", str(binary)], check=True,
            )
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
