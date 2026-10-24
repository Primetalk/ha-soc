#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace battery_monitor {

enum class ChargePhase { Unavailable, Idle, Discharging, Observing, CC, CV, AlmostFull, Full };

inline const char *charge_phase_name(ChargePhase phase) {
  switch (phase) {
    case ChargePhase::Idle: return "idle";
    case ChargePhase::Discharging: return "discharging";
    case ChargePhase::Observing: return "observing";
    case ChargePhase::CC: return "cc";
    case ChargePhase::CV: return "cv";
    case ChargePhase::AlmostFull: return "almost_full";
    case ChargePhase::Full: return "full";
    default: return "unavailable";
  }
}

struct ChargeConfig {
  float charged_voltage_v;
  float tail_current_a;
  float active_current_a;
  float voltage_band_v;
  uint32_t window_ms;
  uint32_t max_gap_ms;

  bool valid() const {
    return std::isfinite(charged_voltage_v) && charged_voltage_v > 0 &&
        std::isfinite(tail_current_a) && tail_current_a > 0 &&
        std::isfinite(active_current_a) && active_current_a > tail_current_a &&
        std::isfinite(voltage_band_v) && voltage_band_v > 0 &&
        window_ms >= 60000U && window_ms <= 3600000U &&
        max_gap_ms > 0 && max_gap_ms <= 10000U;
  }
};

// Inferred phases, not charger telemetry. Only complete observed sessions can
// anchor SOC; reboot or lost data deliberately forfeits all accumulated proof.
class ChargeDetector {
 public:
  void reset() { *this = ChargeDetector{}; }
  ChargePhase phase() const { return phase_; }
  bool full() const { return phase_ == ChargePhase::Full; }

  // True is a one-sample request to synchronize SOC, including periodic repeats.
  bool update(uint32_t now, float voltage, float current, bool healthy,
              const ChargeConfig &config) {
    if (!healthy || !config.valid() || !std::isfinite(voltage) ||
        !std::isfinite(current) || voltage <= 0) {
      reset();
      return false;
    }
    if (has_sample_ && now - last_sample_ms_ > config.max_gap_ms) reset();
    if (current <= 0) {
      reset();
      phase_ = current < 0 ? ChargePhase::Discharging : ChargePhase::Idle;
      return false;
    }
    if (cv_seen_ && (voltage < config.charged_voltage_v ||
                    std::fabs(voltage - last_voltage_) > config.voltage_band_v ||
                    current >= peak_current_ * 0.8f)) {
      reset();
    }
    has_sample_ = true;
    last_sample_ms_ = now;
    last_voltage_ = voltage;
    tail_average_.update(now, current);

    if (voltage < config.charged_voltage_v || current > peak_current_ * 0.8f) {
      taper_seen_ = false;
      taper_.clear();
    }

    // A single high-current spike must never establish a charging session.
    if (current >= config.active_current_a) {
      if (active_.elapsed(now) >= 60000U) armed_ = true;
      peak_current_ = std::max(peak_current_, current);
    } else {
      active_.clear();
    }
    // Require an intermediate part of the taper. An abrupt drop from CC to
    // zero/small offset must not look like a completed CV phase.
    if (armed_ && voltage >= config.charged_voltage_v &&
        current > config.tail_current_a && current <= peak_current_ * 0.8f) {
      if (taper_.elapsed(now) >= 60000U) taper_seen_ = true;
    } else {
      taper_.clear();
    }

    // 61 points cover a rolling window (five minutes by default), ~1 KiB.
    // Every input sample still enforces freshness, polarity and tail continuity.
    // Keep ALL voltage extrema between points; decimation must not hide dips.
    if (count_ != 0) {
      auto &point = points_[(next_ + POINTS - 1) % POINTS];
      point.min_voltage = std::min(point.min_voltage, voltage);
      point.max_voltage = std::max(point.max_voltage, voltage);
    }
    if (count_ == 0 || now - last_point_ms_ >= config.window_ms / 60U) {
      points_[next_] = {voltage, voltage, current, now};
      next_ = (next_ + 1) % POINTS;
      if (count_ < POINTS) ++count_;
      last_point_ms_ = now;
    }
    bool cc = false;
    bool stable_voltage = false;
    const unsigned oldest = (next_ + POINTS - count_) % POINTS;
    if (count_ > 1 && now - points_[oldest].time >= config.window_ms) {
      float min_v = voltage, max_v = voltage;
      float min_i = current, max_i = current, early = 0, late = 0;
      unsigned early_count = 0, late_count = 0;
      for (unsigned i = 0; i < count_; ++i) {
        const auto &p = points_[(oldest + i) % POINTS];
        const uint32_t age = now - p.time;
        // Retain only the latest time window plus the bucket crossing its
        // start. Sample jitter must not turn 61 points into a longer window.
        if (age > config.window_ms && i + 1 < count_ &&
            now - points_[(oldest + i + 1) % POINTS].time >= config.window_ms)
          continue;
        min_v = std::min(min_v, p.min_voltage);
        max_v = std::max(max_v, p.max_voltage);
        min_i = std::min(min_i, p.current);
        max_i = std::max(max_i, p.current);
        if (age >= config.window_ms * 4U / 5U) { early += p.current; ++early_count; }
        if (age <= config.window_ms / 5U) { late += p.current; ++late_count; }
      }
      early = early_count > 0 ? early / early_count : current;
      late = late_count > 0 ? late / late_count : current;
      const bool constant_current = armed_ && min_i > config.tail_current_a &&
          max_i <= min_i * 1.2f;
      cc = constant_current && min_i >= config.active_current_a;
      if (constant_current && !cv_seen_) {
        // A new constant-current plateau (even below active_current_a)
        // supersedes a pending taper that
        // never reached CV. Otherwise an hours-old decline could qualify a cut.
        taper_seen_ = false;
        taper_.clear();
        cv_seen_ = false;
        peak_current_ = current;
      }
      stable_voltage = min_v >= config.charged_voltage_v &&
          max_v - min_v <= config.voltage_band_v;
      if (armed_ && taper_seen_ && stable_voltage &&
          early > config.tail_current_a && late <= early * 0.8f) {
        cv_seen_ = true;
      }
    }

    // Once CV is recognized, a slow/flat tail does not undo it. The latest
    // voltage window and averaged current must still qualify on every sample.
    if (cv_seen_ && stable_voltage && tail_average_.ready() &&
        tail_average_.value() <= config.tail_current_a) {
      const uint32_t elapsed = tail_.elapsed(now);
      if (elapsed >= config.window_ms) {
        phase_ = ChargePhase::Full;
        // Restart just the dwell clock; retain the full indicator between repeats.
        tail_.start = now;
        full_latched_ = true;
        return true;
      }
      phase_ = full_latched_ ? ChargePhase::Full : ChargePhase::AlmostFull;
    } else {
      tail_.clear();
      full_latched_ = false;
      phase_ = cv_seen_ ? ChargePhase::CV : (cc ? ChargePhase::CC : ChargePhase::Observing);
    }
    return false;
  }

