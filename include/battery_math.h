#pragma once

#include <cmath>
#include <cstdint>

#include "persistence.h"

namespace battery_monitor {

// Right-endpoint rectangular integration of DC terminal power, in kWh.
// Invalid readings break continuity; long gaps and boot downtime are skipped.
inline void integrate_energy_sample(float power_w, uint32_t now, uint32_t max_gap_ms,
                                    double &charged, double &discharged,
                                    uint32_t &previous_ms, bool &baseline) {
  if (!std::isfinite(power_w)) {
    baseline = false;
    return;
  }
  const uint32_t elapsed = now - previous_ms;
  previous_ms = now;
  const bool had_baseline = baseline;
  baseline = true;
  if (!had_baseline || elapsed > max_gap_ms) return;
  const double energy = static_cast<double>(power_w) * elapsed / 3600000000.0;
  if (energy >= 0) charged += energy;
  else discharged -= energy;
}

inline double soc_percent(double remaining_ah, double capacity_ah) {
  if (!std::isfinite(remaining_ah) || !std::isfinite(capacity_ah) || capacity_ah <= 0.0f) {
    return NAN;
  }
  return remaining_ah * 100.0 / capacity_ah;
}

inline double rescale_remaining_ah(double remaining_ah, double old_capacity_ah,
                                  double new_capacity_ah) {
  if (!std::isfinite(remaining_ah) || !std::isfinite(old_capacity_ah) ||
      !std::isfinite(new_capacity_ah) || old_capacity_ah <= 0.0f ||
      new_capacity_ah <= 0.0f) {
    return NAN;
  }
  return remaining_ah * new_capacity_ah / old_capacity_ah;
}

inline bool anchor_soc_full(float capacity_ah, double &remaining_ah, bool &valid,
                            uint32_t *checkpoint) {
  if (!std::isfinite(capacity_ah) || capacity_ah <= 0) return false;
  remaining_ah = capacity_ah;
  valid = true;
  store_soc_checkpoint(checkpoint, remaining_ah, capacity_ah, true);
  return true;
}

inline double integrate_remaining_ah(double remaining_ah, double normalized_current_a,
                                    double charging_efficiency_percent,
                                    uint32_t elapsed_ms) {
  if (!std::isfinite(remaining_ah) || !std::isfinite(normalized_current_a) ||
      !std::isfinite(charging_efficiency_percent)) {
    return remaining_ah;
  }
  double effective_current = normalized_current_a;
  if (effective_current > 0.0) {
    effective_current *= charging_efficiency_percent / 100.0;
  }
  return remaining_ah + effective_current * (static_cast<double>(elapsed_ms) / 3600000.0);
}

inline bool soc_is_suspect(bool valid, float soc, float plausible_min, float plausible_max) {
  return valid && std::isfinite(soc) && (soc < plausible_min || soc > plausible_max);
}

}  // namespace battery_monitor
