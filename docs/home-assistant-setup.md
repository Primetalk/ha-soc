# First use with Home Assistant

This guide starts after the monitor has been flashed successfully and joined the
configured Wi-Fi network. It covers first adoption, basic observation, and manual
SOC setup. The advanced entity/event contract and automation examples remain in
[`home-assistant.md`](home-assistant.md).

> **Prototype status:** The complete physical monitor has not passed the project
> commissioning checklist. No real charger or load automation has been validated.
> Home Assistant is an observability and supplementary automation layer, not a
> BMS or primary safety system.

## Before adding the device

Confirm all of these first:

- firmware was built from [`../battery-monitor.yaml`](../battery-monitor.yaml)
  using ESPHome `2026.8.0`;
- the serial log shows the node joining the intended Wi-Fi network;
- the INA219 and OLED appear at their configured I2C addresses;
- the private native API encryption key is available from `secrets.yaml`;
- the OTA password is kept private and is **not** substituted for the API key;
- wiring has passed the de-energized checks in [`wiring.md`](wiring.md); and
- independent BMS, fuse, charger, and load protection remains effective without
  the monitor or Home Assistant.

## 1. Add the ESPHome integration

Home Assistant will often discover the node automatically after it joins the
same reachable network.

1. Open **Settings > Devices & services**.
2. Look for a discovered **ESPHome** device named **Battery Monitor**.
3. Select **Configure**.
4. When asked for the encryption key, paste the value of
   `api_encryption_key` from the private `secrets.yaml` file.
5. Complete the integration flow and assign the device to an area if desired.

If discovery does not appear:

1. Open **Settings > Devices & services**.
2. Select **Add integration** and choose **ESPHome**.
3. Enter `battery-monitor.local` or the IP address shown in the serial log/OLED
   diagnostics.
4. Enter the same private API encryption key when prompted.

Do not expose the API key in screenshots, dashboards, issue reports, or committed
configuration. If connection fails, use the Home Assistant/API entry in
[`troubleshooting.md`](troubleshooting.md) rather than disabling encryption.

**Success criterion:** **Settings > Devices & services > ESPHome > Battery
Monitor** shows the device as connected and lists its entities.

## 2. Verify actual entity IDs

Home Assistant normally derives entity IDs from the friendly device and entity
names, but IDs can differ after discovery, renaming, migration, or conflict
resolution.

Open the Battery Monitor device page and verify the registry IDs rather than
assuming that every example ID is exact. The expected default IDs below are
illustrative:

| First entity to inspect | Expected default ID                                   | Healthy first-use result                                              |
| ----------------------- | ----------------------------------------------------- | --------------------------------------------------------------------- |
| Device Online           | `binary_sensor.battery_monitor_device_online`         | On while the native API is connected                                  |
| Measurement Healthy     | `binary_sensor.battery_monitor_measurement_healthy`   | On after fresh finite INA219 readings arrive                          |
| Battery Voltage         | `sensor.battery_monitor_battery_voltage`              | Plausible finite voltage, later verified against a meter              |
| Battery Current         | `sensor.battery_monitor_battery_current`              | Signed current; positive charge, negative discharge                   |
| Battery Power           | `sensor.battery_monitor_battery_power`                | Same sign as current                                                  |
| SOC Valid               | `binary_sensor.battery_monitor_soc_valid`             | Off on a genuinely fresh device                                       |
| SOC Status              | `sensor.battery_monitor_soc_status`                   | `not_set` on a genuinely fresh device                                 |
| SOC Rule Engine Ready   | `binary_sensor.battery_monitor_soc_rule_engine_ready` | Off until SOC, measurements, settings, and startup baseline are ready |

If Measurement Healthy is off or numeric measurements are unavailable, do not
anchor SOC or configure automation. Diagnose the measurement path first.

**Success criterion:** the device is online, measurements update, and every value
has the expected sign/availability semantics even if calibration acceptance is
not yet complete.

## 3. Understand the initial SOC state

On a genuinely fresh device, these states are normal:

- trusted **State of Charge** is unavailable, not zero;
- **SOC Valid** is off;
- **SOC Status** is `not_set`;
- **SOC Estimate Unbounded** and **Remaining Capacity** can remain visible for
  diagnosis;
- **SOC Rule Engine Ready** is off; and
- Stop Charge, Capacity Warning, and Stop Load conditions are unavailable.

This protects users from mistaking an unanchored number for a trusted empty or
full state. The rule engine cannot become ready until SOC is valid, required
measurements are fresh, rule settings are valid, and startup baselining has
completed.

## 4. Set runtime battery values

Open the device's controls and verify:

- **Rated Battery Capacity** — the installation's rated capacity in Ah;
- **Charging Efficiency** — the fraction of positive charge current added to the
  Ah estimate.

The defaults are 300 Ah and 99%. They describe the repository's default profile,
not universal values or BMS limits.

Changing Rated Battery Capacity preserves the current unbounded SOC percentage by
rescaling Remaining Capacity. It does not establish that SOC is correct and does
not qualify a full or empty endpoint.

**Success criterion:** the values match the selected battery model and documented
operating assumptions.

## 5. Add a starter dashboard

The easiest approach is to create an Entities card in the dashboard editor and
select the actual entity IDs from the device page.

An optional YAML card using the expected default IDs is:

