# Migrating to ESP8266

Use [`../battery-monitor-esp8266.yaml`](../battery-monitor-esp8266.yaml) for an
ESP8266 alternative to the default ESP32-C3. It includes the same six packages:
INA219 measurement, SOC integration, hysteretic rules and event replay, OLED,
encrypted Home Assistant API, OTA, and the fallback setup hotspot.

> **Prototype status:** This target has not been commissioned on physical
> ESP8266 hardware. A successful build does not establish runtime stability,
> power-loss recovery, or measurement accuracy. Complete the checks below and
> [`commissioning.md`](commissioning.md) before relying on it.

The independent protection requirements and four installation conductors in
[`wiring.md`](wiring.md) still apply. De-energize and isolate the installation
before changing wiring. The MCU migration does not change the external shunt,
Kelvin leads, breakout-shunt isolation, fuses, or BMS requirements.

## Choose the board

The entry point defaults to a **4 MB D1 mini** (`d1_mini`). A **4 MB NodeMCU
1.0 / ESP-12E** can be selected by changing `esp8266_board` to `nodemcuv2`.
Confirm the actual flash size and pinout of the board revision; the board name
alone is not proof that a clone matches the profile. See the PlatformIO profiles
for [D1 mini](https://docs.platformio.org/en/latest/boards/espressif8266/d1_mini.html)
and [NodeMCU](https://docs.platformio.org/en/latest/boards/espressif8266/nodemcuv2.html).

ESP-01 and other 1 MB boards are outside this guide's target: exposed pins,
firmware size, and OTA space need a separate assessment. The default ESP32-C3
remains the option with more memory headroom.

## Configuration differences

| Setting | Default ESP32-C3 | ESP8266 entry point |
| --- | --- | --- |
| Configuration | `battery-monitor.yaml` | `battery-monitor-esp8266.yaml` |
| Board profile | `esp32-c3-devkitm-1` | `d1_mini` |
| Device name | `battery-monitor` | `battery-monitor-esp8266` |
| SDA / SCL | GPIO0 / GPIO1 | GPIO4 / GPIO5 |
| SOC checkpoint interval | 60 seconds | 15 minutes |
| Flash write coalescing interval | 5 seconds | 60 seconds |
| API connection limit | Framework default | 2 |
| Flash restoration | Platform default | Explicitly enabled |

The root-level substitutions in the ESP8266 entry point override the shared
defaults. Review battery capacity, efficiency, shunt resistance, polarity, and
rule levels in [`../packages/battery-config.yaml`](../packages/battery-config.yaml).
Put ESP8266-specific overrides in the ESP8266 entry point so they do not change
the ESP32-C3 target. The `esp32_board` substitution in the shared package is
unused by the ESP8266 entry point.

Keep measurement sampling at 1 second. The longer checkpoint interval only
changes how often SOC is staged for persistence; live measurement, integration,
rule evaluation, and display updates continue at their existing rates.

## Rewire the controller connections

Connect both I2C modules in parallel to the following nets:

| Signal | D1 mini / NodeMCU | INA219 | SSD1306 OLED |
| --- | --- | --- | --- |
| SDA | GPIO4, usually labeled D2 | SDA | SDA |
| SCL | GPIO5, usually labeled D1 | SCL | SCL |
| Logic power | Compatible regulated 3.3 V rail | VCC | VCC |
| Reference | GND | GND | GND |

GPIO numbers and board labels are different: **GPIO4 is D2**, not D4. Do not
reuse the ESP32-C3 GPIO0/GPIO1 arrangement. GPIO0 affects ESP8266 boot mode and
GPIO1 is normally the serial transmit pin. Verify the board labels against its
schematic. The shared bus remains at 400 kHz, with INA219 address `0x40` and OLED
address `0x3C`. See [ESPHome I2C documentation](https://esphome.io/components/i2c/).

Keep SDA/SCL pull-ups at 3.3 V and check combined pull-up strength when both
modules have resistors fitted. Follow the selected controller's supply-input
ratings: a pin labeled `3V3` requires regulated 3.3 V, while `5V`/`VIN` behavior
depends on the exact board. Never apply the battery bus directly to either.
Provide a supply that remains stable during Wi-Fi transmit bursts, and avoid
unintended USB/external-supply backfeeding.

The committed schematics depict the ESP32-C3. Use their installation and sensing
topology with the controller pin substitution above; they are not ESP8266 board
pinout drawings.

## Persistence and flash wear

`restore_from_flash: true` is required for saved SOC, settings, rule latches,
and the latest-event journal to survive removal of power. Do not disable it as
a memory optimization. See the [ESPHome ESP8266 platform documentation](https://esphome.io/components/esp8266/).

ESPHome's ESP8266 preferences implementation commits changed preferences by
erasing and rewriting a flash sector. Its flash-restoration buffer is 512 bytes
in the pinned ESPHome 2026.8.0; the project's three records and eight saved numeric settings
use approximately 144 bytes including per-record checksums, before framework
preferences. Check the implementation and allocation behavior again when
changing ESPHome versions or adding restored entities. See the
[ESPHome preferences source](https://api-docs.esphome.io/esp8266_2preferences_8cpp_source).

With continuously changing SOC, the original 60-second checkpoint schedule can
cause about 1,440 sector erases per day. The ESP8266 default of 15 minutes reduces
SOC-driven commits to roughly 96 per day. Settings changes, rule crossings, and
other saved state can add writes. This is not a flash-lifetime guarantee: consult
the actual flash device's endurance specification and expected service life.
The 60-second flash interval coalesces pending writes; it does not write unchanged
data on a timer.

The trade-off is recovery accuracy. Sudden power loss can lose approximately
15 minutes of integration **plus up to about 1 minute of write delay and the
restore poll delay**. At a constant 10 A, 16 minutes is about 2.67 Ah, or 0.89%
of a 300 Ah battery. The monitor cannot account for current while it is unpowered.
A restored SOC estimate may therefore need a new known full/empty anchor.
Recent settings, latch state, and event sequence changes may also be lost before
their pending writes reach flash. This ordinary checkpoint loss window assumes
the last flash commit remains readable. Power failure during sector erase/write
can invalidate previously saved preferences too: this backend does not provide
an atomic redundant copy. In that case expect defaults, invalid SOC requiring a
new anchor, and potentially a reset event sequence. Event replay remains a
latest-record journal, not a lossless event queue.

Change `soc_checkpoint_interval` and `flash_write_interval` only after choosing
an acceptable recovery window and write budget. If frequent durable checkpoints
are essential, external nonvolatile storage needs a separate implementation;
this entry point does not add one. Graceful-shutdown checkpointing already
exists, but cannot be relied on during abrupt power removal.

## Build and flash

Run from the repository root using the project's pinned ESPHome release:

```sh
python3 -m venv .venv
. .venv/bin/activate
python -m pip install "esphome==2026.8.0"
```

If `secrets.yaml` does not exist, copy `secrets.example.yaml` to it. Replace all
placeholder credentials before flashing; do not overwrite existing private
secrets or commit them. Both entry points use the same secret names.

```sh
python3 tests/validate_repository.py
esphome config battery-monitor-esp8266.yaml
esphome compile battery-monitor-esp8266.yaml
```

Inspect the resolved configuration for GPIO4/GPIO5, flash restoration, 15-minute
checkpoints, and the two-connection API limit. Treat warnings as issues to
investigate. Record the compiler's RAM/flash report; a successful link alone
does not prove sufficient heap or OTA space during operation.

On 2026-09-10, the full D1 mini target compiled without compiler warnings using
ESPHome 2026.8.0 and fixture credentials: **48,188 / 81,920 bytes RAM (58.8%)**
and **528,225 / 1,044,464 bytes application flash (50.6%)**. These are build-time
figures, not measured free heap. The flash denominator is the application's
build limit, not the board's full 4 MB. Rebuild after configuration changes and
complete the runtime/OTA checks below.

Flash the new board over USB in a safe low-energy setup. Select its actual serial
port, for example:

```sh
esphome run battery-monitor-esp8266.yaml --device /dev/ttyUSB0
```

On macOS the port is typically under `/dev/cu.*`. Subsequent updates must use
the ESP8266 entry point and the ESP8266 device's address. Never upload an ESP32
firmware image to the ESP8266, or the reverse.

Both targets are included in the
[firmware CI workflow](../.github/workflows/esphome.yaml). CI compiles the default
D1 mini profile; changing to another board still requires your own build and
hardware acceptance checks.

## Home Assistant migration

The new device name allows a bench setup alongside the old monitor. The shared
packages preserve entity meanings, but a replacement MCU has a different device
identity: do not assume Home Assistant entity IDs, device-bound automations, or
stored preferences transfer automatically.

Record the old capacity, efficiency, thresholds, and hysteresis values. Add the
ESP8266 through [`home-assistant-setup.md`](home-assistant-setup.md), restore those
settings, and establish SOC only at a verified anchor. Review dashboards and
automation references before enabling supplementary control actions. A new
event journal starts a new sequence history; follow the deduplication and state
reconciliation contract in [`home-assistant.md`](home-assistant.md).

## Acceptance and memory checks

In addition to [`commissioning.md`](commissioning.md), record these results for
the actual board, modules, power supply, and firmware version:

- Repeated cold boot and reset with both I2C modules attached; both addresses
  found and no intermittent boot-mode or I2C faults.
- Stable voltage/current samples and OLED updates while Home Assistant is
  connected; no watchdog resets or growing measurement gaps.
- Available heap and largest free block over an extended run, during Wi-Fi/API
  reconnects, with a logging client, and during OTA. The optional
  [ESPHome debug component](https://esphome.io/components/debug/) can expose heap
  diagnostics; repeat checks after removing temporary instrumentation.
- An actual OTA update with Home Assistant connected, followed by a normal boot.
- Restoration of capacity, efficiency, all six rule settings, SOC, latches, and
  the journal after power cycling. First allow a full checkpoint/write window,
  then test interruptions before the next commit to establish the loss window.
  In a bench setup, also verify conservative recovery from invalid preferences
  after an interrupted commit: defaults and unanchored SOC must not be mistaken
  for a restored, accepted battery state.
- Wi-Fi and Home Assistant outages: local measurement/rules keep running and
  reconnect replay reconciles with current entity state.

The API limit permits Home Assistant plus one logging client. Additional clients
may be refused. Do not raise the limit without checking heap; see the
[native API memory guidance](https://esphome.io/components/api/).

If memory is inadequate, first investigate the runtime heap report. Possible
reductions include removing unused diagnostic entities, limiting font glyphs,
or removing the fallback AP/captive portal when its recovery path is unnecessary.
Disabled-by-default entities still occupy firmware resources. Keep API encryption
and measurement-validity checks. Dropping the OLED package is a larger optional
trade-off; the supplied entry point retains the full display.
