#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#ifndef BATTERY_MONITOR_HOST_TEST
#include "esphome/components/sensor/sensor.h"
#include "esphome/core/controller_registry.h"
#endif

namespace battery_monitor {

// ESPHome emits global declarations before files listed in esphome.includes.
// Restored globals therefore use primitive uint32_t arrays; this header only
// supplies indexes and helpers used later inside generated lambdas. Each array
// is persisted as one preference object by RestoringGlobalsComponent.

constexpr uint32_t SOC_CHECKPOINT_MAGIC = 0x534F4343U;  // "SOCC"
constexpr uint32_t SOC_CHECKPOINT_VERSION = 1U;
enum SocCheckpointIndex : size_t {
  SOC_CHECKPOINT_MAGIC_INDEX = 0,
  SOC_CHECKPOINT_VERSION_INDEX,
  SOC_CHECKPOINT_REMAINING_AH_BITS_INDEX,
  SOC_CHECKPOINT_CAPACITY_AH_BITS_INDEX,
  SOC_CHECKPOINT_FLAGS_INDEX,
  SOC_CHECKPOINT_WORDS,
};
constexpr uint32_t SOC_CHECKPOINT_VALID_FLAG = 1U << 0;

constexpr uint32_t RULE_LATCH_MAGIC = 0x524C5443U;  // "RLTC"
constexpr uint32_t RULE_LATCH_VERSION = 1U;
enum RuleLatchIndex : size_t {
  RULE_LATCH_MAGIC_INDEX = 0,
  RULE_LATCH_VERSION_INDEX,
  RULE_LATCH_FLAGS_INDEX,
  RULE_LATCH_WORDS,
};
constexpr uint32_t RULE_LATCH_STOP_CHARGE_FLAG = 1U << 0;
constexpr uint32_t RULE_LATCH_CAPACITY_WARNING_FLAG = 1U << 1;
constexpr uint32_t RULE_LATCH_STOP_LOAD_FLAG = 1U << 2;

constexpr uint32_t EVENT_JOURNAL_MAGIC = 0x534F4345U;  // "SOCE"
constexpr uint32_t EVENT_JOURNAL_VERSION = 1U;
enum EventJournalIndex : size_t {
  EVENT_JOURNAL_MAGIC_INDEX = 0,
  EVENT_JOURNAL_VERSION_INDEX,
  EVENT_JOURNAL_SEQUENCE_INDEX,
  EVENT_JOURNAL_UPTIME_SECONDS_INDEX,
  EVENT_JOURNAL_EPOCH_SECONDS_INDEX,
  EVENT_JOURNAL_SOC_PERCENT_BITS_INDEX,
  EVENT_JOURNAL_RULE_CODE_INDEX,
  EVENT_JOURNAL_DIRECTION_INDEX,
  EVENT_JOURNAL_FLAGS_INDEX,
  EVENT_JOURNAL_WORDS,
};
constexpr uint32_t EVENT_JOURNAL_SOC_VALID_FLAG = 1U << 0;
constexpr uint32_t EVENT_JOURNAL_SOC_SUSPECT_FLAG = 1U << 1;
constexpr uint32_t EVENT_JOURNAL_TIMESTAMP_VALID_FLAG = 1U << 2;
constexpr uint32_t EVENT_DIRECTION_NONE = 0U;
constexpr uint32_t EVENT_DIRECTION_UP = 1U;
constexpr uint32_t EVENT_DIRECTION_DOWN = 2U;

inline uint32_t encode_float(float value) {
  static_assert(sizeof(float) == sizeof(uint32_t), "32-bit float required");
  uint32_t bits;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

inline float decode_float(uint32_t bits) {
  static_assert(sizeof(float) == sizeof(uint32_t), "32-bit float required");
  float value;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

inline bool has_flag(uint32_t flags, uint32_t flag) { return (flags & flag) != 0U; }

// Numeric sensors do not derive from StatefulEntityBase in ESPHome 2026.8.0,
// so they have no invalidate_state() method. Clear the authoritative state flag
// and notify registered frontends so the native API emits missing_state=true.
// A later publish_state() call restores availability through the normal path.
#ifndef BATTERY_MONITOR_HOST_TEST
inline void invalidate_sensor_state(esphome::sensor::Sensor *sensor) {
  if (sensor == nullptr || !sensor->has_state()) return;
  sensor->set_has_state(false);
#if defined(USE_SENSOR) && defined(USE_CONTROLLER_REGISTRY)
  esphome::ControllerRegistry::notify_sensor_update(sensor);
#endif
}
#endif

inline void set_flag(uint32_t &flags, uint32_t flag, bool enabled) {
  if (enabled) {
    flags |= flag;
  } else {
    flags &= ~flag;
  }
}

inline bool soc_checkpoint_is_valid(const uint32_t *record) {
  const float capacity_ah = decode_float(record[SOC_CHECKPOINT_CAPACITY_AH_BITS_INDEX]);
  return record[SOC_CHECKPOINT_MAGIC_INDEX] == SOC_CHECKPOINT_MAGIC &&
         record[SOC_CHECKPOINT_VERSION_INDEX] == SOC_CHECKPOINT_VERSION &&
         std::isfinite(decode_float(record[SOC_CHECKPOINT_REMAINING_AH_BITS_INDEX])) &&
         std::isfinite(capacity_ah) && capacity_ah > 0.0f;
}

inline void store_soc_checkpoint(uint32_t *record, float remaining_ah, float capacity_ah,
                                 bool valid) {
  // Magic is written last so a partially prepared in-memory record is invalid.
  record[SOC_CHECKPOINT_MAGIC_INDEX] = 0U;
  record[SOC_CHECKPOINT_REMAINING_AH_BITS_INDEX] = encode_float(remaining_ah);
  record[SOC_CHECKPOINT_CAPACITY_AH_BITS_INDEX] = encode_float(capacity_ah);
  record[SOC_CHECKPOINT_FLAGS_INDEX] = valid ? SOC_CHECKPOINT_VALID_FLAG : 0U;
  record[SOC_CHECKPOINT_VERSION_INDEX] = SOC_CHECKPOINT_VERSION;
  record[SOC_CHECKPOINT_MAGIC_INDEX] = SOC_CHECKPOINT_MAGIC;
}

inline bool rule_latch_record_is_valid(const uint32_t *record) {
  return record[RULE_LATCH_MAGIC_INDEX] == RULE_LATCH_MAGIC &&
         record[RULE_LATCH_VERSION_INDEX] == RULE_LATCH_VERSION;
}

inline void reset_rule_latches(uint32_t *record) {
  record[RULE_LATCH_MAGIC_INDEX] = 0U;
  record[RULE_LATCH_FLAGS_INDEX] = 0U;
  record[RULE_LATCH_VERSION_INDEX] = RULE_LATCH_VERSION;
  record[RULE_LATCH_MAGIC_INDEX] = RULE_LATCH_MAGIC;
}

inline bool rule_latch_value(const uint32_t *record, uint32_t flag) {
  return rule_latch_record_is_valid(record) && has_flag(record[RULE_LATCH_FLAGS_INDEX], flag);
}

inline void set_rule_latch(uint32_t *record, uint32_t flag, bool value) {
  if (!rule_latch_record_is_valid(record)) reset_rule_latches(record);
  set_flag(record[RULE_LATCH_FLAGS_INDEX], flag, value);
}

inline bool event_journal_is_valid(const uint32_t *record) {
  return record[EVENT_JOURNAL_MAGIC_INDEX] == EVENT_JOURNAL_MAGIC &&
         record[EVENT_JOURNAL_VERSION_INDEX] == EVENT_JOURNAL_VERSION &&
         record[EVENT_JOURNAL_SEQUENCE_INDEX] != 0U &&
         std::isfinite(decode_float(record[EVENT_JOURNAL_SOC_PERCENT_BITS_INDEX]));
}

inline float soc_percent(float remaining_ah, float capacity_ah) {
  if (!std::isfinite(remaining_ah) || !std::isfinite(capacity_ah) || capacity_ah <= 0.0f) {
    return NAN;
  }
  return remaining_ah * 100.0f / capacity_ah;
}

inline float rescale_remaining_ah(float remaining_ah, float old_capacity_ah,
                                  float new_capacity_ah) {
  if (!std::isfinite(remaining_ah) || !std::isfinite(old_capacity_ah) ||
      !std::isfinite(new_capacity_ah) || old_capacity_ah <= 0.0f ||
      new_capacity_ah <= 0.0f) {
    return NAN;
  }
  return remaining_ah * new_capacity_ah / old_capacity_ah;
}

inline float integrate_remaining_ah(float remaining_ah, float normalized_current_a,
                                    float charging_efficiency_percent,
                                    uint32_t elapsed_ms) {
  if (!std::isfinite(remaining_ah) || !std::isfinite(normalized_current_a) ||
      !std::isfinite(charging_efficiency_percent)) {
    return remaining_ah;
  }
  float effective_current = normalized_current_a;
  if (effective_current > 0.0f) {
    effective_current *= charging_efficiency_percent / 100.0f;
  }
  return remaining_ah + effective_current * (static_cast<float>(elapsed_ms) / 3600000.0f);
}

inline bool update_upward_latch(bool previous, float soc, float level, float hysteresis) {
  if (!std::isfinite(soc) || !std::isfinite(level) || !std::isfinite(hysteresis)) return previous;
  if (soc >= level) return true;
  if (soc <= level - std::max(0.0f, hysteresis)) return false;
  return previous;
}

inline bool update_downward_latch(bool previous, float soc, float level, float hysteresis) {
  if (!std::isfinite(soc) || !std::isfinite(level) || !std::isfinite(hysteresis)) return previous;
  if (soc <= level) return true;
  if (soc >= level + std::max(0.0f, hysteresis)) return false;
  return previous;
}

inline bool soc_is_suspect(bool valid, float soc, float plausible_min, float plausible_max) {
  return valid && std::isfinite(soc) && (soc < plausible_min || soc > plausible_max);
}

inline bool soc_rule_configuration_is_valid(float stop_charge_level,
                                            float stop_charge_hysteresis,
                                            float capacity_warning_level,
                                            float capacity_warning_hysteresis,
                                            float stop_load_level,
                                            float stop_load_hysteresis) {
  return std::isfinite(stop_charge_level) && std::isfinite(stop_charge_hysteresis) &&
         std::isfinite(capacity_warning_level) &&
         std::isfinite(capacity_warning_hysteresis) && std::isfinite(stop_load_level) &&
         std::isfinite(stop_load_hysteresis) && stop_charge_hysteresis >= 0.0f &&
         capacity_warning_hysteresis >= 0.0f && stop_load_hysteresis >= 0.0f &&
         stop_load_level < capacity_warning_level &&
         capacity_warning_level < stop_charge_level;
}

}  // namespace battery_monitor
