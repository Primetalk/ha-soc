# Home Assistant contract

This document describes the entities and events exported by
[`battery-monitor.yaml`](../battery-monitor.yaml). It also provides conservative
automation patterns for reconciliation and event deduplication.

The examples assume Home Assistant assigned the default `battery_monitor_*`
entity IDs. Entity IDs can differ after discovery, renaming, or migration. Check
**Settings > Devices & services > ESPHome > Battery Monitor** and replace every
example ID with the entity registry ID from the actual installation.

## Safety and authority model

The firmware exposes two kinds of rule output:

1. **Persistent condition binary sensors** are authoritative for charger/load
   reconciliation.
2. **Crossing events** are one-shot notification and reconciliation hints.

Never control equipment solely from a transient event. Home Assistant may be
offline when a crossing occurs, and the monitor retains only the latest event
record rather than a lossless event queue.

These entities and examples do not replace a BMS, battery fuse, charger
protection, load protection, hardwired interlocks, or correctly rated switching
hardware. A Home Assistant outage, Wi-Fi failure, ESP32 fault, or stale sensor
must not defeat the battery's independent safety layers.

## Naming and identity

The tables give each entity's ESPHome display name and internal configuration
ID. The expected Home Assistant entity ID is illustrative. ESPHome constructs
registry unique IDs from the device identity and object identity; keep the
device name and internal IDs unchanged if entity-registry continuity matters.

The public current convention is fixed:

- positive amperes and watts mean charging;
- negative amperes and watts mean discharging;
- the charging/discharging/idle labels use a deadband, but the full normalized
  current is still integrated for coulomb counting.

## Measurement entities

| Display name         | ESPHome ID                 | Expected Home Assistant ID                          | Unit/type         | Contract                                                                     |
| -------------------- | -------------------------- | --------------------------------------------------- | ----------------- | ---------------------------------------------------------------------------- |
| Battery Voltage      | `battery_voltage`          | `sensor.battery_monitor_battery_voltage`            | V                 | INA219 bus voltage measured at battery-side VIN- relative to common negative |
| Battery Current      | `battery_current`          | `sensor.battery_monitor_battery_current`            | A                 | Normalized current; positive charging, negative discharging                  |
| Battery Power        | `battery_power`            | `sensor.battery_monitor_battery_power`              | W                 | Battery voltage multiplied by normalized current                             |
| Shunt Voltage        | `shunt_voltage_mv`         | `sensor.battery_monitor_shunt_voltage`              | mV                | Differential shunt voltage diagnostic                                        |
| INA219 Raw Current   | `ina219_current_raw`       | `sensor.battery_monitor_ina219_raw_current`         | A                 | Pre-polarity diagnostic; disabled by default                                 |
| INA219 Raw Power     | `ina219_power_raw`         | `sensor.battery_monitor_ina219_raw_power`           | W                 | INA219 raw-power diagnostic; disabled by default                             |
| INA219 Shunt Voltage | `ina219_shunt_voltage_raw` | `sensor.battery_monitor_ina219_shunt_voltage`       | V                 | Native differential reading; disabled by default                             |
| Battery Charging     | `battery_charging`         | `binary_sensor.battery_monitor_battery_charging`    | binary            | Current is above the positive deadband                                       |
| Battery Discharging  | `battery_discharging`      | `binary_sensor.battery_monitor_battery_discharging` | binary            | Current is below the negative deadband                                       |
| Battery Idle         | `battery_idle`             | `binary_sensor.battery_monitor_battery_idle`        | binary            | Absolute current is inside the deadband                                      |
| Measurement Healthy  | `measurement_healthy`      | `binary_sensor.battery_monitor_measurement_healthy` | diagnostic binary | Required finite readings are fresh                                           |

If the INA219 stops producing valid samples for the configured stale interval,
the numeric and mode entities become unavailable rather than publishing a
fabricated zero. `Measurement Healthy` turns off, the SOC rule engine becomes
unready, and authoritative rule conditions become unavailable. Integration
resumes from a new timing baseline when valid samples return; firmware does not
integrate across the gap.

## SOC entities and controls

