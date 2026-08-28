# ESPHome high-side battery monitor

An ESP32-C3 and INA219 monitor for a 4S LiFePO4 battery, an external
500 A / 75 mV shunt, an SSD1306 OLED, and Home Assistant.

> **Prototype only:** Firmware and documentation exist, but the complete physical
> monitor has not passed the commissioning checklist. No real charger or load
> automation has been validated. Do not treat this repository as a field-proven
> battery controller or an accepted construction design.

The canonical ESPHome entry point is
[`battery-monitor.yaml`](battery-monitor.yaml). It provides:

- high-side battery-voltage and bidirectional-current measurement;
- signed current and power: positive means charging, negative means discharging;
- manually anchored amp-hour state-of-charge estimation;
- explicit measurement health, SOC trust, and plausibility diagnostics;
- hysteretic Stop Charge, Capacity Warning, and Stop Load conditions;
- persistent condition state plus sequence-tagged reconnect event replay;
- a local SSD1306 status display; and
- encrypted Home Assistant API, protected OTA, and a fallback setup hotspot.

## Safety boundary

This firmware estimates state of charge. It is **not** a BMS, fuse, disconnect,
charger protection, load protection, or safety-rated control system.

Before connecting the monitor to a battery installation, provide independently
functioning:

- BMS cell-voltage, temperature, and overcurrent protection;
- a battery main fuse with suitable DC voltage and interrupt ratings;
- charger overvoltage and fault protection;
- load undervoltage and fault protection;
- correctly rated cables, bus bars, lugs, insulation, enclosure, strain relief,
  and disconnects; and
- a protected DC/DC supply rated for the full battery range and transients.

**Never route battery or load current through the INA219 breakout, ESP32-C3,
OLED, a PCB relay, or thin sense wire.** Main current flows only through the
external shunt and installation-rated high-current components.

The two Kelvin sense leads connect directly to the external-shunt sense points,
are fused individually near those energized taps, and never carry monitor-supply
current. The INA219 breakout's onboard differential shunt must be isolated using
a method documented for the exact board revision.

A 300 Ah LiFePO4 battery can deliver destructive fault current. De-energize and
isolate the battery before changing conductors. Use appropriately qualified help,
procedures, instruments, and personal protective equipment. Read
[`docs/wiring.md`](docs/wiring.md) and
[`docs/fuse-selection.md`](docs/fuse-selection.md) before choosing or connecting
hardware.

Home Assistant, Wi-Fi, crossing events, and the monitor itself are supplementary
observability/automation paths. Independent battery and equipment protection must
remain effective while any of them is offline, stale, rebooting, or failed.

## Choose your path

