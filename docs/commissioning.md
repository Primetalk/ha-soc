# Commissioning and hardware acceptance

Use this procedure after assembling the canonical monitor from
[`battery-monitor.yaml`](../battery-monitor.yaml). Record actual observations,
instruments, tolerances, and pass/fail decisions.

> **Prototype only:** Firmware and documentation exist, but the complete physical
> monitor has not passed this checklist and no real charger or load automation
> has been validated. Treat every applicable item as unpassed until its result is
> recorded. Do not enable real charger/load control until every applicable
> monitor and control-path acceptance item passes.

Select and record `F_MAIN`, `F1`, `F2`, and `F3` using the separate
[fuse-selection guide](fuse-selection.md) before energizing the installation.

## Safety prerequisites

This monitor is not a BMS or battery safety device. Before energizing anything,
confirm that the installation has independently functioning:

- BMS cell-voltage, temperature, and overcurrent protection;
- battery main fuse with an adequate DC interrupt rating;
- charger fault and overvoltage protection;
- load fault and undervoltage protection;
- correctly rated cable, bus bars, lugs, insulation, enclosure, and strain
  relief;
- a protected supply for the ESP32-C3, INA219, and OLED;
- a safe means to isolate the battery and controlled test sources/loads.

Use current-limited bench equipment for early testing where possible. A 300 Ah
battery can supply destructive fault current. De-energize and verify absence of
hazardous voltage before changing conductors. Use qualified assistance and
appropriate protective equipment when the installation requires it.

Do not place an ammeter directly across a battery or shunt. Do not route main
current through the INA219 breakout, ESP32-C3, OLED, thin Kelvin wire, or a PCB
relay.

## Test record

| Field                                                  | Record                                     |
| ------------------------------------------------------ | ------------------------------------------ |
| Date                                                   |                                            |
| Operator                                               |                                            |
| Firmware project/version                               | `primetalk.esphome-shunt` / `0.3.0`        |
| ESPHome version                                        | `2026.8.0`                                 |
| ESP32-C3 board/revision                                |                                            |
| INA219 breakout make/revision                          |                                            |
| Onboard-shunt isolation method                         | desolder / jumper / documented cut / other |
| External shunt make/serial/rating                      | 500 A / 75 mV expected                     |
| Fuse-selection record / design-review reference        |                                            |
| Battery/BMS                                            |                                            |
| Reference multimeter and calibration status            |                                            |
| Reference DC current instrument and calibration status |                                            |
| Agreed voltage tolerance                               |                                            |
| Agreed current tolerance(s)                            |                                            |
| Ambient temperature                                    |                                            |
| Notes/change references                                |                                            |

Choose tolerances before testing based on the complete error budget, reference
instrument accuracy, INA219 range/resolution, shunt tolerance, wiring, expected
operating currents, and the decisions the data will support. Do not select a
tolerance after seeing the result.

## 1. De-energized construction inspection

- [ ] Main current path is charger/load positive bus -> external shunt bus side
      -> external shunt battery side -> battery positive.
- [ ] Battery/common negative is continuous to INA219 and ESP32-C3 ground for
      this non-isolated high-side design.
- [ ] Main battery current cannot pass through the INA219 breakout.
- [ ] Bus-side Kelvin tap connects only to INA219 `VIN+` through its protected
      sense lead.
- [ ] Battery-side Kelvin tap connects only to INA219 `VIN-` through its
      protected sense lead.
- [ ] Each Kelvin lead is fused near the energized shunt tap with a device that
      protects the wire and has a suitable DC interrupt capability.
- [ ] `F1`, `F2`, `F3`, and `F_MAIN` selections record nominal current,
      time-current behavior, DC voltage rating, DC interrupt rating, holder
      rating, protected-wire ampacity, prospective fault current, and maximum
      unfused length as required by the
      [fuse-selection guide](fuse-selection.md).
- [ ] Kelvin terminals are direct shunt sense points, not remote high-current
      lugs with load-dependent voltage drop.
- [ ] Thin leads are paired/routed together, mechanically protected, and cannot
      contact high-current conductors or sharp metal.
- [ ] The breakout's onboard shunt has been electrically isolated using a
      board-documented method.
- [ ] Resistance/continuity measurement and the board schematic confirm no
      low-resistance onboard bridge remains between `VIN+` and `VIN-`.