| Display name               | ESPHome ID                          | Expected Home Assistant ID                          | Contract                                                                           |
| -------------------------- | ----------------------------------- | --------------------------------------------------- | ---------------------------------------------------------------------------------- |
| Rated Battery Capacity     | `rated_capacity_ah`                 | `number.battery_monitor_rated_battery_capacity`     | Persistent Ah setting; changing it rescales remaining Ah to preserve unbounded SOC |
| Charging Efficiency        | `charge_efficiency_percent`         | `number.battery_monitor_charging_efficiency`        | Persistent percent applied only to positive charging current                       |
| State of Charge            | `state_of_charge`                   | `sensor.battery_monitor_state_of_charge`            | Trusted, unbounded SOC; unavailable while SOC is invalid                           |
| SOC Estimate Unbounded     | `soc_estimate_unbounded`            | `sensor.battery_monitor_soc_estimate_unbounded`     | Diagnostic estimate that remains visible while SOC trust is invalid                |
| Remaining Capacity         | `remaining_capacity_ah`             | `sensor.battery_monitor_remaining_capacity`         | Live, unbounded Ah accumulator                                                     |
| Discarded Integration Gaps | `discarded_integration_gaps_sensor` | `sensor.battery_monitor_discarded_integration_gaps` | Boot-local count of rejected timing gaps                                           |
| SOC Valid                  | `soc_valid`                         | `binary_sensor.battery_monitor_soc_valid`           | On only after a valid restored/manual anchor                                       |
| SOC Suspect                | `soc_suspect`                       | `binary_sensor.battery_monitor_soc_suspect`         | On when valid SOC is outside configured plausibility limits                        |
| SOC Status                 | `soc_status`                        | `sensor.battery_monitor_soc_status`                 | `not_set`, `valid`, `suspect`, or `configuration_error`                            |
| Set Battery Full           | `set_battery_full`                  | `button.battery_monitor_set_battery_full`           | Sets remaining Ah to rated capacity and marks SOC valid                            |
| Set Battery Empty          | `set_battery_empty`                 | `button.battery_monitor_set_battery_empty`          | Sets remaining Ah to zero and marks SOC valid                                      |
| Invalidate SOC             | `invalidate_soc`                    | `button.battery_monitor_invalidate_soc`             | Preserves diagnostic estimate but removes trust and suppresses rules               |

`SOC Valid` and `SOC Suspect` answer different questions:

- **invalid** means the estimate has not been anchored or was explicitly
  invalidated; trusted SOC and rule conditions are unavailable;
- **valid and not suspect** means it is anchored and inside configured
  plausibility limits;
- **valid and suspect** means it is anchored but outside those limits. The
  unbounded value remains visible and still drives rule conditions; it is not
  clamped to 0-100%.

## SOC rule entities

### Runtime settings

| Rule             | Level entity                                    | Hysteresis entity                                    | Direction and defaults |
| ---------------- | ----------------------------------------------- | ---------------------------------------------------- | ---------------------- |
| Stop Charge      | `number.battery_monitor_stop_charge_level`      | `number.battery_monitor_stop_charge_hysteresis`      | Upward, 95%, 2%        |
| Capacity Warning | `number.battery_monitor_capacity_warning_level` | `number.battery_monitor_capacity_warning_hysteresis` | Downward, 40%, 2%      |
| Stop Load        | `number.battery_monitor_stop_load_level`        | `number.battery_monitor_stop_load_hysteresis`        | Downward, 30%, 2%      |

The required level ordering is:

```text
Stop Load < Capacity Warning < Stop Charge
```

If values are missing, non-finite, or unordered, `SOC Rule Configuration Valid`
turns off, `SOC Rule Engine Ready` turns off, and all three authoritative
conditions become unavailable. Restoring a valid configuration baselines the
latches without creating a synthetic crossing event.

### Readiness and authoritative conditions