 private:
  // Thirty completed one-second buckets, time-weighted with the previous
  // sample held until the next sample. Refresh at least once per second rather
  // than weighting rapid samples more heavily. Gaps are rejected by update().
  class TailAverage {
   public:
    void update(uint32_t now, float current) {
      if (!started_) {
        started_ = true;
      } else {
        uint32_t elapsed = now - previous_ms_;
        while (elapsed > 0) {
          const uint32_t part = std::min(elapsed, 1000U - bucket_ms_);
          bucket_area_ += static_cast<double>(previous_current_) * part;
          bucket_ms_ += part;
          elapsed -= part;
          if (bucket_ms_ == 1000U) {
            sum_ -= buckets_[next_];
            buckets_[next_] = bucket_area_ / 1000.0;
            sum_ += buckets_[next_];
            next_ = (next_ + 1) % 30;
            if (count_ < 30) ++count_;
            bucket_ms_ = 0;
            bucket_area_ = 0;
          }
        }
      }
      previous_ms_ = now;
      previous_current_ = current;
    }
    bool ready() const { return count_ == 30; }
    float value() const { return static_cast<float>(sum_ / 30.0); }

   private:
    double buckets_[30]{};
    double sum_ = 0, bucket_area_ = 0;
    uint32_t previous_ms_ = 0, bucket_ms_ = 0;
    unsigned next_ = 0, count_ = 0;
    float previous_current_ = 0;
    bool started_ = false;
  };

  struct Timer {
    bool running = false;
    uint32_t start = 0;
    uint32_t elapsed(uint32_t now) {
      if (!running) { running = true; start = now; }
      return now - start;
    }
    void clear() { running = false; }
  };
  struct Point { float min_voltage = 0, max_voltage = 0, current = 0; uint32_t time = 0; };
  static constexpr unsigned POINTS = 61;
  Point points_[POINTS]{};
  unsigned next_ = 0, count_ = 0;
  uint32_t last_point_ms_ = 0, last_sample_ms_ = 0;
  bool has_sample_ = false, armed_ = false, taper_seen_ = false;
  bool cv_seen_ = false, full_latched_ = false;
  float peak_current_ = 0, last_voltage_ = 0;
  TailAverage tail_average_;
  Timer active_, taper_, tail_;
  ChargePhase phase_ = ChargePhase::Unavailable;
};

// A function-local object avoids custom types in ESPHome's generated global
// declarations, which precede esphome.includes. No preference allocation.
inline ChargeDetector &charge_detector() {
  static ChargeDetector detector;
  return detector;
}

}  // namespace battery_monitor
