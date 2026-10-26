#define BATTERY_MONITOR_HOST_TEST
#include "include/battery_monitor_types.h"
#include "include/charge_detection.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>

namespace {

int failures = 0;

void expect(bool condition, const char *description) {
  if (condition) return;
  std::cerr << "FAIL: " << description << '\n';
  failures++;
}

void expect_near(double actual, double expected, double tolerance, const char *description) {
  if (std::isfinite(actual) && std::fabs(actual - expected) <= tolerance) return;
  std::cerr << "FAIL: " << description << " (expected " << expected << ", got " << actual
            << ")\n";
  failures++;
}

// These traces catch premature anchors, missed taper evidence, stale-data
// continuity, and repeated anchors driven by SOC rather than measurements.
struct ChargeTrace {
  battery_monitor::ChargeDetector detector;
  battery_monitor::ChargeConfig config{14.4f, 0.3f, 5.0f, 0.15f, 300000U, 5000U};
  uint32_t now = 1000;
  int anchors = 0;
  void sample(float voltage, float current, bool healthy = true) {
    if (detector.update(now, voltage, current, healthy, config)) ++anchors;
    now += 1000;
  }
  void hold(int seconds, float voltage, float current) {
    for (int i = 0; i < seconds; ++i) sample(voltage, current);
  }
  void cc() { hold(361, 13.5f, 35.0f); }
  void cv(float voltage = 14.616f) {
    for (int i = 0; i <= 360; ++i)
      sample(voltage, 35.0f - 34.0f * static_cast<float>(i) / 360.0f);
  }
};

void test_charge_process() {
  using battery_monitor::ChargePhase;
  ChargeTrace t;
  t.cc();
  expect(t.detector.phase() == ChargePhase::CC, "sustained 35 A must infer CC");
  expect(t.anchors == 0, "CC must not anchor SOC");
  t.cv();
  expect(t.detector.phase() == ChargePhase::CV, "stable high voltage and taper must infer CV");
  expect(t.anchors == 0, "CV above tail current must not anchor SOC");
  t.hold(330, 14.616f, 0.3f);
  expect(t.detector.phase() == ChargePhase::AlmostFull,
         "tail current must finish averaging and five full minutes before detection");
  expect(t.anchors == 0, "no premature tail anchor");
  t.hold(1, 14.616f, 0.3f);
  expect(t.detector.full(), "qualified tail must expose Full charge detected");
  expect(t.anchors == 1, "qualified tail must request one automatic anchor");
  t.hold(299, 14.616f, 0.3f);
  expect(t.anchors == 1, "continued tail must not anchor on every sample");
  t.hold(1, 14.616f, 0.3f);
  expect(t.anchors == 2, "continued tail must re-anchor after another five minutes");
  t.sample(14.616f, 0.0f);
  expect(!t.detector.full(), "zero current must cancel full detection immediately");
  t.hold(700, 14.616f, 0.3f);
  expect(t.anchors == 2, "old session evidence must not survive charger termination");
}

void test_charge_rejections() {
  using battery_monitor::ChargePhase;
  ChargeTrace resting;
  resting.hold(1000, 14.616f, 0.2f);
  expect(resting.anchors == 0, "booting into a small positive offset must not mark full");
  ChargeTrace cutoff;
  cutoff.cc();
  cutoff.hold(1000, 14.616f, 0.2f);
  expect(cutoff.anchors == 0, "an abrupt CC-to-tail drop lacks intermediate CV evidence");
  ChargeTrace low_voltage;
  low_voltage.cc();
  for (int i = 0; i <= 360; ++i)
    low_voltage.sample(13.5f, 35.0f - 34.0f * static_cast<float>(i) / 360.0f);
  low_voltage.hold(1000, 13.5f, 0.2f);
  expect(low_voltage.anchors == 0, "low-voltage current taper must not qualify full");
  for (int fault = 0; fault < 6; ++fault) {
    ChargeTrace t;
    t.cc(); t.cv(); t.hold(200, 14.616f, 0.2f);
    if (fault == 0) t.sample(NAN, 0.2f);
    if (fault == 1) t.sample(14.616f, NAN);
    if (fault == 2) t.sample(14.616f, 0.2f, false);
    if (fault == 3) t.now += 6000;
    if (fault == 4) t.sample(14.616f, -0.1f);
    if (fault == 5) t.detector.reset();
    t.hold(700, 14.616f, 0.2f);
    expect(t.anchors == 0, "invalid data, gaps, discharge and reset must discard all evidence");
  }
  ChargeTrace interrupted;
  interrupted.cc(); interrupted.cv(); interrupted.hold(200, 14.616f, 0.2f);
  interrupted.hold(31, 14.616f, 2.0f);
  interrupted.hold(300, 14.616f, 0.2f);
  expect(interrupted.anchors == 0, "tail interruption must restart dwell");
  interrupted.hold(40, 14.616f, 0.2f);
  expect(interrupted.anchors == 1, "a fresh complete dwell can qualify again");
  ChargeTrace droop;
  droop.cc(); droop.cv(); droop.hold(200, 14.616f, 0.2f);
  droop.sample(13.8f, 0.2f);
  droop.hold(700, 14.616f, 0.2f);
  expect(droop.anchors == 0, "loss of high-voltage plateau discards CV evidence");
  ChargeTrace invalid_config;
  invalid_config.config.tail_current_a = 10.0f;
  invalid_config.cc(); invalid_config.cv(); invalid_config.hold(700, 14.616f, 0.2f);
  expect(invalid_config.anchors == 0 && invalid_config.detector.phase() == ChargePhase::Unavailable,
         "invalid configuration must fail closed");
}

void test_charge_rollover_and_soc_correction() {
  ChargeTrace t;
  t.now = UINT32_MAX - 800000;
  t.cc(); t.cv(); t.hold(631, 14.616f, 0.2f);
  expect(t.anchors == 2, "phase windows and repeated anchors must survive millis rollover");
  double remaining = 270.0;  // The reported 90% of a 300 Ah battery.
  bool valid = false;
  uint32_t checkpoint[battery_monitor::SOC_CHECKPOINT_WORDS] = {};
  expect(battery_monitor::anchor_soc_full(300.0f, remaining, valid, checkpoint),
         "automatic full anchoring accepts a valid capacity even with invalid SOC");
  expect(remaining == 300.0 && valid, "automatic anchor corrects drifted 90% to valid 100%");
  expect(battery_monitor::soc_checkpoint_is_valid(checkpoint), "full anchor stages a valid checkpoint");
  expect(!battery_monitor::anchor_soc_full(NAN, remaining, valid, checkpoint),
         "invalid capacity must not change the live or persisted anchor");
  expect(remaining == 300.0 && valid, "failed anchor preserves existing state");
}

void test_charge_requires_continuous_recent_cv_evidence() {
  ChargeTrace pending;
  pending.cc();
  pending.hold(120, 14.616f, 20.0f);
  pending.hold(600, 13.5f, 35.0f);
  pending.hold(600, 14.616f, 35.0f);
  pending.hold(700, 14.616f, 0.2f);
  expect(pending.anchors == 0, "pending taper proof must also clear on renewed bulk charging");
  ChargeTrace pending_stall;
  pending_stall.cc(); pending_stall.hold(1200, 14.616f, 20.0f);
  pending_stall.hold(700, 14.616f, 0.2f);
  expect(pending_stall.anchors == 0, "constant intermediate current must replace old taper proof");
  ChargeTrace low_pending;
  low_pending.cc(); low_pending.hold(1200, 14.616f, 2.0f);
  low_pending.hold(700, 14.616f, 0.2f);
  expect(low_pending.anchors == 0, "a constant current below active threshold also replaces old taper proof");
  ChargeTrace renewed;
  renewed.hold(361, 14.616f, 35.0f);
  renewed.hold(120, 14.616f, 20.0f);
  renewed.hold(1200, 14.616f, 35.0f);
  renewed.hold(700, 14.616f, 0.2f);
  expect(renewed.anchors == 0, "renewed bulk current must require new taper evidence");
  ChargeTrace dips;
  for (int i = 0; i <= 300; ++i) {
    const float voltage = i <= 180 || i % 5 == 0 ? 14.616f : 13.5f;
    dips.sample(voltage, 35.0f - 34.0f * static_cast<float>(i) / 300.0f);
  }
  dips.hold(700, 14.616f, 0.2f);
  expect(dips.anchors == 0, "voltage dips between retained points must break plateau proof");
  ChargeTrace stalled;
  stalled.cc(); stalled.cv(); stalled.hold(1200, 14.616f, 2.0f);
  stalled.hold(700, 14.616f, 0.2f);
  expect(stalled.detector.full() && stalled.anchors >= 1,
         "recognized CV must survive a flat intermediate tail while voltage remains stable");
}

void test_charge_endpoint_boundaries() {
  ChargeTrace exact;
  exact.cc(); exact.cv(14.4f); exact.hold(331, 14.4f, 0.3f);
  expect(exact.anchors == 1, "exact configured voltage and current boundaries qualify");
  ChargeTrace above;
  above.cc(); above.cv(); above.hold(700, 14.616f, 0.31f);
  expect(above.anchors == 0, "current above the requested 0.3 A limit must not anchor");
  ChargeTrace below;
  below.cc(); below.cv(14.39f); below.hold(700, 14.39f, 0.2f);
  expect(below.anchors == 0, "voltage below the requested 14.4 V limit must not anchor");
}

// Voltage milestones are from the reported charge; the current curve is an
// illustrative taper, not a reconstruction of unrecorded measurements.
void test_gradually_rising_cv_and_slow_tail() {
  ChargeTrace t;
  t.config.tail_current_a = 0.5f;
  const int times[] = {0, 326, 1177, 1992, 2940};
  const float volts[] = {13.416f, 14.392f, 14.604f, 14.616f, 14.616f};
  bool saw_cv = false;
  bool lost_cv = false;
  for (int seconds = 0; seconds < 4200; ++seconds) {
    float voltage = 14.616f;
    for (int k = 1; k < 5; ++k) {
      if (seconds <= times[k]) {
        voltage = volts[k-1] + (volts[k]-volts[k-1]) *
            (seconds-times[k-1]) / (times[k]-times[k-1]);
        break;
      }
    }
    float current = 0.28f + 34.849f * std::exp(-seconds / 400.0f);
    if (seconds >= 2940) current = (seconds / 5) % 2 ? 0.259f : 0.336f;
    t.sample(voltage, current);
    if (t.detector.phase() == battery_monitor::ChargePhase::CV) saw_cv = true;
    if (saw_cv && t.detector.phase() == battery_monitor::ChargePhase::Observing)
      lost_cv = true;
  }
  expect(saw_cv && !lost_cv, "gradual voltage rise and slowing taper must retain recognized CV");
  expect(t.detector.full() && t.anchors >= 2,
         "reported voltage milestones with a slow taper must reach full and repeat sync");
}

void test_tail_uses_average_and_full_dwell() {
  ChargeTrace t;
  t.config.tail_current_a = 0.5f;
  t.cc(); t.cv();
  // Average 0.45 A, although individual samples exceed the 0.5 A threshold.
  for (int i = 0; i < 301; ++i) t.sample(14.616f, i % 2 ? 0.6f : 0.3f);
  expect(t.anchors == 0, "averaging must settle before the full five-minute tail dwell");
  for (int i = 0; i < 60; ++i) t.sample(14.616f, i % 2 ? 0.6f : 0.3f);
  expect(t.detector.full() && t.anchors == 1,
         "tail eligibility must use the 30-second average instead of individual steps");
  t.hold(31, 14.616f, 0.8f);
  expect(!t.detector.full(), "sustained current above tail must clear full after averaging");
  t.hold(301, 14.616f, 0.3f);
  expect(t.anchors == 1, "renewed tail must wait for averaging and a fresh dwell");
  t.hold(40, 14.616f, 0.3f);
  expect(t.detector.full() && t.anchors == 2, "renewed averaged tail must qualify again");
  t.sample(14.616f, -0.01f);
  expect(!t.detector.full(), "averaging must never conceal a discharge sample");
}

void test_tail_average_weights_time_not_sample_count() {
  ChargeTrace t;
  t.config.tail_current_a = 0.5f;
  t.cc(); t.cv();
  const uint32_t start = t.now;
  // Equal time at 0.6/0.3 A means 0.45 A average, but sample the higher
  // value ten times more often. A sample-count average would never qualify.
  while (t.now - start < 360000U) {
    const bool high = (t.now - start) % 20000U < 10000U;
    if (t.detector.update(t.now, 14.616f, high ? 0.6f : 0.3f, true, t.config))
      ++t.anchors;
    t.now += high ? 100U : 1000U;
  }
  expect(t.detector.full() && t.anchors == 1,
         "uneven sample frequency must not distort the time-weighted tail average");

  ChargeTrace above;
  above.config.tail_current_a = 0.5f;
  above.cc(); above.cv();
  const uint32_t second_start = above.now;
  // Most elapsed time is above threshold, although most samples are below it.
  while (above.now - second_start < 700000U) {
    const bool high = (above.now - second_start) % 11000U < 10000U;
    if (above.detector.update(above.now, 14.616f, high ? 0.6f : 0.3f, true, above.config))
      ++above.anchors;
    above.now += high ? 1000U : 100U;
  }
  expect(above.anchors == 0, "frequent low samples must not conceal a high time-average current");
}

void test_cv_requires_fresh_voltage_stability() {
  ChargeTrace t;
  t.cc(); t.cv(); t.hold(400, 14.616f, 0.2f);
  expect(t.detector.full(), "test starts with a qualified full condition");
  const int previous_anchors = t.anchors;
  // No individual step exceeds 0.15 V, but the recent window spans 0.2 V.
  for (int i = 1; i <= 100; ++i) t.sample(14.616f + i * 0.002f, 0.2f);
  expect(!t.detector.full(), "a wide recent voltage window must clear the full indication");
  t.hold(300, 14.816f, 0.2f);
  expect(t.anchors == previous_anchors, "full dwell must wait for a fresh stable voltage window");
  t.hold(301, 14.816f, 0.2f);
  expect(t.detector.full() && t.anchors == previous_anchors + 1,
         "a new stable high plateau can qualify without relearning substantial charge");
  t.sample(15.1f, 0.2f);
  t.hold(700, 15.1f, 0.2f);
  expect(!t.detector.full() && t.anchors == previous_anchors + 1,
         "an abrupt voltage jump must discard process history");
}

void test_current_direction_hysteresis() {
  using battery_monitor::CurrentDirection;
  battery_monitor::CurrentDirectionDetector d;
  uint32_t now = UINT32_MAX - 15000U;
  auto sample = [&](float current) {
    expect(d.update(now, current, 0.3f, 0.1f, 10000U, 5000U),
           "finite current and valid hysteresis must be accepted");
    now += 1000;
  };
  for (int i = 0; i < 10; ++i) sample(35.0f);
  expect(d.state() == CurrentDirection::Idle, "charging indication must wait ten full seconds");
  sample(35.0f);
  expect(d.state() == CurrentDirection::Charging, "sustained current must indicate charging");
  for (int i = 0; i < 120; ++i) sample((i / 5) % 2 ? 0.259f : 0.198f);
  expect(d.state() == CurrentDirection::Charging,
         "reported steps around 0.2 A must not chatter after charging is established");
  for (int i = 0; i < 10; ++i) sample(0.061f);
  expect(d.state() == CurrentDirection::Charging, "brief low current must not clear charging");
  sample(0.061f);
  expect(d.state() == CurrentDirection::Idle, "ten seconds below exit threshold must become idle");
  for (int i = 0; i < 120; ++i) sample((i / 5) % 2 ? 0.137f : 0.061f);
  expect(d.state() == CurrentDirection::Idle, "small tail current must not restart charging indication");
  for (int i = 0; i < 11; ++i) sample(-1.0f);
  expect(d.state() == CurrentDirection::Discharging, "negative current must use symmetric hysteresis");
  for (int i = 0; i < 60; ++i) sample(-0.198f);
  expect(d.state() == CurrentDirection::Discharging, "discharge state must survive the hysteresis band");
  now += 6000;
  sample(35.0f);
  expect(d.state() == CurrentDirection::Idle, "a sample gap must discard pending and settled direction");
  for (int i = 0; i < 10; ++i) sample(35.0f);
  expect(d.state() == CurrentDirection::Charging, "direction can settle after data recovers");
  expect(!d.update(now, NAN, 0.3f, 0.1f, 10000U, 5000U), "invalid current must be rejected");
  expect(d.state() == CurrentDirection::Idle, "invalid input must clear the direction state");
  expect(!d.update(now, 1.0f, 0.1f, 0.3f, 10000U, 5000U), "inverted hysteresis must be rejected");
}

void test_direction_exit_is_independent_of_opposite_entry() {
  using battery_monitor::CurrentDirection;
  for (float sign : {1.0f, -1.0f}) {
    battery_monitor::CurrentDirectionDetector d;
    for (uint32_t t = 1000; t <= 11000; t += 1000)
      d.update(t, sign, 0.3f, 0.1f, 10000U, 5000U);
    for (uint32_t t = 12000; t <= 22000; t += 1000) {
      const float current = ((t - 12000) / 5000) % 2 ? -sign : 0.0f;
      d.update(t, current, 0.3f, 0.1f, 10000U, 5000U);
    }
    expect(d.state() == CurrentDirection::Idle,
           "continuous exit condition must clear direction even when opposite entry is interrupted");
  }
}

void test_cv_voltage_window_uses_elapsed_time() {
  for (uint32_t cadence : {1000U, 1100U}) {
    ChargeTrace t;
    t.config.tail_current_a = 0.5f;
    t.cc(); t.cv();
    const uint32_t start = t.now;
    while (t.now - start <= 900000U) {
      const float voltage = 14.616f + 0.144f * ((t.now - start) / 300000.0f);
      if (t.detector.update(t.now, voltage, 0.3f, true, t.config)) ++t.anchors;
      t.now += cadence;
    }
    expect(t.detector.full() && t.anchors >= 2,
           "slightly irregular samples must not stretch the five-minute voltage window");
  }
}

void test_float_codec() {
  const float values[] = {0.0f, -0.0f, 1.0f, -123.5f, 98765.25f};
  for (float value : values) {
    const float decoded = battery_monitor::decode_float(battery_monitor::encode_float(value));
    expect(std::memcmp(&value, &decoded, sizeof(value)) == 0,
           "float encoding must preserve every bit");
  }
}

void test_soc_checkpoint() {
  uint32_t record[battery_monitor::SOC_CHECKPOINT_WORDS] = {};
  battery_monitor::store_soc_checkpoint(record, 151.25f, 300.0f, true);

  expect(battery_monitor::soc_checkpoint_is_valid(record),
         "stored SOC checkpoint must validate");
  expect_near(battery_monitor::decode_float(
                  record[battery_monitor::SOC_CHECKPOINT_REMAINING_AH_BITS_INDEX]),
              151.25f, 0.0001f, "checkpoint must retain remaining Ah");
  expect_near(battery_monitor::decode_float(
                  record[battery_monitor::SOC_CHECKPOINT_CAPACITY_AH_BITS_INDEX]),
              300.0f, 0.0001f, "checkpoint must retain associated capacity");
  expect(battery_monitor::has_flag(record[battery_monitor::SOC_CHECKPOINT_FLAGS_INDEX],
                                   battery_monitor::SOC_CHECKPOINT_VALID_FLAG),
         "checkpoint must retain the valid flag");

  record[battery_monitor::SOC_CHECKPOINT_MAGIC_INDEX] = 0U;
  expect(!battery_monitor::soc_checkpoint_is_valid(record),
         "checkpoint with invalid magic must fail validation");

  battery_monitor::store_soc_checkpoint(record, 151.25f, 300.0f, false);
  expect(battery_monitor::soc_checkpoint_is_valid(record),
         "invalid-SOC checkpoint is still a structurally valid record");
  expect(!battery_monitor::has_flag(record[battery_monitor::SOC_CHECKPOINT_FLAGS_INDEX],
                                    battery_monitor::SOC_CHECKPOINT_VALID_FLAG),
         "invalid-SOC checkpoint must clear the valid flag");

  battery_monitor::store_soc_checkpoint(
      record, 151.25f, std::numeric_limits<float>::quiet_NaN(), true);
  expect(!battery_monitor::soc_checkpoint_is_valid(record),
         "checkpoint with non-finite capacity must fail validation");
}

void test_rule_latch_record() {
  uint32_t record[battery_monitor::RULE_LATCH_WORDS] = {};
  expect(!battery_monitor::rule_latch_record_is_valid(record),
         "zeroed latch record must be invalid");

  battery_monitor::reset_rule_latches(record);
  expect(battery_monitor::rule_latch_record_is_valid(record),
         "reset latch record must validate");
  expect(!battery_monitor::rule_latch_value(
             record, battery_monitor::RULE_LATCH_STOP_CHARGE_FLAG),
         "reset latch must be clear");

  battery_monitor::set_rule_latch(record, battery_monitor::RULE_LATCH_STOP_CHARGE_FLAG, true);
  battery_monitor::set_rule_latch(record,
                                  battery_monitor::RULE_LATCH_CAPACITY_WARNING_FLAG, true);
  expect(battery_monitor::rule_latch_value(
             record, battery_monitor::RULE_LATCH_STOP_CHARGE_FLAG),
         "set stop-charge latch must read back true");
  expect(battery_monitor::rule_latch_value(
             record, battery_monitor::RULE_LATCH_CAPACITY_WARNING_FLAG),
         "set warning latch must read back true");

  battery_monitor::set_rule_latch(record, battery_monitor::RULE_LATCH_STOP_CHARGE_FLAG, false);
  expect(!battery_monitor::rule_latch_value(
             record, battery_monitor::RULE_LATCH_STOP_CHARGE_FLAG),
         "cleared stop-charge latch must read back false");
  expect(battery_monitor::rule_latch_value(
             record, battery_monitor::RULE_LATCH_CAPACITY_WARNING_FLAG),
         "clearing one latch must preserve other latch bits");
}

void test_soc_math() {
  expect_near(battery_monitor::soc_percent(150.0f, 300.0f), 50.0f, 0.0001f,
              "SOC must be an Ah ratio");
  expect_near(battery_monitor::soc_percent(330.0f, 300.0f), 110.0f, 0.0001f,
              "SOC must remain unbounded above 100 percent");
  expect_near(battery_monitor::soc_percent(-30.0f, 300.0f), -10.0f, 0.0001f,
              "SOC must remain unbounded below zero percent");
  expect(std::isnan(battery_monitor::soc_percent(1.0f, 0.0f)),
         "zero capacity must produce an invalid SOC result");

  const float rescaled = battery_monitor::rescale_remaining_ah(150.0f, 300.0f, 420.0f);
  expect_near(rescaled, 210.0f, 0.0001f,
              "capacity change must scale remaining Ah by the capacity ratio");
  expect_near(battery_monitor::soc_percent(rescaled, 420.0f), 50.0f, 0.0001f,
              "capacity rescaling must preserve unbounded SOC");
  expect(std::isnan(battery_monitor::rescale_remaining_ah(1.0f, 0.0f, 300.0f)),
         "capacity rescaling must reject a non-positive old capacity");

  expect_near(battery_monitor::integrate_remaining_ah(100.0f, 10.0f, 90.0f, 3600000U),
              109.0f, 0.0001f,
              "charging integration must apply charging efficiency");
  expect_near(battery_monitor::integrate_remaining_ah(100.0f, -10.0f, 90.0f, 3600000U),
              90.0f, 0.0001f,
              "discharging integration must not apply charging efficiency");
  expect_near(battery_monitor::integrate_remaining_ah(100.0f, 20.0f, 100.0f, 1800000U),
              110.0f, 0.0001f, "elapsed milliseconds must convert to amp-hours");
  expect_near(battery_monitor::integrate_remaining_ah(
                  100.0f, std::numeric_limits<float>::quiet_NaN(), 99.0f, 1000U),
              100.0f, 0.0001f, "non-finite current must not alter remaining Ah");
}

void test_low_current_soc_precision() {
  // A float accumulator rounds each 1 s increment at 300 Ah. These
  // month-long cases catch both exaggerated discharge and lost increments.
  const struct {
    double current_a;
    double efficiency;
    double expected_ah;
  } cases[] = {
      {-0.06, 99.0, 256.8},
      {-0.04, 99.0, 271.2},
      {0.06, 99.0, 342.768},
      {0.04, 100.0, 328.8},
  };
  for (const auto &sample : cases) {
    double remaining = 300.0;
    for (uint32_t second = 0; second < 30U * 24U * 3600U; ++second) {
      remaining = battery_monitor::integrate_remaining_ah(
          remaining, sample.current_a, sample.efficiency, 1000U);
      if (second % 900U == 0U) {
        uint32_t checkpoint[battery_monitor::SOC_CHECKPOINT_WORDS] = {};
        battery_monitor::store_soc_checkpoint(checkpoint, remaining, 300.0f, true);
      }
    }
    expect_near(remaining, sample.expected_ah, 0.00001,
                "month-long small currents must retain their amp-hours");
  }

  // Capacity changes and invalid samples must not narrow the live accumulator.
  const double remaining = 300.000001;
  expect_near(battery_monitor::rescale_remaining_ah(remaining, 300.0, 600.0),
              600.000002, 1e-10, "capacity rescaling must retain live precision");
  expect_near(battery_monitor::soc_percent(remaining, 300.0),
              100.00000033333333, 1e-10, "SOC ratio must retain live precision");
  expect_near(battery_monitor::integrate_remaining_ah(remaining, NAN, 99.0, 1000U),
              remaining, 1e-10, "invalid samples must preserve the precise accumulator");

  uint32_t checkpoint[battery_monitor::SOC_CHECKPOINT_WORDS] = {};
  battery_monitor::store_soc_checkpoint(checkpoint, remaining, 300.0f, true);
  // Golden five-word version-1 record: magic, version, float 300 Ah twice, valid.
  const uint32_t legacy_record[] = {0x534F4343U, 1U, 0x43960000U, 0x43960000U, 1U};
  expect(sizeof(checkpoint) == sizeof(legacy_record) &&
             std::memcmp(checkpoint, legacy_record, sizeof(checkpoint)) == 0,
         "double live state must produce the legacy float checkpoint format");
}

void test_hysteresis() {
  expect(!battery_monitor::update_upward_latch(false, 94.9f, 95.0f, 2.0f),
         "upward latch must remain clear below the assert level");
  expect(battery_monitor::update_upward_latch(false, 95.0f, 95.0f, 2.0f),
         "upward latch must assert at its level");
  expect(battery_monitor::update_upward_latch(true, 94.0f, 95.0f, 2.0f),
         "upward latch must hold inside the hysteresis band");
  expect(!battery_monitor::update_upward_latch(true, 93.0f, 95.0f, 2.0f),
         "upward latch must clear at level minus hysteresis");

  expect(battery_monitor::update_downward_latch(false, 40.0f, 40.0f, 2.0f),
         "downward latch must assert at its level");
  expect(battery_monitor::update_downward_latch(true, 41.0f, 40.0f, 2.0f),
         "downward latch must hold inside the hysteresis band");
  expect(!battery_monitor::update_downward_latch(true, 42.0f, 40.0f, 2.0f),
         "downward latch must clear at level plus hysteresis");
  expect(!battery_monitor::update_downward_latch(false, 41.0f, 40.0f, 2.0f),
         "clear downward latch must remain clear inside the band");

  const float nan = std::numeric_limits<float>::quiet_NaN();
  expect(battery_monitor::update_upward_latch(true, nan, 95.0f, 2.0f),
         "non-finite upward input must preserve prior state");
  expect(!battery_monitor::update_downward_latch(false, nan, 40.0f, 2.0f),
         "non-finite downward input must preserve prior state");
}

void test_plausibility_and_rule_configuration() {
  expect(!battery_monitor::soc_is_suspect(false, 150.0f, -10.0f, 110.0f),
         "invalid SOC must not be labeled suspect");
  expect(!battery_monitor::soc_is_suspect(true, 110.0f, -10.0f, 110.0f),
         "plausibility endpoints must be inclusive");
  expect(battery_monitor::soc_is_suspect(true, 110.1f, -10.0f, 110.0f),
         "valid SOC above the plausibility range must be suspect");

  expect(battery_monitor::soc_rule_configuration_is_valid(95.0f, 2.0f, 40.0f, 2.0f,
                                                          30.0f, 2.0f),
         "default rule configuration must validate");
  expect(!battery_monitor::soc_rule_configuration_is_valid(95.0f, 2.0f, 40.0f, 2.0f,
                                                           40.0f, 2.0f),
         "equal stop-load and warning levels must be invalid");
  expect(!battery_monitor::soc_rule_configuration_is_valid(95.0f, -0.1f, 40.0f, 2.0f,
                                                           30.0f, 2.0f),
         "negative hysteresis must be invalid");
  expect(!battery_monitor::soc_rule_configuration_is_valid(
             std::numeric_limits<float>::quiet_NaN(), 2.0f, 40.0f, 2.0f, 30.0f, 2.0f),
         "non-finite rule values must be invalid");
}

void test_event_journal_validation() {
  uint32_t record[battery_monitor::EVENT_JOURNAL_WORDS] = {};
  record[battery_monitor::EVENT_JOURNAL_MAGIC_INDEX] = battery_monitor::EVENT_JOURNAL_MAGIC;
  record[battery_monitor::EVENT_JOURNAL_VERSION_INDEX] =
      battery_monitor::EVENT_JOURNAL_VERSION;
  record[battery_monitor::EVENT_JOURNAL_SEQUENCE_INDEX] = 1U;
  record[battery_monitor::EVENT_JOURNAL_SOC_PERCENT_BITS_INDEX] =
      battery_monitor::encode_float(39.875f);
  record[battery_monitor::EVENT_JOURNAL_RULE_CODE_INDEX] = 2U;
  record[battery_monitor::EVENT_JOURNAL_DIRECTION_INDEX] =
      battery_monitor::EVENT_DIRECTION_DOWN;

  expect(battery_monitor::event_journal_is_valid(record),
         "well-formed event journal must validate");
  expect_near(battery_monitor::decode_float(
                  record[battery_monitor::EVENT_JOURNAL_SOC_PERCENT_BITS_INDEX]),
              39.875f, 0.0001f, "event journal must retain unbounded SOC");

  record[battery_monitor::EVENT_JOURNAL_SEQUENCE_INDEX] = 0U;
  expect(!battery_monitor::event_journal_is_valid(record),
         "event sequence zero must be reserved and invalid");
  record[battery_monitor::EVENT_JOURNAL_SEQUENCE_INDEX] = 1U;
  record[battery_monitor::EVENT_JOURNAL_SOC_PERCENT_BITS_INDEX] =
      battery_monitor::encode_float(std::numeric_limits<float>::infinity());
  expect(!battery_monitor::event_journal_is_valid(record),
         "event with non-finite SOC must fail validation");
}

void test_event_recording_contract() {
  uint32_t record[battery_monitor::EVENT_JOURNAL_WORDS] = {};
  battery_monitor::store_soc_event(record, 2U, -1, 39.875f, true, true,
                                  12U, 1700000000U, true);
  const uint32_t expected[] = {
      0x534F4345U, 1U, 1U, 12U, 1700000000U, 0x421F8000U, 2U, 2U, 7U};
  expect(std::memcmp(record, expected, sizeof(expected)) == 0,
         "event recording must preserve the nine-word legacy record and flags");
  expect(std::strcmp(battery_monitor::soc_rule_name(2U), "capacity_warning") == 0,
         "event and diagnostic consumers must share the public rule name");
  expect(std::strcmp(battery_monitor::soc_rule_name(0U), "unknown") == 0 &&
             std::strcmp(battery_monitor::soc_rule_name(0U, "none"), "none") == 0 &&
             std::strcmp(battery_monitor::soc_rule_name(0U, "replay"), "replay") == 0,
         "unknown rules must retain each consumer's existing fallback");
  record[battery_monitor::EVENT_JOURNAL_SEQUENCE_INDEX] = UINT32_MAX;
  battery_monitor::store_soc_event(record, 1U, 1, 101.0f, false, false, 13U, 99U, false);
  expect(record[battery_monitor::EVENT_JOURNAL_SEQUENCE_INDEX] == 1U &&
             record[battery_monitor::EVENT_JOURNAL_DIRECTION_INDEX] == 1U &&
             record[battery_monitor::EVENT_JOURNAL_EPOCH_SECONDS_INDEX] == 0U &&
             record[battery_monitor::EVENT_JOURNAL_FLAGS_INDEX] == 0U,
         "sequence rollover must skip zero and invalid timestamps must be cleared");
  record[battery_monitor::EVENT_JOURNAL_MAGIC_INDEX] = 0U;
  battery_monitor::store_soc_event(record, 3U, -1, 29.0f, true, false, 14U, 0U, false);
  expect(record[battery_monitor::EVENT_JOURNAL_SEQUENCE_INDEX] == 1U,
         "an invalid previous journal must restart its sequence at one");
}

void test_rule_engine_lifecycle() {
  const battery_monitor::SocRuleConfig config{95.0f, 2.0f, 40.0f, 2.0f, 30.0f, 2.0f};
  uint32_t latches[battery_monitor::RULE_LATCH_WORDS] = {};
  bool armed = false;
  auto result = battery_monitor::evaluate_soc_rules(latches, 25.0f, config, true, false, armed);
  expect(!result.ready && !armed && result.crossings == 0U && latches[2] == 6U,
         "startup must reconstruct low-SOC latches without readiness or events");
  result = battery_monitor::evaluate_soc_rules(latches, 31.0f, config, true, true, armed);
  expect(result.ready && armed && result.crossings == 0U && latches[2] == 6U,
         "arming must retain restored latches inside hysteresis without events");
  result = battery_monitor::evaluate_soc_rules(latches, 100.0f, config, true, true, armed);
  expect(result.ready && result.crossings == 1U && latches[2] == 1U,
         "rising through the upper rule must emit only stop-charge activation");
  result = battery_monitor::evaluate_soc_rules(latches, 94.0f, config, true, true, armed);
  expect(result.crossings == 0U && latches[2] == 1U,
         "the upper latch must hold inside its hysteresis band");
  result = battery_monitor::evaluate_soc_rules(latches, 25.0f, config, true, true, armed);
  expect(result.crossings == 6U && latches[2] == 6U,
         "a large fall must report warning and stop-load activations together");
  result = battery_monitor::evaluate_soc_rules(latches, 100.0f, config, false, true, armed);
  expect(!result.ready && !armed && result.crossings == 0U && latches[2] == 6U,
         "lost measurement or SOC authority must disarm and preserve stored latches");
  result = battery_monitor::evaluate_soc_rules(latches, 100.0f, config, true, true, armed);
  expect(result.ready && result.crossings == 0U && latches[2] == 1U,
         "recovery must baseline current conditions without a synthetic crossing");
  auto invalid = config;
  invalid.stop_load_level = invalid.capacity_warning_level;
  result = battery_monitor::evaluate_soc_rules(latches, 25.0f, invalid, true, true, armed);
  expect(!result.ready && !armed && latches[2] == 1U,
         "invalid threshold ordering must preserve latches and disarm the engine");
  result = battery_monitor::evaluate_soc_rules(latches, 25.0f, config, true, true, armed);
  expect(result.ready && result.crossings == 0U && latches[2] == 6U,
         "configuration recovery must baseline without events");
  armed = false;  // A valid runtime setting edit also requests reconstruction.
  result = battery_monitor::evaluate_soc_rules(latches, 100.0f, config, true, true, armed);
  expect(result.ready && result.crossings == 0U && latches[2] == 1U,
         "valid setting edits must not fabricate crossings");
}

void test_sample_freshness() {
  expect(!battery_monitor::sample_is_fresh(100U, 0U, 5000U),
         "the legacy zero timestamp must remain an absent-sample sentinel");
  expect(battery_monitor::sample_is_fresh(6000U, 1000U, 5000U),
         "a sample at the stale boundary must remain fresh");
  expect(!battery_monitor::sample_is_fresh(6001U, 1000U, 5000U),
         "a sample past the stale boundary must be rejected");
  expect(battery_monitor::sample_is_fresh(500U, UINT32_MAX - 499U, 1000U),
         "freshness subtraction must remain safe through millis rollover");
}

void test_parallel_branch_current() {
  using battery_monitor::parallel_branch_current;
  expect_near(parallel_branch_current(10.0f, false, NAN, false), 10.0, 1e-6,
              "an absent auxiliary sensor must preserve single-shunt accounting");
  expect_near(parallel_branch_current(0.0f, true, 0.06f, true), -0.06, 1e-7,
              "auxiliary consumption must count when main current is zero");
  expect_near(parallel_branch_current(10.0f, true, 0.06f, true), 9.94, 1e-6,
              "auxiliary consumption must reduce net charging current");
  expect_near(parallel_branch_current(-10.0f, true, 0.06f, true), -10.06, 1e-6,
              "auxiliary consumption must add to main-branch discharge");
  expect_near(parallel_branch_current(0.0f, true, -0.06f, true), 0.06, 1e-7,
              "signed auxiliary measurements must not be rectified");
  expect(std::isnan(parallel_branch_current(0.0f, true, 0.06f, false)),
         "a detected sensor with stale data must not silently become absent");
  expect(std::isnan(parallel_branch_current(10.0f, true, NAN, true)),
         "a detected sensor with invalid data must invalidate combined current");
  expect(std::isnan(parallel_branch_current(NAN, true, 0.06f, true)),
         "auxiliary sensing cannot replace missing main-branch measurements");

  double remaining = 300.0;
  double charged = 0.0, discharged = 0.0;
  uint32_t previous = 0;
  bool baseline = false;
  for (uint32_t second = 1; second <= 3601; ++second) {
    const float current = parallel_branch_current(0.0f, true, 0.06f, true);
    if (second > 1)
      remaining = battery_monitor::integrate_remaining_ah(remaining, current, 99.0, 1000U);
    battery_monitor::integrate_energy_sample(12.8f * current, second * 1000U,
        5000U, charged, discharged, previous, baseline);
  }
  expect_near(remaining, 299.94, 1e-7,
              "one hour of auxiliary-only discharge must remove 0.06 Ah");
  expect_near(discharged, 0.000768, 1e-10,
              "auxiliary-only consumption must appear in battery energy totals");
  expect(charged == 0.0, "auxiliary discharge must not accumulate charged energy");
}

void test_energy_tracking() {
  double charged = 0.0, discharged = 0.0;
  uint32_t previous_ms = 0;
  bool baseline = false;
  battery_monitor::integrate_energy_sample(1000.0f, 1000U, 5000U,
      charged, discharged, previous_ms, baseline);
  expect(charged == 0.0, "first sample must only establish a baseline");
  for (uint32_t n = 2; n <= 3601; ++n)
    battery_monitor::integrate_energy_sample(1000.0f, n * 1000U, 5000U,
        charged, discharged, previous_ms, baseline);
  expect(std::abs(charged - 1.0) < 1e-10, "1000 W for one hour must charge 1 kWh");
  battery_monitor::integrate_energy_sample(-500.0f, 3602000U, 5000U,
      charged, discharged, previous_ms, baseline);
  expect(std::abs(discharged - 0.0001388888888888889) < 1e-12,
         "negative power must increase only discharged energy");
  const double before = charged;
  battery_monitor::integrate_energy_sample(1000.0f, 3700000U, 5000U,
      charged, discharged, previous_ms, baseline);
  expect(charged == before, "long sample gaps must not accumulate energy");
  battery_monitor::integrate_energy_sample(NAN, 3701000U, 5000U,
      charged, discharged, previous_ms, baseline);
  battery_monitor::integrate_energy_sample(1000.0f, 3702000U, 5000U,
      charged, discharged, previous_ms, baseline);
  expect(charged == before, "first sample after invalid data must rebaseline");
  previous_ms = UINT32_MAX - 499U;
  battery_monitor::integrate_energy_sample(1000.0f, 500U, 5000U,
      charged, discharged, previous_ms, baseline);
  expect(std::abs(charged - before - 1.0 / 3600.0) < 1e-12,
         "millis rollover must preserve elapsed time");

  uint32_t record[6] = {};
  double restored_charge = 0.0, restored_discharge = 0.0;
  expect(!battery_monitor::restore_energy_checkpoint(record, restored_charge,
      restored_discharge), "empty energy checkpoint must be rejected");
  battery_monitor::store_energy_checkpoint(record, charged, discharged);
  expect(battery_monitor::restore_energy_checkpoint(record, restored_charge,
      restored_discharge), "energy checkpoint must restore");
  expect(restored_charge == charged && restored_discharge == discharged,
         "checkpoint must preserve both double precision totals exactly");
  battery_monitor::store_energy_checkpoint(record, -1.0, discharged);
  expect(!battery_monitor::restore_energy_checkpoint(record, restored_charge,
      restored_discharge), "negative totals must be rejected");
  battery_monitor::store_energy_checkpoint(record, INFINITY, discharged);
  expect(!battery_monitor::restore_energy_checkpoint(record, restored_charge,
      restored_discharge), "nonfinite totals must be rejected");
}

}  // namespace

int main() {
  test_tail_average_weights_time_not_sample_count();
  test_cv_requires_fresh_voltage_stability();
  test_direction_exit_is_independent_of_opposite_entry();
  test_cv_voltage_window_uses_elapsed_time();
  test_current_direction_hysteresis();
  test_gradually_rising_cv_and_slow_tail();
  test_tail_uses_average_and_full_dwell();
  test_charge_process();
  test_charge_rejections();
  test_charge_rollover_and_soc_correction();
  test_charge_requires_continuous_recent_cv_evidence();
  test_charge_endpoint_boundaries();
  test_energy_tracking();
  test_float_codec();
  test_soc_checkpoint();
  test_rule_latch_record();
  test_soc_math();
  test_low_current_soc_precision();
  test_hysteresis();
  test_plausibility_and_rule_configuration();
  test_event_journal_validation();
  test_event_recording_contract();
  test_rule_engine_lifecycle();
  test_sample_freshness();
  test_parallel_branch_current();

  if (failures != 0) {
    std::cerr << failures << " deterministic helper test(s) failed\n";
    return 1;
  }
  std::cout << "All deterministic battery-monitor helper tests passed\n";
  return 0;
}
