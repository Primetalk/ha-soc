#pragma once

#include <cstdint>
#include <cmath>

namespace battery_monitor {

// Preserve the firmware's zero sentinel, inclusive age boundary and wraparound.
inline bool sample_is_fresh(uint32_t now, uint32_t last_sample, uint32_t max_age) {
  return last_sample != 0U && now - last_sample <= max_age;
}

// Separate battery-side supply branch: main is positive into the battery,
// auxiliary is positive from the battery toward its small consumers.
// Parallel branch currents add: main + (-auxiliary consumption), converting
// both to the positive-charge convention before combining them.
// Once detected, a failed auxiliary measurement must never masquerade as zero.
inline float parallel_branch_current(float main_a, bool auxiliary_present,
                                     float auxiliary_a, bool auxiliary_fresh) {
  if (!std::isfinite(main_a)) return NAN;
  if (!auxiliary_present) return main_a;
  if (!auxiliary_fresh || !std::isfinite(auxiliary_a)) return NAN;
  return main_a - auxiliary_a;
}

}  // namespace battery_monitor
