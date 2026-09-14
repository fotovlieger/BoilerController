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
// by bisection. Runs off-ISR, on state changes only.
inline uint32_t power_to_delay_us(double power_fraction) {
  constexpr double PI = 3.14159265358979323846;
  if (power_fraction <= 0.) return HALF_PERIOD_US;
  if (power_fraction >= 1.) return 0;
  double lo = 0., hi = PI;
  for (int i = 0; i < 24; i++) {
    double a = 0.5 * (lo + hi);
    double p = ((PI - a) + 0.5 * std::sin(2. * a)) / PI;
    if (p > power_fraction) {
      lo = a;  // p decreases with a, need a larger angle
    } else {
      hi = a;
    }
  }
  return (uint32_t)(0.5 * (lo + hi) / PI * HALF_PERIOD_US + 0.5);
}

}  // namespace my_component
}  // namespace esphome
