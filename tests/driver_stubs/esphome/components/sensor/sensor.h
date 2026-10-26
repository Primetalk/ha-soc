#pragma once
#include <cmath>
#include <vector>
namespace esphome::sensor {
class Sensor {
 public:
  float state{NAN};
  std::vector<float> samples;
  void publish_state(float value) { state = value; samples.push_back(value); }
};
}  // namespace esphome::sensor