- [ ] The INA219 input pins still connect to their corresponding external sense
      terminals after modification.
- [ ] INA219 and OLED supply voltages match their board ratings and ESP32-C3
      logic levels.
- [ ] Battery and common-mode voltages remain within all IC, breakout, supply,
      connector, and insulation ratings.
- [ ] Main fuse, BMS, charger protection, and load protection are installed and
      independently verified.

Record inspection issues and corrective work:

```text

```

## 2. Firmware and first boot

Before connecting to the battery, create private
[`secrets.yaml`](../secrets.example.yaml) values and run:

```sh
esphome config battery-monitor.yaml
esphome compile battery-monitor.yaml
```

- [ ] Validation succeeds using ESPHome 2026.8.0.
- [ ] Compilation succeeds without compiler warnings or errors.
- [ ] Only the understood GPIO8/GPIO9 strapping warnings remain with default
      pins.
- [ ] Firmware is flashed over USB with the monitor in a safe test setup.
- [ ] Serial logs show I2C devices at INA219 `0x40` and OLED `0x3C` (or the
      deliberately configured alternatives).
- [ ] There are no missing-component, I2C communication, font, include, or
      preference-schema errors.
- [ ] The board cold-boots and resets reliably with actual I2C pull-ups attached.
- [ ] The fallback AP uses the private configured password.
- [ ] Home Assistant discovers the node over the encrypted native API.
- [ ] `Device Online` turns on after network and native API connection.

GPIO8 and GPIO9 are ESP32-C3 strapping pins. If cold boot/reset is unreliable,
move I2C to verified non-strapping pins in
[`packages/battery-config.yaml`](../packages/battery-config.yaml), rebuild, and
repeat this section.

## 3. Fresh-state behavior

For this test, use a genuinely fresh preference state or a dedicated test board;
do not erase production preferences without a recovery plan.

- [ ] Voltage/current measurements begin without an SOC anchor.
- [ ] `SOC Valid` is off.
- [ ] Trusted `State of Charge` is unavailable, not zero.
- [ ] `SOC Status` is `not_set`.
- [ ] The diagnostic unbounded estimate and remaining Ah are visible.
- [ ] `SOC Rule Engine Ready` is off.
- [ ] Stop Charge, Capacity Warning, and Stop Load conditions are unavailable.
- [ ] No crossing event is emitted during startup or first measurement
      stabilization.
- [ ] OLED shows `SOC NOT SET` and never presents missing measurement data as
      zero.

Observed result:

```text

```

## 4. Voltage comparison

Measure directly at the battery terminals with a trusted high-impedance
multimeter while observing `Battery Voltage`. Keep loading stable long enough to
compare simultaneous readings.

| Test point/condition            | Reference V | INA219 V | Error V | Error % | Tolerance | Pass |
| ------------------------------- | ----------: | -------: | ------: | ------: | --------: | ---- |
| Low expected operating voltage  |             |          |         |         |           |      |
| Mid-range/rest voltage          |             |          |         |         |           |      |
| High expected operating voltage |             |          |         |         |           |      |

- [ ] All tested voltage errors meet the predeclared tolerance.
- [ ] Voltage remains stable when display/network activity changes.
- [ ] Battery voltage corresponds to the battery-side shunt terminal (`VIN-`),
      not the charger/load bus side.
- [ ] Any systematic offset or gain error is recorded for the future calibration
      phase rather than hidden by an undocumented formula.

## 5. Zero-current offset, noise, and drift

With chargers and loads intentionally off and after thermal warm-up, log raw
current and shunt voltage. Verify independently that no meaningful standby path
is flowing before treating the result as zero-current error.

| Time from power-up | Ambient/board temperature | Raw current A | Shunt mV | Notes |
| ------------------ | ------------------------: | ------------: | -------: | ----- |
| 1 minute           |                           |               |          |       |
| 5 minutes          |                           |               |          |       |
| 15 minutes         |                           |               |          |       |
| 30 minutes         |                           |               |          |       |

- [ ] Peak-to-peak noise is recorded.
- [ ] Warm-up drift is recorded.
- [ ] Long-term apparent Ah drift is estimated from the observed current offset.
- [ ] `Battery Idle` is on inside the configured deadband.
- [ ] `Battery Charging` and `Battery Discharging` are off while idle.
- [ ] Crossing either side of the deadband changes only mode classification;
      current inside the deadband is still integrated and is not silently set to
      zero.

