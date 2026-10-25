#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "persistence.h"

namespace battery_monitor {

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

struct SocRuleConfig {
  float stop_charge_level;
  float stop_charge_hysteresis;
  float capacity_warning_level;
  float capacity_warning_hysteresis;
  float stop_load_level;
  float stop_load_hysteresis;

  bool valid() const {
    return soc_rule_configuration_is_valid(
        stop_charge_level, stop_charge_hysteresis,
        capacity_warning_level, capacity_warning_hysteresis,
        stop_load_level, stop_load_hysteresis);
  }
};

inline const char *soc_rule_name(uint32_t code, const char *fallback = "unknown") {
  switch (code) {
    case 1U: return "stop_charge";
    case 2U: return "capacity_warning";
    case 3U: return "stop_load";
    default: return fallback;
  }
}

struct SocRuleEvaluation {
  bool ready = false;
  uint32_t crossings = 0U;
};

// Baseline after startup, configuration edits or lost authority; only normal
// evaluation emits false-to-true crossings. Persistent latch bytes stay intact
// while inputs/configuration are unavailable.
inline SocRuleEvaluation evaluate_soc_rules(uint32_t *latches, float soc,
                                           const SocRuleConfig &config, bool inputs_ok,
                                           bool startup_ready, bool &armed) {
  if (!inputs_ok || !config.valid()) {
    armed = false;
    return {};
  }
  const bool baseline = !startup_ready || !armed;
  struct Rule { uint32_t flag; float level; float hysteresis; bool upward; };
  const Rule rules[] = {
      {RULE_LATCH_STOP_CHARGE_FLAG, config.stop_charge_level, config.stop_charge_hysteresis, true},
      {RULE_LATCH_CAPACITY_WARNING_FLAG, config.capacity_warning_level,
       config.capacity_warning_hysteresis, false},
      {RULE_LATCH_STOP_LOAD_FLAG, config.stop_load_level, config.stop_load_hysteresis, false},
  };
  SocRuleEvaluation result{startup_ready, 0U};
  for (const auto &rule : rules) {
    const bool previous = rule_latch_value(latches, rule.flag);
    const bool next = rule.upward
        ? update_upward_latch(previous, soc, rule.level, rule.hysteresis)
        : update_downward_latch(previous, soc, rule.level, rule.hysteresis);
    set_rule_latch(latches, rule.flag, next);
    if (!baseline && !previous && next) result.crossings |= rule.flag;
  }
  armed = startup_ready;
  return result;
}

}  // namespace battery_monitor
