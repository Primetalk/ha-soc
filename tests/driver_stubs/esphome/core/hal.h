#pragma once
#include <cstdint>
namespace esphome {
inline uint32_t test_millis = 0;
inline uint32_t millis() { return test_millis; }
inline void delay(uint32_t ms) { test_millis += ms; }
}