```yaml
type: entities
title: Battery monitor - observe first
show_header_toggle: false
entities:
  - entity: binary_sensor.battery_monitor_device_online
    name: Home Assistant connection
  - entity: binary_sensor.battery_monitor_measurement_healthy
  - entity: sensor.battery_monitor_battery_voltage
  - entity: sensor.battery_monitor_battery_current
  - entity: sensor.battery_monitor_battery_power
  - entity: sensor.battery_monitor_state_of_charge
  - entity: binary_sensor.battery_monitor_soc_valid
  - entity: binary_sensor.battery_monitor_soc_suspect
  - entity: sensor.battery_monitor_soc_status
  - entity: binary_sensor.battery_monitor_soc_rule_configuration_valid
  - entity: binary_sensor.battery_monitor_soc_rule_engine_ready
```

Replace every ID with the registry ID from the actual installation. Keep the
health and trust entities next to the attractive numeric values; a large SOC gauge
without SOC Valid and Measurement Healthy can conceal an unsafe assumption.

Do not add real equipment controls yet. First complete the applicable acceptance
checks in [`commissioning.md`](commissioning.md).

## 6. Anchor SOC only at a qualified endpoint

> **Manual-anchor warning:** Set Battery Full and Set Battery Empty do not inspect
> individual cell voltage, temperature, charger state, BMS state, tail current,
> load state, or manufacturer endpoint criteria. Firmware accepts the operator's
> command; it does not prove that the endpoint is true or safe.

Use a separate dashboard area for manual controls so an anchor cannot be confused
with an ordinary display action. For example:

```yaml
type: vertical-stack
cards:
  - type: markdown
    content: >-
      ## Manual SOC controls
      Use Set Full/Empty only at a battery-manufacturer-qualified endpoint.
      These buttons do not verify cell voltage or safety conditions.
  - type: entities
    show_header_toggle: false
    entities:
      - entity: number.battery_monitor_rated_battery_capacity
      - entity: number.battery_monitor_charging_efficiency
      - entity: button.battery_monitor_set_battery_full
      - entity: button.battery_monitor_set_battery_empty
      - entity: button.battery_monitor_invalidate_soc
```

Home Assistant does not add a universal confirmation step to these ESPHome button
entities. Restrict dashboard editing/access appropriately and keep the warning in
the same control area.

### Full anchor

1. Use the battery/BMS manufacturer's procedure to establish that the battery is
   genuinely at its qualified full endpoint.
2. Confirm measurement health and correct current polarity.
3. Confirm Rated Battery Capacity.
4. Press **Set Battery Full** once.
5. Verify Remaining Capacity equals rated capacity, State of Charge is 100%, SOC
   Valid is on, and SOC Status is `valid` or `suspect` only if outside configured
   plausibility.

### Empty anchor

1. Use the battery/BMS manufacturer's procedure to establish that the battery is
   genuinely at its qualified empty endpoint without defeating undervoltage or
   cell protections.
2. Confirm measurement health and correct current polarity.
3. Press **Set Battery Empty** once.
4. Verify Remaining Capacity is 0 Ah, State of Charge is 0%, and SOC Valid is on.

### Invalidate an untrusted estimate

Press **Invalidate SOC** if the estimate was anchored incorrectly, hardware or
capacity changed in a way that makes the estimate untrustworthy, or commissioning
shows an unresolved measurement problem. Invalidation preserves the diagnostic
estimate but makes trusted SOC and authoritative rule conditions unavailable.

It is safer to show SOC as not trusted than to retain a plausible but knowingly
incorrect number.

## 7. Verify rule settings

The runtime rule levels must be strictly ordered:

```text
Stop Load < Capacity Warning < Stop Charge
```

All hysteresis values must be finite and nonnegative. The user controls already
enforce nonnegative ranges, but cross-rule ordering still depends on the chosen
levels.

Default behavior is:

| Rule             | Assert           | Clear/rearm                         |
| ---------------- | ---------------- | ----------------------------------- |
| Stop Charge      | SOC rises to 95% | SOC falls to 93% with 2% hysteresis |
| Capacity Warning | SOC falls to 40% | SOC rises to 42% with 2% hysteresis |
| Stop Load        | SOC falls to 30% | SOC rises to 32% with 2% hysteresis |

After an edit:

1. verify **SOC Rule Configuration Valid** is on;
2. verify **Measurement Healthy** and **SOC Valid** are on;
3. wait for startup/settings baselining;
4. verify **SOC Rule Engine Ready** turns on; and
5. treat rule conditions as unavailable while readiness is off.

If configuration validity turns off, restore strict level ordering. Restoring a
valid configuration reconstructs conditions without fabricating a threshold
crossing event.

## 8. Keep automation separate from first setup

Do not control a charger or load from a one-shot event. Home Assistant can be
offline when an event occurs, and the monitor stores only the latest event record.
Persistent Stop Charge/Stop Load condition entities are the source of truth, and
automation must also require rule-engine readiness.

The conservative examples and complete event contract are in
[`home-assistant.md`](home-assistant.md). Test adapted examples first with logging,
notifications, or a harmless test switch. No example replaces BMS, fuse, charger,
load, interlock, or correctly rated switching hardware.

## 9. Complete commissioning

Before relying on SOC or connecting any real supplementary control action,
complete [`commissioning.md`](commissioning.md), including:

- voltage and bidirectional-current comparison against trusted instruments;
- offset, drift, and polarity checks;
- manual anchors and unbounded SOC behavior;
- checkpoint and reboot tests;
- rule threshold and hysteresis tests;
- stale-sensor and recovery tests;
- OLED acceptance; and
- Home Assistant outage, replay, and deduplication tests.

Use [`troubleshooting.md`](troubleshooting.md) when a check fails. Do not mark an
item accepted merely because the displayed value looks plausible.
