# Troubleshooting

Use this guide when setup or commissioning does not match the documented
behavior. It is organized by visible symptom rather than firmware subsystem.

> **Safety first:** Never troubleshoot by bypassing a BMS or fuse, placing an
> ammeter across a battery or shunt, intentionally shorting a production battery,
> or changing energized high-current/Kelvin wiring. De-energize and isolate the
> installation before continuity or resistance tests. Obtain qualified help when
> battery fault current, energized work, or installation rules exceed your
> competence.

This project is a prototype. The complete physical monitor has not passed the
project commissioning checklist, and no real charger or load automation has been
validated. Record failed checks rather than adjusting the documentation's
acceptance criteria after seeing a result.

## Evidence to collect first

Before changing configuration, save enough evidence to distinguish a software,
network, sensor, wiring, or power problem:

- exact ESP32-C3 and INA219 breakout revisions;
- firmware project/version and ESPHome version;
- relevant substitutions from
  [`../packages/battery-config.yaml`](../packages/battery-config.yaml);
- complete boot and failure-period ESPHome logs with credentials removed;
- I2C scan results;
- Home Assistant entity states and actual registry IDs;
- battery, BMS, charger/load, fuse, and supply state at the time;
- trusted reference-instrument readings and accuracy specifications; and
- what changed immediately before the symptom.

Never publish Wi-Fi passwords, the native API encryption key, the fallback AP
password, or the OTA password.

## ESPHome validation or compilation fails

**Symptom:** `esphome config battery-monitor.yaml` or compilation exits with an
error, reports a missing local file, or emits an unexpected compiler warning.

**Normal or abnormal:** GPIO8/GPIO9 strapping warnings are expected with the
default pins. Missing includes/assets, schema errors, component errors, and
compiler warnings are abnormal.

**Likely causes:**

- ESPHome is not the supported `2026.8.0` release;
- the virtual environment is inactive;
- `secrets.yaml` is absent or contains invalid-shaped values;
- a package, helper, font, or generated file is missing;
- a local edit introduced invalid YAML or C++; or
- the checkout is incomplete.

**Safe checks:**

1. Run `esphome version` in the intended virtual environment.
2. Run `python3 tests/validate_repository.py`.
3. Compare private secret keys—not values—with
   [`../secrets.example.yaml`](../secrets.example.yaml).
4. Re-run configuration validation and read the first error before subsequent
   cascading messages.
5. Inspect local changes rather than deleting them blindly.

**Corrective action:** install the pinned release, restore missing tracked files,
correct the specifically reported schema/path error, or revert only the known bad
edit. Do not weaken compiler or repository checks to make a failure disappear.

**Record:** full command, tool version, first error, and changed files.

**Stop/escalate:** do not flash a build that did not validate and compile cleanly.

## Device does not join Wi-Fi

**Symptom:** the node repeatedly fails to connect, remains absent from the router,
or never becomes reachable at `battery-monitor.local`.

**Normal or abnormal:** a brief connection delay is normal. Persistent failure is
abnormal; the device may expose its password-protected fallback hotspot.

**Likely causes:**

- incorrect SSID/password;
- unsupported band or signal conditions;
- network isolation, filtering, or DHCP failure;
- unstable ESP32 supply or reset loop;
- strapping-pin/I2C pull-up interaction; or
- the wrong firmware/board profile.

**Safe checks:**

1. Read serial logs over USB in a low-energy test setup.
2. Verify the SSID and password in private `secrets.yaml` without posting them.
3. Check whether the configured fallback hotspot appears.
4. Confirm stable input and 3.3 V rails with suitable instruments.
5. Check router DHCP/association logs and Wi-Fi reachability.
6. Test repeated cold boot with the actual I2C modules attached.

**Corrective action:** correct private credentials, network reachability, or the
verified power/boot issue. If default I2C pins cause unreliable boot, use the
strapping-pin procedure below.

**Record:** serial connection messages, reset reason if available, power
measurements, router evidence, and board revision.

**Stop/escalate:** if the controller resets, browns out, heats, or has unstable
rails, disconnect its protected supply and resolve the hardware fault before
continued operation.

