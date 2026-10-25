#pragma once

#ifndef BATTERY_MONITOR_HOST_TEST
#include "esphome/components/sensor/sensor.h"
#include "esphome/core/controller_registry.h"

namespace battery_monitor {

// Numeric sensors do not derive from StatefulEntityBase in ESPHome 2026.8.0,
// so they have no invalidate_state() method. Clear the authoritative state flag
// and notify registered frontends so the native API emits missing_state=true.
// A later publish_state() call restores availability through the normal path.
inline void invalidate_sensor_state(esphome::sensor::Sensor *sensor) {
  if (sensor == nullptr || !sensor->has_state()) return;
  sensor->set_has_state(false);
#if defined(USE_SENSOR) && defined(USE_CONTROLLER_REGISTRY)
  esphome::ControllerRegistry::notify_sensor_update(sensor);
#endif
}

}  // namespace battery_monitor
#endif
