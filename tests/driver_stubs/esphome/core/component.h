#pragma once
#include <cmath>
#include <cstdint>

namespace esphome {
inline bool app_error = false;
class PollingComponent {
 public:
  virtual ~PollingComponent() = default;
  virtual void setup() {}
  virtual void dump_config() {}
  virtual void update() {}
  virtual void on_powerdown() {}
  void mark_failed() { failed_ = true; app_error = true; }
  bool is_failed() const { return failed_; }
  void reset_to_construction_state() {
    if (failed_) { failed_ = false; app_error = false; }
  }
  void status_set_warning() { warning_ = true; }
  void status_clear_warning() { warning_ = false; }
  bool status_has_warning() const { return warning_; }

 private:
  bool failed_{false};
  bool warning_{false};
};
}  // namespace esphome
