#include "my_component.h"
#include "esphome/core/log.h"

// NOTE: set+mode, etc are not being called; they used to be :-(
// workaround was to move some stuff to loop()
// TODO: fix!

namespace esphome {
namespace my_component {
  static const char *TAG = "my_component";

  void MyComponent::loop() {

        // Convert to enum
    if (mode_->current_option() == "Off") power_mode_ = POWER_OFF;
    else if (mode_->current_option() == "On") power_mode_ = POWER_ON;
    else if (mode_->current_option() == "Auto") power_mode_ = POWER_AUTO;
    else if (mode_->current_option() == "Manual") power_mode_ = POWER_MANUAL;

    if (power_mode_ == POWER_OFF) {
      power_setpoint_ = 0.;
    }
    else if (power_mode_ == POWER_ON) {
      power_setpoint_ = 100.;
    }
    else {
      power_setpoint_ = power_->state;
      if (power_setpoint_ > 100.) power_setpoint_ = 100.;
      if (power_setpoint_ < 2.) power_setpoint_ = 0.;  // less than 2% gives timing 'riscs'
      delay_us_ = 10000. * (1. - power_setpoint_ / 100.); // much power is short delay, 10000 is 10 ms
    }
    static int cnt=0;
    if (cnt++%100==0) {
       // ESP_LOGI("loop", "mode=%d, setp=%f, delay=%d", power_mode_, power_setpoint_, delay_us_ );
    }
  }

void MyComponent::set_power(number::Number *num) {
    ESP_LOGI(TAG, "set_power() called — setpoint=%.1f", num->state);
    this->power_ = num;
    this->power_setpoint_ = num->state;
}

void MyComponent::set_mode(select::Select *sel) {
    ESP_LOGI(TAG, "set_mode() called — current mode='%s'", sel->current_option().c_str());
    this->mode_ = sel;

    // Convert to enum
    if (sel->current_option() == "Off") this->power_mode_ = POWER_OFF;
    else if (sel->current_option() == "On") this->power_mode_ = POWER_ON;
    else if (sel->current_option() == "Auto") this->power_mode_ = POWER_AUTO;
    else if (sel->current_option() == "Manual") this->power_mode_ = POWER_MANUAL;
}

void MyComponent::set_clock(GPIOPin *pin, int raw_pin) {
    ESP_LOGI(TAG, "Clock pin set to GPIO%d", raw_pin);
    this->clock_ = pin;
    this->clock_pin_number_ = raw_pin;
}

void MyComponent::set_trigger(GPIOPin *pin, int raw_pin) {
    ESP_LOGI(TAG, "Trigger pin set to GPIO%d", raw_pin);
    this->trigger_ = pin;
    this->trigger_pin_number_ = raw_pin;
}


  void MyComponent::setup()
  {
    ESP_LOGI(TAG, "Setting up MyComponent (GPTimer hardware pulse)...");

    clock_->setup();
    trigger_->setup();

    // Configure trigger pin directly for ISR safety
    gpio_reset_pin((gpio_num_t)trigger_pin_number_);
    gpio_set_direction((gpio_num_t)trigger_pin_number_, GPIO_MODE_OUTPUT);

    // Configure timers
    gptimer_config_t timer_cfg = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000 // 1 tick = 1 µs
    };

    ESP_ERROR_CHECK(gptimer_new_timer(&timer_cfg, &delay_timer_));
    gptimer_event_callbacks_t delay_cbs = {.on_alarm = delay_timer_cb};
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(delay_timer_, &delay_cbs, this));
    ESP_ERROR_CHECK(gptimer_enable(delay_timer_));

    ESP_ERROR_CHECK(gptimer_new_timer(&timer_cfg, &pulse_timer_));
    gptimer_event_callbacks_t pulse_cbs = {.on_alarm = pulse_timer_cb};
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(pulse_timer_, &pulse_cbs, this));
    ESP_ERROR_CHECK(gptimer_enable(pulse_timer_));

    // Configure clock pin with edge interrupts
    gpio_config_t io_conf{};
    io_conf.intr_type = GPIO_INTR_ANYEDGE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << clock_pin_number_);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io_conf);

    gpio_install_isr_service(0);
    gpio_isr_handler_add((gpio_num_t)clock_pin_number_, gpio_edge_isr, this);
  }

  void IRAM_ATTR MyComponent::gpio_edge_isr(void *arg)
  {
    auto inst = (MyComponent *)arg;
    gptimer_set_raw_count(inst->delay_timer_, 0);

    if (inst->power_setpoint_ == 0.) {
      gpio_set_level((gpio_num_t)inst->trigger_pin_number_, 0);  // inverted by optocoupler
    }
    else if (inst->power_setpoint_ == 100.) {
      gpio_set_level((gpio_num_t)inst->trigger_pin_number_, 1);  // inverted by optocoupler
    }
    else {
      // generate a delayed pulse
      gptimer_alarm_config_t alarm_cfg = {
          .alarm_count = inst->delay_us_,
          .flags = {.auto_reload_on_alarm = false}};
      gptimer_set_alarm_action(inst->delay_timer_, &alarm_cfg);
      gptimer_start(inst->delay_timer_);
    }
  }

  bool IRAM_ATTR MyComponent::delay_timer_cb(gptimer_handle_t timer,
                                              const gptimer_alarm_event_data_t *edata,
                                              void *arg)
  {
    auto inst = (MyComponent *)arg; // this
    gpio_set_level((gpio_num_t)inst->trigger_pin_number_, 1);
    gptimer_stop(timer);
    gptimer_set_raw_count(inst->pulse_timer_, 0);

    gptimer_alarm_config_t alarm_cfg = {
        .alarm_count = inst->pulse_us_,
        .flags = {.auto_reload_on_alarm = false}};
    gptimer_set_alarm_action(inst->pulse_timer_, &alarm_cfg);
    gptimer_start(inst->pulse_timer_);
    return false;
  }

  bool IRAM_ATTR MyComponent::pulse_timer_cb(gptimer_handle_t timer,
                                              const gptimer_alarm_event_data_t *edata,
                                              void *arg)
  {
    auto inst = (MyComponent *)arg; // this
    gpio_set_level((gpio_num_t)inst->trigger_pin_number_, 0);
    gptimer_stop(timer);
    return false;
  }

} // namespace my_component
} // namespace esphome
