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
  t.hold(300, 14.616f, 0.2f);
  expect(t.detector.phase() == ChargePhase::AlmostFull,
         "tail current must remain a candidate until five full minutes elapse");
  expect(t.anchors == 0, "no premature tail anchor");
  t.hold(1, 14.616f, 0.2f);
  expect(t.detector.full(), "qualified tail must expose Full charge detected");
  expect(t.anchors == 1, "qualified tail must request one automatic anchor");
  t.hold(299, 14.616f, 0.2f);
  expect(t.anchors == 1, "continued tail must not anchor on every sample");
  t.hold(1, 14.616f, 0.2f);
  expect(t.anchors == 2, "continued tail must re-anchor after another five minutes");
  t.sample(14.616f, 0.0f);
  expect(!t.detector.full(), "zero current must cancel full detection immediately");
  t.hold(700, 14.616f, 0.2f);
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
  interrupted.sample(14.616f, 2.0f);
  interrupted.hold(300, 14.616f, 0.2f);
  expect(interrupted.anchors == 0, "tail interruption must restart dwell");
  interrupted.hold(1, 14.616f, 0.2f);
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
  t.cc(); t.cv(); t.hold(601, 14.616f, 0.2f);
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
  expect(stalled.anchors == 0, "a stalled intermediate current must not retain CV proof forever");
}

void test_charge_endpoint_boundaries() {
  ChargeTrace exact;
  exact.cc(); exact.cv(14.4f); exact.hold(301, 14.4f, 0.3f);
  expect(exact.anchors == 1, "exact configured voltage and current boundaries qualify");
  ChargeTrace above;
  above.cc(); above.cv(); above.hold(700, 14.616f, 0.31f);
  expect(above.anchors == 0, "current above the requested 0.3 A limit must not anchor");
  ChargeTrace below;
  below.cc(); below.cv(14.39f); below.hold(700, 14.39f, 0.2f);
  expect(below.anchors == 0, "voltage below the requested 14.4 V limit must not anchor");
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

  if (failures != 0) {
    std::cerr << failures << " deterministic helper test(s) failed\n";
    return 1;
  }
  std::cout << "All deterministic battery-monitor helper tests passed\n";
  return 0;
}
