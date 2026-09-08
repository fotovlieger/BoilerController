#pragma once

#include "esphome/core/component.h"
#include "esphome/components/number/number.h"
#include "esphome/components/select/select.h"
#include "esphome/core/hal.h"

extern "C"
{
#include "driver/gptimer.h"
#include "driver/gpio.h"
}

namespace esphome
{
  namespace my_component
  {

    class MyComponent : public Component
    {
    public:

      enum PowerMode {
        POWER_OFF,
        POWER_ON,
        POWER_AUTO,
        POWER_MANUAL
      };
      number::Number *power_{nullptr};
      select::Select *mode_{nullptr};
      GPIOPin *clock_{nullptr};
      GPIOPin *trigger_{nullptr};
      
      int clock_pin_number_{-1};
      int trigger_pin_number_{-1};

      
      volatile uint64_t delay_us_{2000};
      volatile uint64_t pulse_us_{200};
      volatile PowerMode power_mode_;
      volatile double power_setpoint_;

      // GPTimer handles
      gptimer_handle_t delay_timer_{nullptr};
      gptimer_handle_t pulse_timer_{nullptr};

      // handler 
      void set_power(number::Number *num);
      void set_mode(select::Select *sel);
      void set_clock(GPIOPin *pin, int raw_pin);
      void set_trigger(GPIOPin *pin, int raw_pin);

      void setup() override;
      void loop() override;

      // Interrupy handler
      static bool IRAM_ATTR delay_timer_cb(gptimer_handle_t timer,
                                           const gptimer_alarm_event_data_t *edata,
                                           void *user_data);
      static bool IRAM_ATTR pulse_timer_cb(gptimer_handle_t timer,
                                           const gptimer_alarm_event_data_t *edata,
                                           void *user_data);
      static void IRAM_ATTR gpio_edge_isr(void *arg);
    };

  } // namespace my_component
} // namespace esphome
