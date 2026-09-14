#pragma once

#include <cmath>
#include <cstdint>

namespace esphome {
namespace my_component {

// One mains half-cycle in microseconds: 10000 us = 50 Hz. A firing delay is
// measured from the zero-cross and ranges 0 (full conduction) to this value
// (no conduction).
constexpr uint32_t HALF_PERIOD_US = 10000u;

// Firing delay (us) that delivers `power_fraction` (0..1) of full power into a
// resistive load under phase control. Inverts
//   p(a) = ((pi - a) + sin(2a)/2) / pi,   a = firing angle in [0, pi]
// by bisection (14 iterations = 0.6 us resolution). Runs off-ISR, on state
// changes only. float: the ESP32-C3 has no FPU, so software double would be
// several times slower for no accuracy the 1 us timer can use.
inline uint32_t power_to_delay_us(float power_fraction) {
  constexpr float PI = 3.14159265358979323846f;
  if (power_fraction <= 0.f) return HALF_PERIOD_US;
  if (power_fraction >= 1.f) return 0;
  float lo = 0.f, hi = PI;
  for (int i = 0; i < 14; i++) {
    float a = 0.5f * (lo + hi);
    float p = ((PI - a) + 0.5f * std::sin(2.f * a)) / PI;
    if (p > power_fraction) {
      lo = a;  // p decreases with a, need a larger angle
    } else {
      hi = a;
    }
  }
  return (uint32_t)(0.5f * (lo + hi) / PI * HALF_PERIOD_US + 0.5f);
}

}  // namespace my_component
}  // namespace esphome
