# Getting started

This guide takes a new user from a clean checkout to a safely flashed monitor
that is ready to add to Home Assistant. It does **not** replace the wiring,
fuse-selection, or commissioning procedures.

> **Prototype status:** Firmware and documentation exist, but the complete
> physical monitor has not passed the commissioning checklist. No real charger
> or load automation has been validated. Treat all measurements and controls as
> unaccepted until you complete and record the applicable checks in
> [`commissioning.md`](commissioning.md).

## Decide whether to proceed

This project is most approachable for someone who already knows how to:

- flash an ESP32 with ESPHome;
- identify board pins and I2C addresses from manufacturer documentation;
- make and inspect low-voltage electronic connections;
- use a multimeter safely;
- distinguish a thin sense conductor from a high-current battery conductor; and
- stop and obtain qualified help when battery fault-current, fuse, conductor,
  enclosure, or applicable-code decisions exceed their competence.

The documented 4S LiFePO4 battery can deliver destructive fault current. A
beginner may use this guide to understand the software, but should not construct
or modify the energized battery installation without appropriately qualified
supervision.

## Required independent protection

Before the monitor is connected to a battery installation, the installation must
have independently functioning:

- BMS cell-voltage, temperature, and overcurrent protection;
- a main battery fuse with suitable DC voltage and interrupt ratings;
- charger overvoltage and fault protection;
- load undervoltage and fault protection;
- correctly rated cables, bus bars, lugs, insulation, enclosure, and strain
  relief; and
- a protected DC/DC supply suitable for the complete battery-voltage range and
  credible transients.

The monitor, ESPHome, Wi-Fi, Home Assistant, and the OLED are not primary safety
devices. Review [`fuse-selection.md`](fuse-selection.md) before selecting any
fuse or holder.

## What you need

### Default electronic modules

| Item                    | Repository default                    | Important qualification                                                                                       |
| ----------------------- | ------------------------------------- | ------------------------------------------------------------------------------------------------------------- |
| Controller              | ESP32-C3 SuperMini                    | Confirm the exact revision, pin labels, USB mode, flash size, and supply input.                               |
| Current/voltage monitor | INA219 breakout                       | Its onboard differential shunt must be isolated by a board-documented method before using the external shunt. |
| External shunt          | 500 A / 75 mV                         | Use dedicated fused Kelvin leads; main current flows through the shunt, never through the INA219 board.       |
| Display                 | SSD1306 128x64 I2C OLED               | Default address is `0x3C`; confirm voltage and logic compatibility.                                           |
| Logic supply            | Protected battery-to-3.3 V DC/DC path | Select for actual input range, transients, inrush, temperature, and load.                                     |

This is not a complete bill of materials. Installation-specific fuses, holders,
wire, connectors, protection, disconnects, enclosure, and reference instruments
must be selected for the actual system.

### Software and tools

- Python 3 with virtual-environment support;
- ESPHome `2026.8.0`;
- a data-capable USB cable;
- a terminal;
- a trusted high-impedance multimeter;
- an appropriately rated current reference for commissioning; and
- the exact datasheets and schematics for the selected controller, INA219
  breakout, display, converter, shunt, fuses, and holders.

## 1. Inspect and configure the hardware definition

Read [`wiring.md`](wiring.md) before assembling or energizing anything. Complete
its de-energized checks before battery connection.

Then review [`../packages/battery-config.yaml`](../packages/battery-config.yaml).
The settings are split into two groups:

- **Common installation settings:** device identity, battery capacity default,
  charging efficiency, shunt rating, polarity, I2C pins, addresses, and display
  rotation.
- **Advanced behavior settings:** measurement timing, stale-data timing,
  checkpoint cadence, flash coalescing, plausibility limits, startup suppression,
  and rule defaults. Leave these unchanged unless you understand and will test
  their effects.

The default external-shunt calculation is:

```text
75 mV / 500 A = 0.00015 ohm
```

Do not copy the default resistance or current range to a different shunt. Derive
and verify the values from that shunt's manufacturer data.

**Success criterion:** every hardware-defining substitution matches the exact
as-built module, shunt, and wiring plan, and no advanced value was changed without
a test plan.

## 2. Create private secrets

From the repository root, copy the valid-shaped fixture:

```sh
cp secrets.example.yaml secrets.yaml
```

Replace every placeholder in the private `secrets.yaml`. Generate the native API
encryption key and a strong OTA-password source with:

```sh
openssl rand -base64 32
openssl rand -base64 24
```