| Display name                 | ESPHome ID                     | Expected Home Assistant ID                                   | Meaning                                                                                |
| ---------------------------- | ------------------------------ | ------------------------------------------------------------ | -------------------------------------------------------------------------------------- |
| SOC Rule Configuration Valid | `soc_rule_configuration_valid` | `binary_sensor.battery_monitor_soc_rule_configuration_valid` | Runtime levels and hysteresis pass validation                                          |
| SOC Rule Engine Ready        | `soc_rule_engine_ready`        | `binary_sensor.battery_monitor_soc_rule_engine_ready`        | Valid SOC, fresh measurements, valid configuration, and startup baseline are all ready |
| Stop Charge Requested        | `stop_charge_requested`        | `binary_sensor.battery_monitor_stop_charge_requested`        | Authoritative upward-rule latch                                                        |
| Capacity Warning Active      | `capacity_warning_active`      | `binary_sensor.battery_monitor_capacity_warning_active`      | Authoritative warning latch                                                            |
| Stop Load Requested          | `stop_load_requested`          | `binary_sensor.battery_monitor_stop_load_requested`          | Authoritative downward-rule latch                                                      |

Hysteresis behavior is deterministic:

- Stop Charge asserts at or above its level and clears at or below level minus
  hysteresis.
- Capacity Warning and Stop Load assert at or below their levels and clear at or
  above level plus hysteresis.
- Only an armed transition from clear to asserted creates a crossing event.
- Startup, stale-data recovery, SOC anchoring/invalidation, capacity changes,
  and rule-setting changes reconstruct/baseline conditions without a synthetic
  event.

Automations must require `SOC Rule Engine Ready` to be on and must ignore a rule
condition whose state is `unknown` or `unavailable`.

## Durable latest-event metadata

| Display name                 | ESPHome ID                   | Expected Home Assistant ID                                 |
| ---------------------------- | ---------------------------- | ---------------------------------------------------------- |
| Last SOC Event Sequence      | `last_soc_event_sequence`    | `sensor.battery_monitor_last_soc_event_sequence`           |
| Last SOC Event Rule          | `last_soc_event_rule`        | `sensor.battery_monitor_last_soc_event_rule`               |
| Last SOC Event Direction     | `last_soc_event_direction`   | `sensor.battery_monitor_last_soc_event_direction`          |
| Last SOC Event Unix Time     | `last_soc_event_unix_time`   | `sensor.battery_monitor_last_soc_event_unix_time`          |
| Last SOC Event Delivery      | `last_soc_event_delivery`    | `sensor.battery_monitor_last_soc_event_delivery`           |
| Last SOC Event Value         | `last_soc_event_value`       | `sensor.battery_monitor_last_soc_event_value`              |
| Last SOC Event Device Uptime | `last_soc_event_uptime`      | `sensor.battery_monitor_last_soc_event_device_uptime`      |
| Last SOC Event Was Valid     | `last_soc_event_was_valid`   | `binary_sensor.battery_monitor_last_soc_event_was_valid`   |
| Last SOC Event Was Suspect   | `last_soc_event_was_suspect` | `binary_sensor.battery_monitor_last_soc_event_was_suspect` |

There is also an ESPHome native event entity named `SOC Rule Crossing`. It emits
event types `stop_charge`, `capacity_warning`, `stop_load`, and `replay`. Use the
custom Home Assistant event below when sequence-aware deduplication is needed.

## Custom event contract

The stable Home Assistant event type is:

```text
esphome.battery_monitor_soc_rule
```

Every dynamic field is deliberately serialized as a **string**:

| Field             | String values                                  | Meaning                                    |
| ----------------- | ---------------------------------------------- | ------------------------------------------ |
| `device`          | `battery-monitor` by default                   | ESPHome device name                        |
| `sequence`        | `1` through `4294967295`                       | Durable unsigned event sequence            |
| `rule`            | `stop_charge`, `capacity_warning`, `stop_load` | Rule that asserted                         |
| `direction`       | `up`, `down`                                   | Fixed crossing direction                   |
| `soc_percent`     | Decimal string, for example `39.875`           | Unbounded SOC snapshot                     |
| `uptime_seconds`  | Unsigned decimal string                        | Device uptime at the original crossing     |
| `epoch_seconds`   | Unsigned decimal string; `0` if unavailable    | UTC epoch at the original crossing         |
| `timestamp_valid` | `true`, `false`                                | Whether `epoch_seconds` was valid          |
| `soc_valid`       | `true`, `false`                                | SOC-valid flag at the original crossing    |
| `soc_suspect`     | `true`, `false`                                | Plausibility flag at the original crossing |
| `replay`          | `false` for live, `true` for replay            | Delivery label, not a new crossing         |

Example live payload:

