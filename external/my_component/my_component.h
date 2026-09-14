#pragma once

#include "esphome/components/number/number.h"
#include "esphome/components/select/select.h"
#include "esphome/core/component.h"
#include "esphome/core/gpio.h"

#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "power_law.h"

namespace esphome {
namespace my_component {

class MyComponent : public Component {
 public:
  void set_power(number::Number *num);
  void set_mode(select::Select *sel);
  void set_clock(InternalGPIOPin *pin);
  void set_trigger(InternalGPIOPin *pin);

  void setup() override;

 private:
  void update_target();

  // Not IRAM_ATTR: the GPTimer/GPIO ISR service is allocated without
  // ESP_INTR_FLAG_IRAM and these handlers call flash-resident gptimer APIs.
  static bool delay_timer_cb(gptimer_handle_t timer,
                             const gptimer_alarm_event_data_t *edata,
                             void *arg);
  static bool pulse_timer_cb(gptimer_handle_t timer,
                             const gptimer_alarm_event_data_t *edata,
                             void *arg);
  static void gpio_edge_isr(void *arg);

  number::Number *power_{nullptr};
  select::Select *mode_{nullptr};

  // Raw GPIO numbers, taken once from the pin objects passed to the setters.
  int clock_pin_number_{-1};
  int trigger_pin_number_{-1};

  gptimer_handle_t delay_timer_{nullptr};
  gptimer_handle_t pulse_timer_{nullptr};

  // Encoded firing delay: POWER_FULL - delay_us (0 = Off, POWER_FULL = full
  // on). update_target() stores the delay that delivers the requested power;
  // a single 32-bit value so the ISRs can read it without tearing.
  volatile uint32_t control_{0};

  static constexpr uint32_t POWER_FULL = HALF_PERIOD_US;
  static constexpr uint32_t POWER_PULSE_US = 200u;
};

}  // namespace my_component
}  // namespace esphome