The MVP does not implement zero-offset calibration. If error is unacceptable,
do not compensate by increasing the mode deadband: that would change labels but
not coulomb-counting input. Record the issue for the calibration/evolution work.

## 6. Current sign, gain, and power

Use a current-limited source/load and an appropriately rated calibrated DC
current reference. Test both directions at several safe magnitudes that exercise
the intended operating range without exceeding any device rating.

| Direction/condition   | Reference A | Monitor A | Error A | Error % | Voltage V | Monitor W | Expected V*A W | Pass |
| --------------------- | ----------: | --------: | ------: | ------: | --------: | --------: | -------------: | ---- |
| Low charge            |             |           |         |         |           |           |                |      |
| Medium charge         |             |           |         |         |           |           |                |      |
| Higher safe charge    |             |           |         |         |           |           |                |      |
| Low discharge         |             |           |         |         |           |           |                |      |
| Medium discharge      |             |           |         |         |           |           |                |      |
| Higher safe discharge |             |           |         |         |           |           |                |      |

- [ ] Charging current is positive.
- [ ] Discharging current is negative.
- [ ] Signed power has the same sign as normalized current.
- [ ] Signed power agrees with battery voltage multiplied by normalized current
      within rounding/timing tolerance.
- [ ] Current errors meet the predeclared tolerance in both directions.
- [ ] Reading does not change materially when Kelvin leads/connectors are gently
      disturbed; any movement-sensitive result is corrected before proceeding.
- [ ] No Kelvin fuse, holder, lead, connector, trace, or breakout shunt carries
      measurable bypass current or heats under test.

If every verified current has the opposite sign, change
`current_polarity_multiplier` in
[`packages/battery-config.yaml`](../packages/battery-config.yaml) from `1.0` to
`-1.0`, rebuild, and repeat this section. Do not invert individual downstream
entities or SOC formulas.

## 7. Manual anchors and unbounded SOC

Use a controlled test state or battery endpoint that is independently known.
The firmware buttons do not determine whether cell voltage is safe or an
endpoint is truly qualified.

- [ ] Set the Home Assistant `Rated Battery Capacity` to the test value.
- [ ] Press `Set Battery Full`; remaining Ah equals rated capacity, trusted SOC
      is exactly 100%, and `SOC Valid` turns on.
- [ ] Press `Set Battery Empty`; remaining Ah is zero, trusted SOC is exactly 0%,
      and `SOC Valid` remains on.
- [ ] After a full anchor, integrate controlled net charge and confirm SOC can
      exceed 100% without clamping.
- [ ] After an empty anchor, integrate controlled net discharge and confirm SOC
      can fall below 0% without clamping.
- [ ] Outside the configured plausibility range, `SOC Suspect` turns on and
      `SOC Status` becomes `suspect` while the numeric value remains visible.
- [ ] Press `Invalidate SOC`; trusted SOC and rule conditions become unavailable,
      the diagnostic estimate remains visible, and no event is emitted.
- [ ] Re-anchor and confirm conditions are reconstructed without a synthetic
      crossing event.

Record controlled Ah and observed changes:

| Start state  | Reference net Ah | Expected remaining Ah/SOC | Observed | Pass |
| ------------ | ---------------: | ------------------------- | -------- | ---- |
| Full anchor  |                  |                           |          |      |
| Empty anchor |                  |                           |          |      |
| Above 100%   |                  |                           |          |      |
| Below 0%     |                  |                           |          |      |

## 8. Rated-capacity preservation

This verifies that a runtime capacity edit preserves the current unbounded SOC
ratio and remains consistent across preference writes/reboot.

1. Establish a known valid SOC away from 0% and 100% (for example 50%).
2. Record rated capacity, remaining Ah, and unbounded SOC.
3. Change `Rated Battery Capacity` to a different safe test value.
4. Record the immediate result.
5. Wait longer than checkpoint polling plus flash coalescing, reboot normally,
   and record the restored result.

| Stage                   | Rated Ah | Remaining Ah | SOC % | Pass |
| ----------------------- | -------: | -----------: | ----: | ---- |
| Before edit             |          |              |       |      |
| Immediately after edit  |          |              |       |      |
| After controlled reboot |          |              |       |      |