```yaml
device: battery-monitor
sequence: "27"
rule: capacity_warning
direction: down
soc_percent: "39.998"
uptime_seconds: "86412"
epoch_seconds: "1787590000"
timestamp_valid: "true"
soc_valid: "true"
soc_suspect: "false"
replay: "false"
```

On each native-API client connection, firmware waits two seconds, republishes
the latest metadata, and emits the same record with the same sequence and
`replay: "true"`. A replay is never evidence of a new crossing.

Sequence behavior:

- zero is reserved and never emitted;
- after `4294967295`, the next sequence wraps to `1`;
- consumers should deduplicate by **equality**, not require a numerically larger
  sequence, so wrap is handled;
- a physical power loss before ESPHome flushes the staged journal preference can
  roll back the latest stored sequence; this is bounded by the configured
  preference polling/coalescing delay but cannot be made atomic with loss of
  power;
- after roughly 2^32 crossings a sequence value can repeat. That is far beyond
  normal MVP use, but a consumer that must distinguish it should also store the
  original timestamp/uptime.

The journal stores only the latest crossing. Several crossings during a long
Home Assistant outage are not queued for later delivery. Persistent rule
conditions still reconstruct the current required action.

## Connectivity diagnostics

| Display name         | Expected Home Assistant ID                    | Meaning                              |
| -------------------- | --------------------------------------------- | ------------------------------------ |
| Device Online        | `binary_sensor.battery_monitor_device_online` | Network and native API are connected |
| Wi-Fi Signal         | `sensor.battery_monitor_wi_fi_signal`         | RSSI diagnostic                      |
| Uptime               | `sensor.battery_monitor_uptime`               | Device uptime                        |
| ESPHome Version      | `sensor.battery_monitor_esphome_version`      | Firmware ESPHome version             |
| IP Address           | `sensor.battery_monitor_ip_address`           | Current IP                           |
| Connected SSID       | `sensor.battery_monitor_connected_ssid`       | Current Wi-Fi network                |
| Wi-Fi MAC Address    | `sensor.battery_monitor_wi_fi_mac_address`    | Interface MAC                        |
| Restart              | `button.battery_monitor_restart`              | Normal restart                       |
| Restart in Safe Mode | `button.battery_monitor_restart_in_safe_mode` | Safe-mode restart                    |

The monitor intentionally does not reboot when Wi-Fi or Home Assistant is
unavailable; local measurement, SOC integration, display, and condition
evaluation continue.

## Example: persistent charger reconciliation

This example uses a hypothetical `switch.battery_charger_enable`. Replace it
with the actual low-energy charger control interface. Never connect an ESP32 or
PCB relay directly into the 500 A path.

The automation runs after Home Assistant startup, reconnect, readiness changes,
or condition changes. It does nothing while any authoritative input is
unavailable/unready.

```yaml
automation:
  - alias: Battery monitor - reconcile charger request
    id: battery_monitor_reconcile_charger_request
    mode: restart
    trigger:
      - platform: homeassistant
        event: start
      - platform: state
        entity_id:
          - binary_sensor.battery_monitor_device_online
          - binary_sensor.battery_monitor_soc_rule_engine_ready
          - binary_sensor.battery_monitor_stop_charge_requested
    condition:
      - condition: state
        entity_id: binary_sensor.battery_monitor_device_online
        state: "on"
      - condition: state
        entity_id: binary_sensor.battery_monitor_soc_rule_engine_ready
        state: "on"
      - condition: template
        value_template: >-
          {{ states('binary_sensor.battery_monitor_stop_charge_requested')
             in ['on', 'off'] }}
    action:
      - choose:
          - conditions:
              - condition: state
                entity_id: binary_sensor.battery_monitor_stop_charge_requested
                state: "on"
            sequence:
              - service: switch.turn_off
                target:
                  entity_id: switch.battery_charger_enable
          - conditions:
              - condition: state
                entity_id: binary_sensor.battery_monitor_stop_charge_requested
                state: "off"
            sequence:
              - service: switch.turn_on
                target:
                  entity_id: switch.battery_charger_enable
```

Choose and document an independent safe behavior for Home Assistant, network,
monitor, sensor, driver, or feedback failure. This example's decision to do
nothing while unready is observability logic, not a fail-safe hardware design.

## Example: persistent discretionary-load reconciliation

