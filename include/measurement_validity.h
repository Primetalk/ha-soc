#pragma once

#include <cstdint>

namespace battery_monitor {

// Preserve the firmware's zero sentinel, inclusive age boundary and wraparound.
inline bool sample_is_fresh(uint32_t now, uint32_t last_sample, uint32_t max_age) {
  return last_sample != 0U && now - last_sample <= max_age;
}

}  // namespace battery_monitor