The API encryption key must decode to exactly 32 bytes. The fallback access-point
password must meet ESPHome's minimum length. Never commit the private secrets
file or paste the OTA password into Home Assistant's ESPHome integration prompt.

**Success criterion:** the private file contains the intended Wi-Fi, fallback AP,
native API, and OTA credentials, and version control still ignores it.

## 3. Install the supported ESPHome release

Create an isolated Python environment:

```sh
python3 -m venv .venv
. .venv/bin/activate
python -m pip install --upgrade pip
python -m pip install "esphome==2026.8.0"
```

Confirm the installed release:

```sh
esphome version
```

**Success criterion:** ESPHome reports `2026.8.0` from the active virtual
environment.

## 4. Validate before flashing

Run the repository checks and validate the canonical entry point:

```sh
python3 tests/validate_repository.py
esphome config battery-monitor.yaml
esphome compile battery-monitor.yaml
```

GPIO8/GPIO9 strapping-pin warnings are expected with the default pin assignment.
Missing files, schema errors, component errors, or compiler warnings are not
expected. Do not dismiss an unfamiliar warning merely because one documented
warning is allowed.

**Success criterion:** repository validation, ESPHome configuration validation,
and compilation all complete successfully, with only the understood default-pin
warnings.

## 5. Flash in a low-energy setup

Do the first flash over USB before connecting the monitor to an unsafe or
partially assembled battery circuit:

```sh
esphome run battery-monitor.yaml
```

Select the serial device when prompted. Observe the logs during boot.

Expected first-boot evidence includes:

- the node joins the configured Wi-Fi network, or the password-protected fallback
  access point appears if it cannot;
- the I2C scan finds the INA219 at `0x40` and OLED at `0x3C`, unless deliberately
  changed;
- the board repeatedly cold-boots and resets with the actual I2C pull-ups
  attached;
- there are no missing-component, font, include, preference-schema, or I2C
  communication errors; and
- the OLED shows measurements or a specific unavailable-state message rather than
  fabricated zeroes.

If the board is unreliable with GPIO8/GPIO9, stop and follow the strapping-pin
entry in [`troubleshooting.md`](troubleshooting.md). Do not proceed on the
assumption that an intermittent boot will correct itself.

**Success criterion:** the node boots reliably, both expected I2C devices are
present, and logs contain no unexplained errors.

## 6. Understand the fresh state

On a genuinely fresh device:

- voltage, current, power, and current mode can begin immediately;
- trusted State of Charge is unavailable;
- SOC Valid is off and SOC Status is `not_set`;
- the diagnostic unbounded estimate can still track charge;
- the SOC rule engine is not ready;
- rule conditions are unavailable; and
- the OLED displays `SOC NOT SET`.

This is intentional. Zero would look like a real empty battery, so firmware does
not publish an unanchored estimate as trusted SOC.

Do not press Set Battery Full or Set Battery Empty merely to make the warning
disappear. Those controls are appropriate only when the battery is independently
known to be at a manufacturer-qualified endpoint.

## 7. Add the device to Home Assistant

Continue with [`home-assistant-setup.md`](home-assistant-setup.md). That guide
covers discovery, API encryption, initial entity checks, a starter dashboard, and
safe manual anchoring.

Later network updates can use a discovered target or an explicit host:

```sh
esphome run battery-monitor.yaml --device battery-monitor.local
```

## 8. Commission before relying on the result

Complete and retain the applicable record in
[`commissioning.md`](commissioning.md). It includes voltage and current comparison,
offset/drift characterization, sign verification, persistence tests, rule
hysteresis, stale-sensor recovery, OLED behavior, and Home Assistant outage
behavior.

Do not connect real charger or load actions merely because the firmware compiles
or Home Assistant shows plausible values. This prototype has no published
completed acceptance record, and its Home Assistant controls are supplementary,
non-safety automation only.

## Where to go next

- [`wiring.md`](wiring.md) — monitor-only wiring and de-energized inspection;
- [`home-assistant-setup.md`](home-assistant-setup.md) — first-use Home Assistant
  path;
- [`troubleshooting.md`](troubleshooting.md) — symptom-oriented diagnosis;
- [`glossary.md`](glossary.md) — plain-language terminology;
- [`architecture.md`](architecture.md) — optional firmware internals;
- [`home-assistant.md`](home-assistant.md) — advanced entity/event contract and
  automation patterns;
- [`fuse-selection.md`](fuse-selection.md) — protection selection inputs; and
- [`commissioning.md`](commissioning.md) — acceptance procedure.
