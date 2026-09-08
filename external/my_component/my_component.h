#pragma once

#include "esphome/components/number/number.h"
#include "esphome/components/select/select.h"
#include "esphome/core/component.h"
#include "esphome/core/gpio.h"

#include "driver/gpio.h"
#include "driver/gptimer.h"

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

  static bool IRAM_ATTR delay_timer_cb(gptimer_handle_t timer,
                                       const gptimer_alarm_event_data_t *edata,
                                       void *arg);
  static bool IRAM_ATTR pulse_timer_cb(gptimer_handle_t timer,
                                       const gptimer_alarm_event_data_t *edata,
                                       void *arg);
  static void IRAM_ATTR gpio_edge_isr(void *arg);

  number::Number *power_{nullptr};
  select::Select *mode_{nullptr};

  // Raw GPIO numbers, taken once from the pin objects passed to the setters.
  int clock_pin_number_{-1};
  int trigger_pin_number_{-1};

  gptimer_handle_t delay_timer_{nullptr};
  gptimer_handle_t pulse_timer_{nullptr};

  // Target power scaled by 100 (0..10000 = 0..100 %). A single 32-bit value so
  // update_target() and the ISRs can share it without tearing.
  volatile uint32_t control_{0};

  static constexpr uint32_t POWER_FULL = 10000u;
  static constexpr uint32_t POWER_PULSE_US = 200u;
};

}  // namespace my_component
}  // namespace esphome
