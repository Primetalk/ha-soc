# ESPHome high-side battery monitor

An ESP32-C3 and INA219 battery monitor for a 4S LiFePO4 battery, an external
500 A / 75 mV shunt, an SSD1306 OLED, and Home Assistant.

The canonical ESPHome entry point is [`battery-monitor.yaml`](battery-monitor.yaml).
It provides:

- high-side battery-voltage and current measurement;
- normalized signed current: positive is charging, negative is discharging;
- signed power and charging/discharging/idle states;
- persistent, unbounded amp-hour coulomb counting;
- explicit SOC validity and plausibility diagnostics;
- manual `Set Battery Full`, `Set Battery Empty`, and `Invalidate SOC` controls;
- hysteretic Stop Charge, Capacity Warning, and Stop Load conditions;
- durable sequence-tagged crossing metadata and replay after an API reconnect;
- a local SSD1306 status display;
- encrypted Home Assistant API, OTA, and a fallback captive portal.

This firmware estimates state of charge. It is **not** a battery safety system.
See [Safety boundary](#safety-boundary) before constructing or operating the
monitor.

## Supported default hardware

| Item                      | Default                                              |
| ------------------------- | ---------------------------------------------------- |
| Battery profile           | 4S LiFePO4, 300 Ah rated capacity                    |
| Controller                | ESP32-C3 DevKitM-1                                   |
| Current/voltage monitor   | INA219 breakout, onboard shunt electrically isolated |
| External shunt            | 500 A / 75 mV                                        |
| External shunt resistance | `0.075 V / 500 A = 0.00015 ohm`                      |
| Display                   | SSD1306 128x64 I2C OLED                              |
| INA219 address            | `0x40`                                               |
| OLED address              | `0x3C`                                               |
| I2C pins                  | GPIO8 SDA, GPIO9 SCL                                 |
| Supported ESPHome         | 2026.8.0                                             |

Hardware and battery defaults are centralized in
[`packages/battery-config.yaml`](packages/battery-config.yaml). Review that file
before flashing a different installation.

## Safety boundary

The monitor and its Home Assistant automations do not replace any of the
following:

- a correctly configured BMS;
- a battery main fuse with a suitable DC interrupt rating;
- charger overvoltage and fault protection;
- load undervoltage and fault protection;
- correctly sized cables, bus bars, lugs, enclosures, insulation, and fusing;
- a protected DC/DC supply for the ESP32-C3 and peripherals.

Do not route the 500 A path through the INA219 breakout, an ESP32 board, a PCB
relay, or thin wire. Main current flows only through the external shunt and
properly rated conductors. If future automation switches a charger or load, use
appropriately rated contactors or equipment control inputs with isolated,
fail-safe drivers.

Disconnect and make the battery safe before changing wiring. A 300 Ah battery
can deliver destructive fault current. Use components, fuses, procedures, and
personal protective equipment appropriate to the installation; obtain help
from a qualified professional when required.

## High-side wiring

### Main current path

Install the shunt in the positive conductor:

```text
charger/load positive bus ── VIN+ [500 A / 75 mV shunt] VIN− ── battery positive
common negative bus ─────────────────────────────────────────── battery negative
```

`VIN+` and `VIN-` above identify the two external-shunt sense sides, not a path
through the INA219 PCB. With this orientation:

- charging current flows from `VIN+` to `VIN-` and is published as positive;
- discharging current flows from `VIN-` to `VIN+` and is published as negative;
- the INA219 bus-voltage channel measures the battery-side `VIN-` point relative
  to common negative, so it reports battery voltage;
- ESP32-C3 ground, INA219 ground, OLED ground, and battery/common negative must
  share the required reference for this non-isolated design.

The default design is suitable for the documented 4S battery voltage. Do not
exceed the voltage/common-mode ratings of the INA219 IC, breakout, power supply,
or ESP32-C3. The ESPHome INA219 configuration value is not permission to exceed
hardware ratings.

### Kelvin sense wiring

Use two dedicated sense connections directly at the external shunt terminals:

```text
external shunt bus-side sense point     ── fuse ── INA219 VIN+
external shunt battery-side sense point ── fuse ── INA219 VIN−
```

Requirements:

1. Attach each Kelvin lead directly to its shunt sense point, not farther along
   a high-current cable, bus bar, or lug.
2. Keep both leads together, short, mechanically protected, and away from noisy
   switching conductors where practical.
3. Fuse each lead close to the energized shunt tap with a fuse and holder that
   protect the thin wire and can safely interrupt the installation's DC fault
   current.
4. Never use a sense lead as a supply or load conductor.
5. Verify polarity with a controlled low current before relying on SOC.

### Isolate the breakout's onboard shunt

The INA219 breakout's low-value onboard current-sense resistor must not remain
connected across `VIN+` and `VIN-` when an external shunt is used. Depending on
the board, isolate it by one of these documented board-specific methods:

- desolder the onboard shunt resistor;
- open a provided solder jumper;
- cut the breakout manufacturer's documented link.

Do not cut an unidentified trace. After modification, use the board schematic
and a multimeter to confirm that the onboard resistor no longer forms a
low-resistance bridge while each INA219 input still reaches its corresponding
sense terminal.

A typical `0.1 ohm` onboard resistor would carry:

```text
0.075 V / 0.1 ohm = 0.75 A
```

That current would flow through the thin Kelvin leads, fuses, connectors, and
PCB traces. Their voltage drops would corrupt the measurement and change with
temperature. Isolating the differential shunt does **not** disable INA219 bus
voltage measurement.

### ESP32-C3 I2C warning

The inherited defaults use GPIO8 for SDA and GPIO9 for SCL. Both are ESP32-C3
strapping pins, and many I2C modules include pull-up resistors. ESPHome therefore
emits warnings for these pins. Validate reliable cold boot, reset, and recovery
with the actual modules attached. If the hardware does not boot reliably, move
I2C to suitable non-strapping pins in
[`packages/battery-config.yaml`](packages/battery-config.yaml) and rebuild.

## Firmware layout

| Path                                                                 | Purpose                                                              |
| -------------------------------------------------------------------- | -------------------------------------------------------------------- |
| [`battery-monitor.yaml`](battery-monitor.yaml)                       | Canonical package composition and ESP32 framework                    |
| [`packages/battery-config.yaml`](packages/battery-config.yaml)       | Battery, shunt, polarity, pin, timing, and rule defaults             |
| [`packages/connectivity.yaml`](packages/connectivity.yaml)           | Wi-Fi, encrypted API, OTA, time, and diagnostics                     |
| [`packages/measurement.yaml`](packages/measurement.yaml)             | INA219 measurements, sign normalization, freshness, and current mode |
| [`packages/soc.yaml`](packages/soc.yaml)                             | Coulomb counting, checkpoints, validity, and manual anchors          |
| [`packages/soc-rules.yaml`](packages/soc-rules.yaml)                 | Hysteretic conditions and durable event journal                      |
| [`packages/display.yaml`](packages/display.yaml)                     | SSD1306 pages                                                        |
| [`include/battery_monitor_types.h`](include/battery_monitor_types.h) | Fixed-record persistence and rule helpers                            |
| [`assets/fonts/`](assets/fonts/)                                     | Vendored Roboto Mono font and OFL license                            |
| [`tests/battery_monitor_helpers_test.cpp`](tests/battery_monitor_helpers_test.cpp) | Deterministic host tests for production helpers         |
| [`tests/validate_repository.py`](tests/validate_repository.py)       | Clean-checkout, asset, fixture, credential, and Markdown checks       |
| [`.github/workflows/esphome.yaml`](.github/workflows/esphome.yaml)   | Pinned clean-checkout validation and ESP32-C3 compilation            |

The three `shunt*.yaml` files are obsolete prototypes retained only until the
canonical firmware completes hardware acceptance. Do not use them for a new
installation.

## Configure secrets

Copy the valid-shaped example and replace every value:

```sh
cp secrets.example.yaml secrets.yaml
```

Generate credentials where indicated in the fixture:

```sh
openssl rand -base64 32  # Home Assistant API encryption key
openssl rand -base64 24  # example source for an OTA password
```

Keep `secrets.yaml` private. It is intentionally ignored by version control.
Never commit real Wi-Fi, API, fallback AP, or OTA credentials.

## Install, validate, and compile

Use the supported ESPHome release:

```sh
python3 -m venv .venv
. .venv/bin/activate
python -m pip install --upgrade pip
python -m pip install "esphome==2026.8.0"
```

Validate and compile the canonical entry point:

```sh
esphome config battery-monitor.yaml
esphome compile battery-monitor.yaml
```

GPIO8/GPIO9 strapping warnings are expected with the default pin assignment;
compiler warnings or missing includes/assets are not.

Run the deterministic repository and production-helper checks locally:

```sh
python3 tests/validate_repository.py
c++ -std=c++17 -Wall -Wextra -Wpedantic -Werror -I. \
  tests/battery_monitor_helpers_test.cpp -o /tmp/battery-monitor-helper-tests
/tmp/battery-monitor-helper-tests
rm -f /tmp/battery-monitor-helper-tests
```

The GitHub Actions workflow repeats both checks from a clean checkout, copies
the valid-shaped example to private `secrets.yaml`, installs exactly ESPHome
2026.8.0, validates the canonical configuration, and compiles the default
ESP32-C3 target. It rejects compiler diagnostics while allowing the separately
documented GPIO8/GPIO9 configuration warnings.

## Flash and update

For the first installation, connect the ESP32-C3 over USB and run:

```sh
esphome run battery-monitor.yaml
```

Select the serial device when prompted. After the node has joined Wi-Fi and has
been added to Home Assistant, later runs can use the discovered network target
for encrypted OTA updates. An explicit host can also be selected:

```sh
esphome run battery-monitor.yaml --device battery-monitor.local
```

Do not flash while the wiring is in an unsafe or partially assembled state.

## First start and SOC behavior

On a genuinely fresh device:

1. Voltage, current, power, and current-mode measurements start immediately.
2. Trusted `State of Charge` is unavailable and `SOC Valid` is off.
3. The unbounded diagnostic estimate and remaining amp-hours continue to track.
4. Rule conditions remain unavailable, and crossing events are suppressed.
5. Establish a known endpoint, then press `Set Battery Full` or
   `Set Battery Empty` in Home Assistant.

Only anchor full or empty when the battery is independently known to be at that
endpoint under the battery/BMS manufacturer's procedure. The buttons do not
detect or enforce safe cell voltage.

The accumulator uses:

```text
delta_Ah = current_A * elapsed_seconds / 3600
```

Charging efficiency is applied only to positive charging current. Negative
discharging current removes amp-hours without that compensation. SOC is:

```text
SOC_percent = remaining_Ah / rated_capacity_Ah * 100
```

It is deliberately not clamped to 0-100%. Values outside the configurable
plausibility range remain visible and are marked suspect because they are useful
evidence of capacity, offset, gain, or anchoring error.

Changing `Rated Battery Capacity` rescales remaining amp-hours so the current
unbounded SOC percentage is preserved. `Invalidate SOC` removes trust and makes
the authoritative SOC/rule conditions unavailable while preserving the
diagnostic estimate.

## Persistence and abrupt power loss

Live amp-hours update at measurement cadence and are not written to flash every
second. With the defaults:

- the live value is staged into a checkpoint every 60 seconds;
- restored records are checked for changes every 1 second;
- physical preference writes are coalesced for 5 seconds;
- manual anchors, capacity edits, and invalidation stage a checkpoint
  immediately, but the physical flash write is still delayed by polling and
  coalescing;
- graceful shutdown stages another checkpoint, but unplugging power cannot rely
  on a graceful-shutdown callback.

An abrupt power loss can therefore lose approximately the latest checkpoint
interval plus scheduling/polling/write-coalescing delay: roughly 66 seconds with
the defaults. The exact loss depends on when power fails. After reboot, the first
valid current sample establishes a timing baseline; firmware does not integrate
across downtime.

## Calibration and acceptance

Before relying on the monitor:

1. Compare `Battery Voltage` with a trusted multimeter at the battery terminals
   over the expected voltage range.
2. Compare charging and discharging current with a suitable calibrated DC clamp
   meter or reference instrument at several controlled currents.
3. Confirm positive current while charging and negative current while
   discharging. If the verified raw sign is reversed, change
   `current_polarity_multiplier` to `-1.0` and rebuild; do not change downstream
   formulas independently.
4. With no intentional current, observe raw current and shunt voltage long
   enough to characterize offset and drift. The charging/idle/discharging
   deadband affects labels only; it does not discard current from integration.
5. Exercise manual anchors, reboot restoration, stale-sensor behavior, OLED
   pages, rule thresholds, hysteresis clearing, API outage, and reconnect replay.

The complete bench and hardware checklist is in
[`docs/commissioning.md`](docs/commissioning.md). Do not remove the obsolete
prototypes or enable real charger/load actions until that checklist passes.

## Home Assistant

Persistent condition entities are authoritative. Crossing events are
notification/reconciliation hints and can be missed during an extended outage.
On each native-API reconnect, firmware replays only the most recent durable
record with the original sequence and a `replay` label; consumers must
deduplicate by sequence.

The complete entity contract, event payload, limitations, and safe example
automations are documented in
[`docs/home-assistant.md`](docs/home-assistant.md).

## Design plans

- [`plans/01-stabilization-plan.md`](plans/01-stabilization-plan.md) defines the
  approved MVP and acceptance criteria.
- [`plans/02-evolution-plan.md`](plans/02-evolution-plan.md) covers calibration,
  qualified endpoints, capacity learning, improved measurement hardware, and
  future fail-safe local contactor control.