This uses a hypothetical low-energy control entity
`switch.discretionary_load_enable`.

```yaml
automation:
  - alias: Battery monitor - reconcile discretionary load request
    id: battery_monitor_reconcile_discretionary_load_request
    mode: restart
    trigger:
      - platform: homeassistant
        event: start
      - platform: state
        entity_id:
          - binary_sensor.battery_monitor_device_online
          - binary_sensor.battery_monitor_soc_rule_engine_ready
          - binary_sensor.battery_monitor_stop_load_requested
    condition:
      - condition: state
        entity_id: binary_sensor.battery_monitor_device_online
        state: "on"
      - condition: state
        entity_id: binary_sensor.battery_monitor_soc_rule_engine_ready
        state: "on"
      - condition: template
        value_template: >-
          {{ states('binary_sensor.battery_monitor_stop_load_requested')
             in ['on', 'off'] }}
    action:
      - choose:
          - conditions:
              - condition: state
                entity_id: binary_sensor.battery_monitor_stop_load_requested
                state: "on"
            sequence:
              - service: switch.turn_off
                target:
                  entity_id: switch.discretionary_load_enable
          - conditions:
              - condition: state
                entity_id: binary_sensor.battery_monitor_stop_load_requested
                state: "off"
            sequence:
              - service: switch.turn_on
                target:
                  entity_id: switch.discretionary_load_enable
```

## Example: sequence-deduplicated notifications

Create a persistent helper in Home Assistant configuration (or create an
equivalent Number helper in the UI):

```yaml
input_number:
  battery_monitor_last_event_sequence:
    name: Battery monitor last event sequence
    min: 0
    max: 4294967295
    step: 1
    mode: box
```

The following automation consumes every new sequence, waits briefly for entity
state reconciliation, and notifies only if the durable Capacity Warning
condition is currently authoritative and asserted. Replace the notification
target.

```yaml
automation:
  - alias: Battery monitor - consume SOC rule event
    id: battery_monitor_consume_soc_rule_event
    mode: queued
    max: 10
    trigger:
      - platform: event
        event_type: esphome.battery_monitor_soc_rule
        event_data:
          device: battery-monitor
    variables:
      sequence: "{{ trigger.event.data.sequence | int(0) }}"
      rule: "{{ trigger.event.data.rule | default('unknown') }}"
      delivery: >-
        {{ 'replay' if trigger.event.data.replay == 'true' else 'live' }}
    condition:
      - condition: template
        value_template: "{{ sequence > 0 }}"
    action:
      - delay: "00:00:01"
      # Read the helper only when this queued run starts its action. Capturing it
      # in top-level variables can let two closely spaced duplicate deliveries
      # retain the same stale value before the first run updates the helper.
      - condition: template
        value_template: >-
          {{ sequence !=
             states('input_number.battery_monitor_last_event_sequence') | int(0) }}
      - choose:
          - conditions:
              - condition: template
                value_template: "{{ rule == 'capacity_warning' }}"
              - condition: state
                entity_id: binary_sensor.battery_monitor_soc_rule_engine_ready
                state: "on"
              - condition: state
                entity_id: binary_sensor.battery_monitor_capacity_warning_active
                state: "on"
            sequence:
              - service: notify.mobile_app_replace_me
                data:
                  title: Battery capacity warning
                  message: >-
                    SOC {{ trigger.event.data.soc_percent }}%;
                    sequence {{ sequence }} ({{ delivery }}).
      - service: input_number.set_value
        target:
          entity_id: input_number.battery_monitor_last_event_sequence
        data:
          value: "{{ sequence }}"
```

Important properties of the example:

- it compares for inequality rather than `sequence > previous_sequence`, so
  uint32 wrap is not discarded;
- queued runs read the helper immediately before reconciliation rather than
  caching it when triggered, so closely spaced duplicate deliveries serialize
  behind the first helper update;
- it consumes the sequence after reconciling against persistent readiness and
  condition state;
- a reconnect replay with an already consumed sequence is ignored;
- an unseen replay can recover the latest notification after an outage;
- events for other rules advance the single per-device sequence helper but do
  not drive charger/load controls;
- it cannot recover multiple crossings that occurred during a long outage,
  because the device intentionally journals only the latest record.