## Fallback setup hotspot appears

**Symptom:** a Wi-Fi network named `Battery Monitor Setup` appears.

**Normal or abnormal:** this is the configured recovery behavior when the node
cannot join the primary Wi-Fi network. It is evidence of a primary connectivity
problem, not proof that the battery monitor is otherwise commissioned.

**Likely causes:** incorrect primary credentials, unavailable access point,
insufficient signal, network policy, or DHCP failure.

**Safe checks:** inspect serial logs and primary network status first. Connect to
the fallback hotspot only with the private configured password and from an
appropriate local device.

**Corrective action:** restore primary network connectivity or correct private
credentials, then confirm the fallback hotspot disappears after reconnection.

**Record:** reason shown in logs and the corrected network condition. Do not
record the password.

**Stop/escalate:** never leave a placeholder or publicly known fallback password
on an installed device.

## Home Assistant discovery or API encryption fails

**Symptom:** Home Assistant does not discover the node, manual addition cannot
connect, or the integration rejects the encryption key.

**Normal or abnormal:** discovery can be blocked across subnets; manual addition
by reachable IP/host is valid. Disabling native API encryption is not a remedy.

**Likely causes:**

- Home Assistant cannot route to the node;
- mDNS discovery does not cross the network boundary;
- the OTA password was entered instead of the API key;
- whitespace or the wrong private key was copied;
- firmware and `secrets.yaml` no longer match; or
- the node is offline/resetting.

**Safe checks:**

1. Confirm the device IP in serial logs/router data.
2. Verify Home Assistant can route to that IP.
3. Use manual ESPHome integration addition with the IP if discovery is absent.
4. Copy `api_encryption_key`—not `ota_password`—from private `secrets.yaml`.
5. Confirm the running firmware was built with that same key.
6. Review node and Home Assistant logs without exposing credentials.

**Corrective action:** fix routing/discovery, enter the correct key, or safely
reflash firmware with the intended private secrets. Keep encryption enabled.

**Record:** addresses, sanitized logs, firmware build identity, and whether manual
addition succeeds.

**Stop/escalate:** do not publish or commit the key while asking for support. If a
key was exposed, rotate it and reflash safely.

## OTA update fails

**Symptom:** a network update cannot authenticate, cannot reach the host, or
disconnects during transfer.

**Normal or abnormal:** the first installation normally uses USB. OTA requires a
stable online node and the private OTA password.

**Likely causes:** wrong OTA password, stale hostname/IP, network isolation, weak
Wi-Fi, insufficient supply stability, or a reboot during upload.

**Safe checks:** confirm Device Online, current IP, Wi-Fi signal, stable supply,
and the target selected by ESPHome. Read update logs.

**Corrective action:** correct the target/password or network/power issue. If OTA
remains unreliable, return to USB flashing in a safe low-energy setup rather than
repeatedly interrupting network updates.

**Record:** update stage, error, signal, IP, and reset evidence.

**Stop/escalate:** do not flash while monitor wiring is unsafe, partly assembled,
or undergoing energized modification.

## INA219 is missing from the I2C scan

**Symptom:** address `0x40` (or the deliberately configured alternative) is absent,
measurements do not appear, or logs report INA219 communication errors.

**Normal or abnormal:** absent INA219 data is abnormal. Firmware should mark
measurements unavailable rather than fabricate zero.

**Likely causes:** wrong address, SDA/SCL swap, missing logic power/ground, voltage
incompatibility, damaged breakout, bus short, pull-up/strapping interaction, or a
bad connector.

**Safe checks:**

1. Disconnect and make the battery installation safe.
2. Verify module address selection against the exact board schematic.
3. Inspect SDA, SCL, logic power, and common ground continuity.
4. Confirm the breakout supply voltage and logic compatibility.
5. Inspect for solder bridges or damage from onboard-shunt modification.
6. Test in a current-limited low-energy setup.

**Corrective action:** repair the verified low-voltage wiring/configuration fault,
replace a damaged module, or move I2C pins if the default assignment is proven
unreliable. Re-run the complete affected commissioning checks.

