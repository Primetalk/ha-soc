#pragma once

#include <cmath>
#include <cstdint>

namespace battery_monitor {

enum class CurrentDirection { Idle, Charging, Discharging };

// One mutually exclusive direction state. Classification never changes the
// raw current used for SOC/energy integration or charge-process inference.
class CurrentDirectionDetector {
 public:
  void reset() { *this = CurrentDirectionDetector{}; }
  CurrentDirection state() const { return state_; }
  bool update(uint32_t now, float current, float enter_a, float exit_a,
              uint32_t settle_ms, uint32_t max_gap_ms) {
    if (!std::isfinite(current) || !std::isfinite(enter_a) ||
        !std::isfinite(exit_a) || exit_a < 0 || enter_a <= exit_a ||
        max_gap_ms == 0) {
      reset();
      return false;
    }
    if (has_sample_ && now - last_sample_ms_ > max_gap_ms) reset();
    has_sample_ = true;
    last_sample_ms_ = now;
    const bool leaving = (state_ == CurrentDirection::Charging && current < exit_a) ||
        (state_ == CurrentDirection::Discharging && current > -exit_a);
    if (leaving) {
      if (!exiting_) { exiting_ = true; exit_since_ = now; }
      if (now - exit_since_ >= settle_ms) {
        state_ = CurrentDirection::Idle;
        exiting_ = false;
      }
    } else {
      exiting_ = false;
    }
    // Entry into the opposite direction has its own continuous dwell. Changes
    // between idle and opposite current must not restart the exit clock.
    CurrentDirection target = CurrentDirection::Idle;
    if (current > enter_a) target = CurrentDirection::Charging;
    else if (current < -enter_a) target = CurrentDirection::Discharging;
    if (target == CurrentDirection::Idle || target == state_) {
      pending_ = false;
    } else {
      if (!pending_ || target != target_) {
        pending_ = true;
        target_ = target;
        pending_since_ = now;
      }
      if (now - pending_since_ >= settle_ms) {
        state_ = target;
        pending_ = false;
        exiting_ = false;
      }
    }
    return true;
  }

 private:
  CurrentDirection state_ = CurrentDirection::Idle, target_ = CurrentDirection::Idle;
  bool has_sample_ = false, pending_ = false, exiting_ = false;
  uint32_t last_sample_ms_ = 0, pending_since_ = 0, exit_since_ = 0;
};

inline CurrentDirectionDetector &current_direction_detector() {
  static CurrentDirectionDetector detector;
  return detector;
}

}  // namespace battery_monitor
