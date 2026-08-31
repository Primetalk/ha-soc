# Glossary

This glossary gives a plain-language first explanation. Follow the linked guides
for safety requirements and exact behavior.

## Battery and measurement terms

### BMS

**Battery management system.** Independent hardware that monitors cell voltages,
temperature, and current and applies battery protection according to its design.
This monitor is not a BMS and must not replace one. See
[`wiring.md`](wiring.md).

### SOC

**State of charge.** An estimate of how much usable charge remains, normally
shown as a percentage. In this project, trusted SOC begins only after an operator
anchors the estimate at a qualified full or empty endpoint. See
[`home-assistant-setup.md`](home-assistant-setup.md).

### Coulomb counting

Estimating remaining charge by adding measured charging current and subtracting
measured discharging current over time. Despite the name, this project displays
the accumulated result in amp-hours (Ah). Current offset, gain, missing samples,
capacity assumptions, efficiency, and anchoring all affect the estimate. See
[`architecture.md`](architecture.md).

### Unbounded SOC

An SOC estimate that is allowed to go above 100% or below 0% instead of being
silently clipped. Out-of-range values expose calibration, capacity, efficiency,
or anchoring errors that a clipped gauge would hide. They do not prove that the
battery is physically overfull or empty.

### SOC Valid

Whether firmware currently trusts the SOC anchor. On a fresh device it is off.
Manual Set Full/Empty can turn it on; Invalidate SOC turns it off. Valid says an
anchor exists, not that the estimate has passed every accuracy test.

### SOC Suspect

A diagnostic flag indicating that a valid unbounded SOC value is outside the
configured plausibility range. Suspect SOC remains visible so the cause can be
investigated. Independent BMS and voltage/temperature protections remain
authoritative.

### Qualified endpoint

A battery state independently established as genuinely full or empty using the
battery, BMS, and charger/load manufacturer's procedure. The firmware's Set Full
and Set Empty buttons do not measure individual cells or qualify an endpoint.

### Amp-hour (Ah)

A unit of electric charge commonly used for battery capacity. One ampere flowing
for one hour is one amp-hour. Capacity in Ah is not a safe current, cable, or fuse
rating.

### Shunt

A very low resistance placed in the main current path. The small voltage across
it is proportional to current. The default external shunt is 500 A / 75 mV,
equivalent to `0.00015 ohm`. The 500 A label describes measurement range, not an
approved continuous current or fuse size.

### High-side measurement

Measuring current in the battery-positive path rather than the negative return.
In this design the external shunt is between battery positive and the protected
positive system bus. See [`wiring.md`](wiring.md).

### Kelvin connection

A dedicated thin sense lead connected directly to a shunt's designated sense
point so voltage drop in high-current cables and lugs is not included in the
measurement. Kelvin leads carry negligible sensor input current, must be
individually protected, and must never carry monitor-supply or load current.

### Bus side and battery side

The two sides of the external shunt:

- **bus side / `VIN+`** connects toward the protected charger/load positive bus;
- **battery side / `VIN-`** connects toward battery positive and is the INA219
  battery-voltage sampling point.

These labels identify external-shunt sense points, not a main-current route
through the INA219 breakout.

### Current sign convention

The public meaning of signed current and power:

- positive means charging the battery;
- negative means discharging the battery.

All SOC and power calculations consume the same normalized convention.

### Current-state deadband

A small current range around zero used only to label the state as charging,
discharging, or idle without rapid label changes. This project's deadband does
not erase current from coulomb counting.

### Plausibility range

The configurable unbounded-SOC interval considered unsurprising. Leaving it sets
SOC Suspect but does not clamp the value or replace BMS limits. Plausibility
limits are diagnostic assumptions, not battery safety thresholds.

### Stale data

A measurement that has not been refreshed within the configured interval.
Firmware marks stale numeric and mode entities unavailable rather than continuing
to present old values as current.

### Unavailable

An entity state meaning firmware does not currently have a trustworthy value.
Unavailable is not zero, false, empty, or safe. Home Assistant automations must
handle it explicitly.

## Rule and automation terms

### Threshold

The SOC level at which a rule asserts. This project has Stop Charge, Capacity
Warning, and Stop Load thresholds. These are supervisory requests, not BMS or
hardwired cutoff settings.

### Hysteresis

Separate assert and clear points that prevent a condition from rapidly switching
on and off near one threshold. For example, a 95% Stop Charge level with 2%
hysteresis asserts at 95% and clears at 93%.

### Rule latch