**Record:** scan output, voltages, continuity results, address straps, module
revision, and modification method.

**Stop/escalate:** do not probe or alter exposed energized shunt/battery wiring.
Do not rely on voltage, current, power, or SOC while INA219 communication is bad.

## OLED is missing, blank, unreadable, or rotated

**Symptom:** `0x3C` is absent, the display remains blank, content is upside down,
or operation causes I2C/boot errors.

**Normal or abnormal:** rotation can differ between module installations and is a
configuration issue. Missing display communication or boot instability is a
fault, though monitoring can continue only after measurement health is separately
verified.

**Likely causes:** wrong address, incompatible supply, SDA/SCL/ground wiring,
display damage, wrong rotation, excessive bus capacitance/pull-ups, or strapping
interaction.

**Safe checks:** de-energize before wiring inspection; verify address and voltage;
confirm the INA219 remains visible; inspect boot logs; compare
`oled_rotation` with physical orientation.

**Corrective action:** fix the verified wiring/address/power issue or adjust
`oled_rotation` and rebuild. Do not hide a shared I2C fault by merely disabling
the display.

**Record:** scan output, supply voltage, display revision, orientation, and logs.

**Stop/escalate:** disconnect a hot, damaged, or incorrectly powered module.

## ESP32-C3 cold boot or reset is unreliable

**Symptom:** the controller boots only after repeated resets, behaves differently
with I2C modules attached, or enters an unintended boot mode.

**Normal or abnormal:** ESPHome warnings for default GPIO8/GPIO9 are expected;
unreliable operation is not acceptable.

**Likely causes:** pull-ups on strapping pins, unstable supply/inrush, wrong board
profile/revision assumptions, USB/power interaction, or wiring faults.

**Safe checks:** use a low-energy test setup; record repeated cold starts with and
without each I2C module; verify rail behavior; inspect module pull-ups and exact
ESP32-C3 revision documentation.

**Corrective action:** choose suitable non-strapping SDA/SCL pins verified for the
actual board, update `i2c_sda_pin` and `i2c_scl_pin` in
[`../packages/battery-config.yaml`](../packages/battery-config.yaml), rebuild, and
repeat firmware/boot/I2C commissioning.

**Record:** test matrix, pin changes, pull-up values where known, rails, and logs.

**Stop/escalate:** do not accept an installation that cannot cold-boot and recover
reliably with its final modules attached.

## Measurements are unavailable or OLED says SENSOR UNAVAILABLE

**Symptom:** Battery Voltage, Current, Power, and current-mode entities become
unavailable; Measurement Healthy turns off; the OLED displays `SENSOR
UNAVAILABLE`.

**Normal or abnormal:** this is the intended safe representation after missing,
non-finite, or stale INA219 samples. It is not a zero-current indication.

**Likely causes:** INA219/I2C failure, sensor power loss, loose connector, bus
interference, controller timing gap, or damaged/protected sense wiring.

**Safe checks:** read logs and I2C state; note which entities failed together;
inspect low-voltage wiring after de-energizing; check `F1`/`F2` continuity only in
a safely isolated circuit; verify sensor power.

**Corrective action:** correct the verified communication, power, connector, or
sense-path fault. When valid samples return, firmware establishes a new timing
baseline and does not integrate across the unavailable interval.

**Record:** failure time/duration, logs, Measurement Healthy transitions, I2C
evidence, and recovery behavior.

**Stop/escalate:** treat current, power, SOC, and rule output as untrusted while
measurements are unavailable. Do not let external equipment infer zero from the
missing state.

## Current sign is reversed

**Symptom:** verified charging current is negative and verified discharging
current is positive.

**Normal or abnormal:** public convention is fixed: positive means charging and
negative means discharging.

**Likely causes:** shunt sense orientation differs from the documented topology,
`VIN+`/`VIN-` are swapped, or the raw sensor sign needs the compile-time polarity
multiplier.

**Safe checks:** use a controlled, independently known low-energy charge/load
direction and a suitable reference instrument. Verify wiring against
[`wiring.md`](wiring.md). Confirm that **every** tested current has the opposite
sign, not merely one noisy near-zero reading.