- [ ] Immediate SOC percentage is unchanged within display precision.
- [ ] Remaining Ah scales by `new_capacity / old_capacity`.
- [ ] Controlled reboot restores the same unbounded SOC ratio.
- [ ] No threshold crossing event is fabricated by the capacity edit or reboot.

## 9. Checkpoint and reboot behavior

- [ ] Anchor a valid non-boundary SOC and allow at least one full checkpoint and
      flash-coalescing interval.
- [ ] Perform a controlled restart; remaining Ah and validity restore correctly.
- [ ] The first current sample after boot establishes timing and does not
      integrate the reboot interval.
- [ ] Boot inside each rule's hysteresis band preserves/reconstructs the previous
      latch without a false event.
- [ ] `Discarded Integration Gaps` increments when an intentionally induced
      sample interval exceeds the accepted maximum.
- [ ] A controlled sensor/debugger gap does not add fabricated Ah.

Abrupt power-loss test (perform only where safely designed):

1. Apply a stable, independently measured current.
2. Record the most recent known checkpoint window and wall-clock time.
3. Remove only the monitor's protected low-voltage supply without disturbing the
   safe battery current path.
4. Restore power and compare expected versus restored Ah.

| Current A | Outage point relative to checkpoint | Expected maximum lost Ah | Observed lost Ah | Pass |
| --------: | ----------------------------------- | -----------------------: | ---------------: | ---- |
|           |                                     |                          |                  |      |

With defaults, documented accounting exposure is roughly 66 seconds: 60-second
checkpoint cadence plus approximately 1-second restored-record polling and
5-second write coalescing/scheduling. At constant current `I`, the approximate
bound is `abs(I) * 66 / 3600 Ah`. Timing is not a hard real-time guarantee, so
allow and document measurement uncertainty. A graceful shutdown callback must
not be assumed during abrupt power removal.

## 10. Rule configuration and hysteresis

Use a controlled test capacity/current so thresholds can be crossed safely.
Record the exact sequence and SOC around each boundary.

Default expected behavior:

| Rule             | Assert boundary |               Clear/rearm boundary |
| ---------------- | --------------: | ---------------------------------: |
| Stop Charge      |   upward at 95% | at or below 93% with 2% hysteresis |
| Capacity Warning | downward at 40% | at or above 42% with 2% hysteresis |
| Stop Load        | downward at 30% | at or above 32% with 2% hysteresis |

- [ ] After startup suppression, `SOC Rule Engine Ready` turns on only with valid
      SOC, fresh measurements, and valid rule configuration.
- [ ] Stop Charge asserts once at/above its upward boundary.
- [ ] It remains asserted inside the hysteresis band and clears/rearms at/below
      its clear boundary.
- [ ] Capacity Warning asserts once at/below its downward boundary.
- [ ] It remains asserted inside the hysteresis band and clears/rearms at/above
      its clear boundary.
- [ ] Stop Load asserts once at/below its downward boundary.
- [ ] It remains asserted inside the hysteresis band and clears/rearms at/above
      its clear boundary.
- [ ] A second assert after a genuine clear/rearm produces exactly one new
      sequence.
- [ ] Editing a threshold recomputes/baselines conditions without a synthetic
      crossing event.
- [ ] Configure an invalid order such as Stop Load >= Capacity Warning;
      `SOC Rule Configuration Valid` and engine readiness turn off and
      authoritative conditions become unavailable.
- [ ] Restore sensible ordering; conditions reconstruct without a false event.
- [ ] Valid but suspect SOC still drives conditions while `SOC Suspect` remains
      visibly on.

| Test                    | Start SOC | End SOC | Condition before/after | Sequence before/after | Pass |
| ----------------------- | --------: | ------: | ---------------------- | --------------------- | ---- |
| Stop Charge assert      |           |         |                        |                       |      |
| Stop Charge clear       |           |         |                        |                       |      |
| Capacity Warning assert |           |         |                        |                       |      |
| Capacity Warning clear  |           |         |                        |                       |      |
| Stop Load assert        |           |         |                        |                       |      |
| Stop Load clear         |           |         |                        |                       |      |
| Invalid ordering        |           |         |                        |                       |      |

## 11. Sensor stale and recovery behavior

Induce I2C/sensor failure only through a safe, low-energy test method that does
not expose energized conductors or defeat required protection.

