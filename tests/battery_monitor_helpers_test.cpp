#define BATTERY_MONITOR_HOST_TEST
#include "include/battery_monitor_types.h"

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

void expect_near(float actual, float expected, float tolerance, const char *description) {
  if (std::isfinite(actual) && std::fabs(actual - expected) <= tolerance) return;
  std::cerr << "FAIL: " << description << " (expected " << expected << ", got " << actual
            << ")\n";
  failures++;
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

}  // namespace

int main() {
  test_float_codec();
  test_soc_checkpoint();
  test_rule_latch_record();
  test_soc_math();
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