**Corrective action:** correct incorrect wiring in a de-energized state. If the
verified installation is intentionally opposite at the raw sensor stage, change
only `current_polarity_multiplier` to `-1.0`, rebuild, and repeat sign, power, and
SOC tests. Do not negate individual downstream formulas.

**Record:** reference direction/current, raw and normalized readings, wiring, and
multiplier.

**Stop/escalate:** invalidate SOC and do not rely on rules until polarity is
correct and affected commissioning checks pass.

## Current is nonzero with chargers and loads intended off

**Symptom:** the monitor reports persistent current or Ah drift when the system is
believed idle.

**Normal or abnormal:** real standby loads and monitor consumption can exist. A
small classification deadband changes labels only; it does not remove current
from SOC integration.

**Likely causes:** real parasitic current, INA219/shunt offset, thermal drift,
noise, unwanted current through Kelvin paths/onboard shunt, ground/supply routing,
or reference-instrument limitations.

**Safe checks:** independently prove the intended sources/loads are off; allow
thermal warm-up; record raw current and shunt voltage over time; inspect whether
monitor supply placement accounts for expected consumption; verify onboard-shunt
isolation and no Kelvin bypass current.

**Corrective action:** remove unintended real loads or correct wiring. If measured
sensor offset is unacceptable, record the failed acceptance result. The current
firmware has no zero-offset calibration; increasing the mode deadband will not
correct SOC integration.

**Record:** time, temperature, raw current, shunt voltage, peak-to-peak noise,
drift, and independent current evidence.

**Stop/escalate:** do not hide unacceptable drift by changing labels or applying
an undocumented correction formula.

## Voltage or current disagrees with a reference instrument

**Symptom:** monitor readings differ from a trusted multimeter, clamp meter, load,
or source beyond the predeclared tolerance.

**Normal or abnormal:** small error within the complete agreed error budget can
be acceptable. Tolerance chosen after seeing the result is not valid acceptance.

**Likely causes:** sensor/shunt tolerance, wrong shunt resistance, Kelvin pickup
location, lead/contact drop, onboard-shunt bypass, offset/gain error, clipping,
timing mismatch, instrument error, or incorrect voltage sampling expectations.

**Safe checks:** compare simultaneous stable readings at several safe points in
both current directions; verify reference calibration/accuracy; inspect direct
Kelvin placement; confirm configured shunt values; check movement/temperature
sensitivity.

**Corrective action:** correct a verified configuration or wiring error and repeat
all affected tests. The stabilization firmware does not implement arbitrary
offset/gain calibration, so unacceptable residual error is a failed acceptance
result, not permission to insert an untested formula.

**Record:** reference and monitor values, errors, tolerance, temperature, wiring,
and instrument specification.

**Stop/escalate:** do not rely on SOC thresholds or control decisions when
measurement error exceeds the intended use's accepted tolerance.

## SOC NOT SET, State of Charge unavailable, or SOC Valid off

**Symptom:** the OLED says `SOC NOT SET`, trusted State of Charge is unavailable,
and SOC Valid is off.

**Normal or abnormal:** normal on a genuinely fresh or explicitly invalidated
device. It is abnormal only if a known valid checkpoint should have restored and
the cause has not been explained.

**Likely causes:** no manual anchor yet, intentional invalidation, fresh/erased
preferences, invalid checkpoint, changed hardware/state, or unresolved
measurement problem.

**Safe checks:** determine whether this is a fresh device; inspect logs for
preference/checkpoint errors; verify measurement health and capacity setting;
review whether someone invalidated SOC deliberately.

**Corrective action:** if the estimate is correctly untrusted, leave it invalid
until a manufacturer-qualified full or empty endpoint is independently
established. Then follow [`home-assistant-setup.md`](home-assistant-setup.md). Do
not press an anchor merely to remove the warning.

**Record:** preference history, capacity, measurement state, and endpoint evidence.

**Stop/escalate:** never treat unavailable SOC as zero, and never bypass BMS or
charger/load protection to manufacture an endpoint.

## SOC is suspect, above 100%, or below 0%

