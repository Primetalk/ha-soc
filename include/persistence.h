#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace battery_monitor {

constexpr size_t ENERGY_CHECKPOINT_WORDS = 6;
constexpr uint32_t ENERGY_CHECKPOINT_MAGIC = 0x454E5247U;  // "ENRG"

inline void store_energy_checkpoint(uint32_t *record, double charged, double discharged) {
  static_assert(sizeof(double) == 2 * sizeof(uint32_t), "64-bit double required");
  record[0] = 0;
  record[1] = 1;  // Schema version.
  std::memcpy(record + 2, &charged, sizeof(charged));
  std::memcpy(record + 4, &discharged, sizeof(discharged));
  record[0] = ENERGY_CHECKPOINT_MAGIC;
}

inline bool restore_energy_checkpoint(const uint32_t *record, double &charged,
                                      double &discharged) {
  double charge, discharge;
  std::memcpy(&charge, record + 2, sizeof(charge));
  std::memcpy(&discharge, record + 4, sizeof(discharge));
  if (record[0] != ENERGY_CHECKPOINT_MAGIC || record[1] != 1 ||
      !std::isfinite(charge) || !std::isfinite(discharge) || charge < 0 || discharge < 0)
    return false;
  charged = charge;
  discharged = discharge;
  return true;
}

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

inline void store_soc_checkpoint(uint32_t *record, double remaining_ah, float capacity_ah,
                                 bool valid) {
  // Magic is written last so a partially prepared in-memory record is invalid.
  record[SOC_CHECKPOINT_MAGIC_INDEX] = 0U;
  // Preserve the legacy flash layout. Narrow only the saved copy, never the
  // live accumulator: per-sample float rounding loses small standby currents.
  record[SOC_CHECKPOINT_REMAINING_AH_BITS_INDEX] = encode_float(static_cast<float>(remaining_ah));
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

inline void store_soc_event(uint32_t *record, uint32_t rule_code, int direction,
                            float soc, bool valid, bool suspect, uint32_t uptime_seconds,
                            uint32_t epoch_seconds, bool timestamp_valid) {
  uint32_t sequence = event_journal_is_valid(record) ? record[EVENT_JOURNAL_SEQUENCE_INDEX] : 0U;
  if (++sequence == 0U) sequence = 1U;
  uint32_t flags = 0U;
  set_flag(flags, EVENT_JOURNAL_SOC_VALID_FLAG, valid);
  set_flag(flags, EVENT_JOURNAL_SOC_SUSPECT_FLAG, suspect);
  set_flag(flags, EVENT_JOURNAL_TIMESTAMP_VALID_FLAG, timestamp_valid);

  // ESPHome saves the whole array as one preference record after this returns.
  record[EVENT_JOURNAL_MAGIC_INDEX] = 0U;
  record[EVENT_JOURNAL_SEQUENCE_INDEX] = sequence;
  record[EVENT_JOURNAL_UPTIME_SECONDS_INDEX] = uptime_seconds;
  record[EVENT_JOURNAL_EPOCH_SECONDS_INDEX] = timestamp_valid ? epoch_seconds : 0U;
  record[EVENT_JOURNAL_SOC_PERCENT_BITS_INDEX] = encode_float(soc);
  record[EVENT_JOURNAL_RULE_CODE_INDEX] = rule_code;
  record[EVENT_JOURNAL_DIRECTION_INDEX] = direction > 0 ? EVENT_DIRECTION_UP : EVENT_DIRECTION_DOWN;
  record[EVENT_JOURNAL_FLAGS_INDEX] = flags;
  record[EVENT_JOURNAL_VERSION_INDEX] = EVENT_JOURNAL_VERSION;
  record[EVENT_JOURNAL_MAGIC_INDEX] = EVENT_JOURNAL_MAGIC;
}

}  // namespace battery_monitor
