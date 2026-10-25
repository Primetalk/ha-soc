#pragma once

// Compatibility umbrella for ESPHome lambdas and existing host consumers.
// Persisted globals remain primitive arrays: ESPHome declares them before includes.
#include "battery_math.h"
#include "current_direction.h"
#include "measurement_validity.h"
#include "persistence.h"
#include "sensor_availability.h"
#include "soc_rules.h"