**Symptom:** SOC Suspect turns on, SOC Status becomes `suspect`, or the unbounded
value leaves the configured plausibility range.

**Normal or abnormal:** firmware deliberately does not clamp SOC. The value is
evidence of accumulated offset, gain, capacity, efficiency, anchoring, or missing
sample error; it is not automatically proof that the battery is physically
overfull or empty.

**Likely causes:** incorrect/manual endpoint, capacity mismatch, charging
efficiency assumption, current offset/gain error, reversed sign, lost integration
around power failure, real battery-capacity difference, or wiring faults.

**Safe checks:** verify measurement health/sign/accuracy; review anchor history,
capacity edits, outages, and checkpoint exposure; compare battery/BMS state using
manufacturer-approved methods.

**Corrective action:** correct and commission the underlying measurement/config
issue. If SOC is no longer trustworthy, use Invalidate SOC and re-anchor only at
a qualified endpoint. Do not clamp the value or automatically set full/empty from
the percentage alone.

**Record:** unbounded SOC, remaining Ah, current offset, capacity, anchors,
outages, and independent battery evidence.

**Stop/escalate:** SOC cannot override cell-level BMS, voltage, or temperature
protection. Investigate any actual battery-limit indication independently.

## SOC rule configuration is invalid

**Symptom:** SOC Rule Configuration Valid turns off; rule-engine readiness turns
off; authoritative rule conditions become unavailable.

**Normal or abnormal:** this is intended fail-closed observability after invalid
runtime settings.

**Likely cause:** levels no longer satisfy strict ordering:

```text
Stop Load < Capacity Warning < Stop Charge
```

**Safe checks:** inspect all three levels and hysteresis values. Values must be
finite; hysteresis must be nonnegative (the UI ranges already enforce this).

**Corrective action:** restore strict level ordering and sensible hysteresis.
Firmware reconstructs conditions without fabricating a crossing event.

**Record:** before/after values and validity/readiness transitions.

**Stop/escalate:** external automations must treat unavailable conditions as no
authoritative command, not as an implicit clear state.

## SOC Rule Engine Ready remains off

**Symptom:** rule configuration is valid, but the engine does not become ready.

**Normal or abnormal:** readiness remains off until all prerequisites are met.

**Likely causes:** SOC Valid is off, Measurement Healthy is off, settings are
invalid/missing, or startup/settings baselining has not completed.

**Safe checks:** inspect SOC Valid, Measurement Healthy, SOC Rule Configuration
Valid, SOC Status, and logs. Wait through the configured startup-suppression
window after boot or settings changes.

**Corrective action:** fix the specific failed prerequisite. Do not work around
readiness in a Home Assistant automation.

**Record:** prerequisite entity timeline and relevant logs.

**Stop/escalate:** charger/load automation must require readiness and ignore
unknown/unavailable rule conditions.

## A rule condition is unavailable

**Symptom:** Stop Charge Requested, Capacity Warning Active, or Stop Load Requested
is unavailable rather than on/off.

**Normal or abnormal:** intended while the rule engine is unready due to invalid
SOC, stale measurements, invalid settings, or startup baselining.

**Safe checks:** diagnose readiness prerequisites rather than forcing a condition
state.

**Corrective action:** restore trustworthy inputs. Conditions reconstruct from
current state/latches when ready; recovery does not fabricate a crossing event.

**Record:** readiness and condition transitions.

**Stop/escalate:** never map unavailable to “safe to charge” or “safe to load.”
Choose and document independent hardware-safe failure behavior.

## Home Assistant receives replayed or duplicate-looking events

**Symptom:** an SOC event appears after reconnect, or a live event and reconnect
delivery carry the same sequence.

**Normal or abnormal:** firmware replays the latest durable event after native API
connection, preserving its original sequence and marking `replay: "true"`. This
is not a new threshold crossing.

**Likely causes:** expected reconnect replay, an automation that does not
deduplicate by sequence, or multiple consumers/connections.

**Safe checks:** compare `sequence`, `rule`, and `replay`; inspect persistent rule
condition and readiness; verify the automation's stored last-consumed sequence.

