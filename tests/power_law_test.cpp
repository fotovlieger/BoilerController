// Host test for the phase-control power -> firing-delay inversion.
//
// Build & run:
//   g++ -std=c++17 tests/power_law_test.cpp -o /tmp/power_law_test \
//       && /tmp/power_law_test
//
// Verifies that power_to_delay_us() actually delivers the requested power,
// not merely that it is monotonic.

#include <cassert>
#include <cmath>
#include <cstdio>

#include "../external/my_component/power_law.h"

using esphome::my_component::HALF_PERIOD_US;
using esphome::my_component::power_to_delay_us;

// Forward law: power fraction actually delivered for a given firing delay.
static double power_at_delay(uint32_t delay_us) {
  constexpr double PI = 3.14159265358979323846;
  double a = (double) delay_us / HALF_PERIOD_US * PI;
  return ((PI - a) + 0.5 * std::sin(2. * a)) / PI;
}

int main() {
  // Endpoints and clamping.
  assert(power_to_delay_us(0.0) == HALF_PERIOD_US);
  assert(power_to_delay_us(1.0) == 0);
  assert(power_to_delay_us(-0.5) == HALF_PERIOD_US);
  assert(power_to_delay_us(2.0) == 0);

  // Round-trip: the delay must deliver the requested power (within the 1 us
  // timer resolution).
  for (int pct = 2; pct <= 100; pct++) {
    double want = pct / 100.0;
    double got = power_at_delay(power_to_delay_us(want));
    if (std::fabs(got - want) >= 0.001) {
      std::printf("FAIL pct=%d want=%.4f got=%.4f\n", pct, want, got);
      return 1;
    }
  }

  // More requested power never means a longer delay.
  for (int pct = 2; pct < 100; pct++) {
    assert(power_to_delay_us((pct + 1) / 100.0) <= power_to_delay_us(pct / 100.0));
  }

  // Anchors: 50 % stays at the half-cycle midpoint; 2 % is the practical floor.
  assert(power_to_delay_us(0.50) == 5000);
  assert(power_to_delay_us(0.02) == 8531);

  std::printf("power_law: all checks passed\n");
  return 0;
}