| You are...                                     | Start here                                           | Then use                                                                                                                                                                                           |
| ---------------------------------------------- | ---------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| New to ESPHome or battery monitoring           | [`docs/getting-started.md`](docs/getting-started.md) | [`docs/wiring.md`](docs/wiring.md), [`docs/home-assistant-setup.md`](docs/home-assistant-setup.md), and qualified supervision for battery work                                                     |
| An experienced Home Assistant/ESPHome hobbyist | [Quick start](#quick-start)                          | [`docs/home-assistant-setup.md`](docs/home-assistant-setup.md), [`docs/troubleshooting.md`](docs/troubleshooting.md), and [`docs/commissioning.md`](docs/commissioning.md)                         |
| An engineer, integrator, or maintainer         | [`docs/architecture.md`](docs/architecture.md)       | [`docs/home-assistant.md`](docs/home-assistant.md), [`docs/fuse-selection.md`](docs/fuse-selection.md), [`docs/commissioning.md`](docs/commissioning.md), and [`CONTRIBUTING.md`](CONTRIBUTING.md) |

The beginner guide explains the software path, but it does not make high-current
battery construction beginner-safe. Stop and obtain qualified assistance when
protection, conductor, enclosure, fault-current, or code decisions exceed your
competence.

## Supported default hardware

| Item                    | Default                                              |
| ----------------------- | ---------------------------------------------------- |
| Battery profile         | 4S LiFePO4, 300 Ah rated capacity                    |
| Controller              | ESP32-C3 SuperMini                                   |
| ESPHome board profile   | `esp32-c3-devkitm-1`                                 |
| Current/voltage monitor | INA219 breakout, onboard shunt electrically isolated |
| External shunt          | 500 A / 75 mV (`0.00015 ohm`)                        |
| Display                 | SSD1306 128x64 I2C OLED                              |
| INA219 / OLED addresses | `0x40` / `0x3C`                                      |
| I2C pins                | GPIO8 SDA, GPIO9 SCL                                 |
| Supported ESPHome       | `2026.8.0`                                           |

Hardware and battery defaults are centralized in
[`packages/battery-config.yaml`](packages/battery-config.yaml). Review that file
before flashing a different installation.

The physical controller is an ESP32-C3 SuperMini. The generic board profile is
used because common SuperMini variants generally lack a dedicated PlatformIO
definition. Confirm the flash size, USB mode, regulator/input pin, GPIO labels,
and schematic for the exact revision; SuperMini-branded boards do not necessarily
share one layout.

GPIO8 and GPIO9 are ESP32-C3 strapping pins, and I2C modules often include
pull-ups. Repeatedly test cold boot, reset, and recovery with the actual modules.
Move I2C to verified non-strapping pins and rebuild if the default hardware is
unreliable.

## Monitor connection overview

```text
BATTERY B+ ── external shunt ── F_MAIN ── protected DC+ bus
              │            │
              F2           F1
              │            │
           K_BAT         K_BUS ──┐
                                 │
protected DC+ bus ── F3 ── VMON+ ├── SOC monitor
protected DC- / BMS P- ─── GND ──┘
```

Exactly four implemented conductors cross the monitor boundary: `K_BUS`,
`K_BAT`, `VMON+`, and `GND`. The complete monitor-only wiring, breakout
modification, fuse roles, I2C connections, de-energized checks, and diagrams are
in [`docs/wiring.md`](docs/wiring.md).

[![Detailed high-side battery-monitor schematic](hardware/battery-monitor-schematic.svg)](hardware/battery-monitor-schematic.svg)

The drawing is an electrical schematic, not an approved cable, PCB, enclosure,
fuse, physical-placement, or code-compliance design.

## Quick start

Use [`docs/getting-started.md`](docs/getting-started.md) for explanations and
success criteria. The compact path is:

1. Read the visible safety boundary and complete the de-energized checks in
   [`docs/wiring.md`](docs/wiring.md).
2. Review [`packages/battery-config.yaml`](packages/battery-config.yaml) against
   the exact as-built hardware.
3. Copy the secrets fixture and replace every placeholder:

   ```sh
   cp secrets.example.yaml secrets.yaml
   ```

4. Create the supported environment and build:

   ```sh
   python3 -m venv .venv
   . .venv/bin/activate
   python -m pip install --upgrade pip
   python -m pip install "esphome==2026.8.0"
   python3 tests/validate_repository.py
   esphome config battery-monitor.yaml
   esphome compile battery-monitor.yaml
   ```

5. Flash over USB in a safe low-energy setup:

   ```sh
   esphome run battery-monitor.yaml
   ```

6. Verify reliable boot, INA219 `0x40`, OLED `0x3C`, finite measurements, and no
   unexplained errors.
7. Add the device using
   [`docs/home-assistant-setup.md`](docs/home-assistant-setup.md).
8. Complete and retain the applicable acceptance record in
   [`docs/commissioning.md`](docs/commissioning.md) before relying on measurements
   or connecting real supplementary control actions.

GPIO8/GPIO9 strapping warnings are expected with the default assignment.
Compiler warnings, missing assets/includes, schema errors, or unexplained I2C
errors are not.

Keep `secrets.yaml` private. It is intentionally ignored by version control.
Never commit Wi-Fi, native API, fallback AP, or OTA credentials.

## Fresh-device SOC behavior

On a genuinely fresh device:

1. voltage, current, power, and current mode can begin immediately;
2. trusted State of Charge is unavailable and SOC Valid is off;
3. the diagnostic unbounded estimate and Remaining Capacity can still track;
4. rule conditions remain unavailable and crossing events are suppressed; and
5. the OLED displays `SOC NOT SET`.

This is intentional: an unanchored value must not masquerade as a trusted empty
battery.

> **Manual-anchor warning:** Set Battery Full and Set Battery Empty do not inspect
> cell voltage, temperature, BMS state, charger state, current stability, or
> manufacturer endpoint criteria. Use them only when the battery is independently
> known to be at a manufacturer-qualified full or empty endpoint.

The first-use procedure and guarded dashboard layout are in
[`docs/home-assistant-setup.md`](docs/home-assistant-setup.md).

<details>
<summary>How unbounded SOC works</summary>

Firmware estimates remaining charge by adding measured charging current and
subtracting measured discharging current over time. Positive charge current is
adjusted by Charging Efficiency; negative discharge current is not.

```text
delta_Ah = effective_current_A * elapsed_seconds / 3600
SOC_percent = remaining_Ah / rated_capacity_Ah * 100
```

SOC is deliberately not clamped to 0–100%. An out-of-range value remains visible
and turns on SOC Suspect because it can reveal capacity, offset, gain, efficiency,
anchoring, or power-loss error. It does not replace cell-level BMS evidence.

Changing Rated Battery Capacity rescales Remaining Capacity to preserve the
current unbounded percentage. Invalidate SOC preserves the diagnostic estimate
while making trusted SOC and authoritative rule conditions unavailable.

</details>

<details>
<summary>Persistence and abrupt monitor power loss</summary>

Live Ah updates at measurement cadence but is not written to flash every second.
With defaults, firmware stages a checkpoint every 60 seconds, polls restored
records every 1 second, and coalesces physical preference writes for 5 seconds.

An abrupt monitor power loss can therefore lose approximately 66 seconds of
recent accounting plus normal scheduling uncertainty. At constant current:

```text
approximate maximum lost Ah = abs(current_A) * 66 / 3600
```

The first valid current sample after reboot establishes a new timing baseline;
firmware never integrates across downtime. See
[`docs/architecture.md`](docs/architecture.md) for checkpoint details.

</details>

## Home Assistant authority model

Persistent rule-condition entities are the source of truth for supplementary
charger/load reconciliation. Crossing events are one-shot notification hints and
can be missed while Home Assistant is offline. Automations must require SOC Rule
Engine Ready and handle unknown/unavailable conditions explicitly.

No real charger/load automation has been validated for this prototype. Test any
adapted example with logging or a harmless test switch before connecting external
equipment, and preserve independent safe behavior for network, monitor, sensor,
driver, and Home Assistant failures.

<details>
<summary>Reconnect replay and event deduplication</summary>

Firmware stores only the latest crossing record. On each native API connection,
it republishes that record with the original nonzero sequence and a replay label.
A replay is not a new crossing. Consumers deduplicate equal sequences and
reconcile the current persistent condition.

The journal is not a lossless queue: several crossings during a long outage
cannot all be replayed. The complete contract and conservative automation
examples are in [`docs/home-assistant.md`](docs/home-assistant.md).

</details>

## Documentation map

### Build and operation

- [`docs/getting-started.md`](docs/getting-started.md) — prerequisites, build,
  flash, first-boot checks, and success criteria;
- [`docs/wiring.md`](docs/wiring.md) — monitor-only wiring, protection boundaries,
  diagrams, and de-energized inspection;
- [`docs/home-assistant-setup.md`](docs/home-assistant-setup.md) — discovery,
  encryption, starter dashboard, runtime settings, and safe SOC anchoring;
- [`docs/troubleshooting.md`](docs/troubleshooting.md) — symptom-oriented safe
  diagnosis and stop-work criteria; and
- [`docs/glossary.md`](docs/glossary.md) — plain-language terminology.

### Engineering references

- [`docs/architecture.md`](docs/architecture.md) — package/data flow, measurement
  validity, SOC, persistence, rules, events, and implemented/deferred scope;
- [`docs/home-assistant.md`](docs/home-assistant.md) — complete entity/event
  contract and advanced automation examples;
- [`docs/fuse-selection.md`](docs/fuse-selection.md) — DC protection inputs and
  selection record; and
- [`docs/commissioning.md`](docs/commissioning.md) — hardware and integration
  acceptance procedure.

### Maintainers

- [`CONTRIBUTING.md`](CONTRIBUTING.md) — checks, document ownership, licensing,
  and schematic workflow;
- [`Taskfile.yml`](Taskfile.yml) — recurring local command interface; and
- [`tests/validate_repository.py`](tests/validate_repository.py) — deterministic
  clean-checkout and documentation invariants.

<details>
<summary>Firmware package layout</summary>

| Path                                                                 | Purpose                                                              |
| -------------------------------------------------------------------- | -------------------------------------------------------------------- |
| [`battery-monitor.yaml`](battery-monitor.yaml)                       | Canonical composition and ESP32 framework                            |
| [`packages/battery-config.yaml`](packages/battery-config.yaml)       | Hardware, timing, profile, and rule defaults                         |
| [`packages/connectivity.yaml`](packages/connectivity.yaml)           | Wi-Fi, encrypted API, OTA, time, and diagnostics                     |
| [`packages/measurement.yaml`](packages/measurement.yaml)             | INA219 measurements, sign normalization, freshness, and current mode |
| [`packages/soc.yaml`](packages/soc.yaml)                             | Unbounded Ah/SOC, checkpoints, validity, and manual anchors          |
| [`packages/soc-rules.yaml`](packages/soc-rules.yaml)                 | Hysteretic conditions and latest-event journal                       |
| [`packages/display.yaml`](packages/display.yaml)                     | SSD1306 status pages                                                 |
| [`include/battery_monitor_types.h`](include/battery_monitor_types.h) | Fixed-record persistence and pure rule/SOC helpers                   |

</details>

<details>
<summary>System context and design records</summary>

The wider conceptual battery/BMS/charger/inverter/ATS view is available in
[`hardware/battery-system-installation.svg`](hardware/battery-system-installation.svg).
It is not a construction-ready installation plan. Its unusual ATS labeling,
future local charger-enable route, and engineering limitations are explained in
[`docs/wiring.md`](docs/wiring.md).

</details>

## License and acknowledgement

Project-authored source code and documentation are licensed under
**GPL-3.0-only**. See [`LICENSE`](LICENSE).

Copyright (C) 2026 primetalk contributors.

Third-party assets retain their own licenses. The vendored Roboto Mono font is
licensed under the SIL Open Font License in
[`assets/fonts/OFL.txt`](assets/fonts/OFL.txt).

The project idea was inspired by
[jurgen2005/esphome-shunt](https://github.com/jurgen2005/esphome-shunt). This
repository is intended as an independent implementation; no source code or
documentation from that unlicensed repository is known to be included, and this
project does not claim license or permission on its behalf.