**Corrective action:** use the sequence-aware example in
[`home-assistant.md`](home-assistant.md) and compare sequence by inequality rather
than requiring a numerically larger value, so uint32 wrap is handled. Drive
equipment from persistent conditions, not event delivery.

**Record:** complete sanitized event payload, persistent condition, readiness,
and helper value.

**Stop/escalate:** do not interpret replay as a second battery action or use
transient event count as a safety state.

## Behavior during Wi-Fi or Home Assistant outage

**Symptom:** Device Online is off and Home Assistant stops updating, while the
local display and integration continue.

**Normal or abnormal:** intended. Firmware does not reboot solely because Wi-Fi or
Home Assistant is unavailable. Local measurement, SOC integration, display, and
condition evaluation continue while required sensor data remains healthy.

**Likely causes:** network/API outage, Home Assistant restart, routing issue, or
controller connectivity problem.

**Safe checks:** distinguish network loss from sensor/power loss using the OLED,
serial logs, router data, and Measurement Healthy after reconnection.

**Corrective action:** restore network/API connectivity and reconcile persistent
condition state. The device retains only the latest crossing event, not a lossless
offline queue.

**Record:** outage interval, local display behavior, condition state after
reconnect, and replay sequence.

**Stop/escalate:** network-dependent control is supplementary. Independent BMS,
charger, load, and hardwired protection must remain effective throughout outage.

## SOC rolled back after abrupt monitor power loss

**Symptom:** restored Remaining Capacity/SOC is older than the last value seen
before the monitor lost power.

**Normal or abnormal:** bounded rollback is an explicit consequence of coalesced
flash writes. With defaults, exposure is roughly 66 seconds: 60-second checkpoint
cadence plus polling/write-coalescing and scheduling delay.

**Likely causes:** monitor power failed after the last physical checkpoint; a
graceful shutdown callback did not run; or preferences/checkpoint data is invalid.

**Safe checks:** compare current, outage timing, and restored Ah with the bound:

```text
approximate maximum lost Ah = abs(current_A) * 66 / 3600
```

Review logs for checkpoint/preference errors and distinguish expected rollback
from a missing/invalid record.

**Corrective action:** document expected bounded loss. Investigate losses outside
the accepted bound, recurring monitor-supply interruptions, or preference errors.
Do not reduce flash intervals without analyzing flash wear and retesting.

**Record:** current, last observed/checkpoint times, outage duration, restored Ah,
and calculated bound.

**Stop/escalate:** invalidate/re-anchor SOC if rollback or repeated outages make
the estimate untrustworthy.

## Open Kelvin fuse or suspected thin-lead fault

**Symptom:** measurement health fails, readings become implausible/unavailable, or
inspection indicates an open `F1`/`F2` or damaged Kelvin conductor.

**Normal or abnormal:** an open protection device is a measurement fault. Replacing
it without finding the cause can recreate a high-energy fault.

**Likely causes:** short to common negative/metalwork, damaged insulation,
incorrect fuse/holder, wiring error, onboard shunt still connected, mechanical
damage, or unsuitable DC interrupt capability.

**Safe checks:** isolate and de-energize the battery system using the reviewed
procedure; verify absence of hazardous voltage; inspect routing and damage;
perform continuity/resistance tests only in the safe isolated state; review the
fuse/holder and prospective-fault-current selection record.

**Corrective action:** identify and correct the root cause, replace protection only
with the reviewed specification, and repeat wiring, measurement, and commissioning
checks. Never bridge the fuse or substitute unverified wire/foil.

**Record:** failed part, opening conditions, fault evidence, protected wire,
ratings, corrective work, and retest results.

**Stop/escalate:** if a fuse operated under fault, a holder is damaged, insulation
is compromised, or interrupt capability is uncertain, do not re-energize until a
qualified review confirms the repair and protection design.

## If the symptom is not listed

Keep the monitor in an observe-only, non-control role. Preserve logs and measured
evidence, mark the applicable item failed in
[`commissioning.md`](commissioning.md), and obtain review before changing safety
boundaries or acceptance criteria. The architecture and entity semantics are
documented in [`architecture.md`](architecture.md) and
[`home-assistant.md`](home-assistant.md).