- [ ] After the configured stale interval, `Measurement Healthy` turns off.
- [ ] Battery voltage, current, power, raw measurement diagnostics, and
      charging/discharging/idle states become unavailable rather than zero.
- [ ] OLED shows `SENSOR UNAVAILABLE` and does not display stale values.
- [ ] SOC rule engine readiness turns off.
- [ ] All authoritative rule conditions become unavailable.
- [ ] No crossing event occurs while measurements are stale.
- [ ] Local SOC integration does not bridge the unavailable interval.
- [ ] After valid samples return, measurement entities recover.
- [ ] Conditions are reconstructed before the engine re-arms.
- [ ] Recovery does not emit a synthetic threshold crossing.

Record failure method, durations, and observed transitions:

```text

```

## 12. OLED acceptance

- [ ] Measurement page shows voltage, signed current, signed power, and
      `CHG`/`LOAD`/`IDLE` mode.
- [ ] Home Assistant/API status changes between `HA:ON` and `HA:off` as expected.
- [ ] SOC page distinguishes not-set from a valid numeric SOC.
- [ ] Unbounded values below 0% and above 100% render without clipping to the
      nominal range.
- [ ] Status/rules page distinguishes `NOT SET`, `SUSPECT`, and `OK`.
- [ ] Rule labels match authoritative condition states while the rule engine is
      ready and show `n/a` while it is unready.
- [ ] Missing sensor data shows the dedicated unavailable page rather than
      fabricated zeros.
- [ ] Font renders all used characters and no external/absent font is requested.
- [ ] Display remains readable in its final orientation and does not cause I2C
      errors or unreliable boot.

## 13. Home Assistant outage, replay, and deduplication

Install or adapt the examples from
[`docs/home-assistant.md`](home-assistant.md). Use a harmless test switch or
logging action before connecting real equipment.

- [ ] Persistent charger/load reconciliation runs on Home Assistant start,
      monitor reconnect, engine-readiness change, and condition change.
- [ ] Automations ignore `unknown`/`unavailable` conditions and require rule
      engine readiness.
- [ ] Crossing notification records a new nonzero sequence.
- [ ] Disconnect Home Assistant/API while keeping the monitor powered; local
      measurement, SOC, OLED, and rule evaluation continue.
- [ ] Cause a safe test crossing while Home Assistant is unavailable.
- [ ] On reconnect, authoritative condition state is available and the latest
      journal record is replayed after the configured delay.
- [ ] Replay carries the original sequence and `replay: "true"`.
- [ ] The deduplication helper prevents a live event and its replay from causing
      duplicate notifications.
- [ ] An unseen replay can recover the latest notification.
- [ ] Testing acknowledges that multiple offline crossings are not a lossless
      queue; only the latest record is retained.
- [ ] No test treats a notification or Home Assistant event as a battery safety
      cutoff.

| Scenario                           | Rule | Sequence | Delivery | Condition reconciled | Notification count | Pass |
| ---------------------------------- | ---- | -------: | -------- | -------------------- | -----------------: | ---- |
| Live crossing                      |      |          | live     |                      |                    |      |
| Reconnect replay of consumed event |      |          | replay   |                      |                    |      |
| Offline crossing then replay       |      |          | replay   |                      |                    |      |

## 14. Final acceptance

- [ ] Every deviation is documented and resolved or explicitly rejected as
      unsafe/unacceptable.
- [ ] Firmware defaults in
      [`packages/battery-config.yaml`](../packages/battery-config.yaml) match the
      as-built hardware.
- [ ] Private credentials are not present in version control or test records.
- [ ] Home Assistant entity IDs used by automations match the registry.
- [ ] Charger/load control interfaces, if any, are low-energy isolated controls
      or suitably rated contactor systems—not PCB routing of battery current.
- [ ] Independent BMS/fuse/charger/load protection remains effective with the
      monitor powered off, disconnected, stale, rebooting, or failed.
- [ ] Canonical firmware passes a clean ESPHome 2026.8.0 validation and compile.
- [ ] The completed record is archived with the hardware/firmware revision.

Final disposition:

| Decision                              | Select |
| ------------------------------------- | ------ |
| Accepted for monitored operation      | [ ]    |
| Accepted with documented restrictions | [ ]    |
| Rejected / corrective work required   | [ ]    |

Approver, date, restrictions, and references:

```text

```