The remembered on/off state of a hysteretic condition. Remembering or
reconstructing the latch lets firmware reboot inside a hysteresis band without
arbitrarily reversing the requested state.

### Authoritative condition

A persistent binary-sensor state that represents the current requested action,
such as Stop Charge Requested. Home Assistant should reconcile equipment from
this state, not from a one-shot event alone. “Authoritative” applies within the
monitor's supervisory software contract; it does not outrank BMS or hardware
protection.

### Rule engine readiness

Whether the firmware has all prerequisites required to publish authoritative rule
conditions: valid SOC, fresh measurements, valid settings, and a completed
startup/settings baseline. Conditions are unavailable while the engine is not
ready.

### Crossing event

A one-shot message emitted when an armed rule changes from clear to asserted at a
real threshold crossing. Events are useful for notifications, but can be missed
during an outage.

### Reconciliation

Re-checking the current persistent condition and making the controlled state
match it after Home Assistant starts, an ESPHome node reconnects, readiness
changes, or a condition changes. This prevents a missed event from becoming the
only remembered command.

### Replay

Re-delivery of the latest durable crossing record after the native API connects.
A replay has the original sequence and is labeled as a replay; it is not a new
battery threshold crossing.

### Sequence

A nonzero unsigned number stored with each crossing event. Consumers use it to
recognize that a live event and its replay describe the same record.

### Deduplication

Ignoring repeated delivery of the same sequence so one crossing does not produce
duplicate notifications/actions. Consumers compare for equality/inequality rather
than assuming every valid next sequence is numerically larger, because the
32-bit counter eventually wraps.

## Protection terms

### Fuse nominal current

The current printed on a fuse. It is only one property and does not say that the
fuse can safely interrupt the battery's possible short-circuit current.

### DC voltage rating

The highest direct-current voltage at which the fuse and holder are approved to
operate under specified conditions. An AC-only marking is not evidence of a DC
rating.

### Interrupt or breaking rating

The maximum prospective fault current that a fuse/holder can safely interrupt at
its rated voltage and conditions. It may need to be thousands of amperes even for
a thin sense lead protected by a nominal 0.5 A fuse. See
[`fuse-selection.md`](fuse-selection.md).

### Time-current curve

Manufacturer data showing how long a fuse takes to open at different currents.
A fuse does not open immediately when current exceeds its nominal value.

### Prospective short-circuit current

The current that could flow at a location if a short occurred, considering the
battery/source and complete path impedance. It is an installation-specific input
to fuse and conductor protection design.

## ESPHome and connectivity terms

### ESPHome native API

The encrypted connection used for the ESPHome device to expose entities and
events to Home Assistant. Its encryption key is separate from the OTA password.

### API encryption key

A base64 value representing 32 bytes used to encrypt/authenticate the ESPHome
native API connection. It is private and should not appear in screenshots,
commits, or support logs.

### OTA

**Over the air.** Updating firmware over the network after the first USB flash.
OTA uses its own password and requires a reachable, stable device.

### Fallback access point

A password-protected Wi-Fi hotspot started by the node when it cannot join the
configured primary network. Its appearance is a recovery path and a clue that
primary Wi-Fi failed.

### I2C

A shared low-voltage digital bus using two separate signals, SDA (data) and SCL
(clock). The INA219 and OLED share both signals but have different addresses.

### I2C address

A number identifying one device on the shared I2C bus. Repository defaults are
`0x40` for INA219 and `0x3C` for the OLED.

### Strapping pin

An ESP32 pin whose voltage during reset helps choose a boot mode or startup
configuration. GPIO2, GPIO8, and GPIO9 are ESP32-C3 strapping pins; external I2C
pull-ups on those pins can affect the sampled boot state. The repository therefore
defaults to non-strapping GPIO0/GPIO1. See Espressif's official
[ESP32-C3 GPIO summary](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/api-reference/peripherals/gpio.html#gpio-summary).
Repeated cold-boot testing is still required for the complete hardware.

### Checkpoint

A bounded-cadence persistent copy of live SOC state. Checkpoints limit flash wear
but mean an abrupt monitor power loss can lose recent integration since the last
physical write.

## More detail

- [`getting-started.md`](getting-started.md) — setup path;
- [`wiring.md`](wiring.md) — electrical connections and safety boundary;
- [`home-assistant-setup.md`](home-assistant-setup.md) — first-use controls;
- [`troubleshooting.md`](troubleshooting.md) — symptom-oriented diagnosis;
- [`architecture.md`](architecture.md) — firmware internals; and
- [`home-assistant.md`](home-assistant.md) — advanced integration contract.
