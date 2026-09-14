#include "my_component.h"

#include <cmath>

#include "esphome/core/log.h"

// The control target is derived from the select/number components in
// update_target() and handed to the ISR as a single 32-bit value (control_,
// encoded as POWER_FULL - firing delay in us). update_target() runs on state
// changes only; the setters just store pointers and run at startup, before any
// state is restored.

namespace esphome {
namespace my_component {

static const char *TAG = "my_component";

void MyComponent::set_power(number::Number *num) {
  power_ = num;
}

void MyComponent::set_mode(select::Select *sel) {
  mode_ = sel;
}

void MyComponent::set_clock(InternalGPIOPin *pin) {
  clock_pin_number_ = pin->get_pin();
}

void MyComponent::set_trigger(InternalGPIOPin *pin) {
  trigger_pin_number_ = pin->get_pin();
}

void MyComponent::update_target() {
  uint32_t target = 0;   // "Off" and anything unknown -> 0
  uint32_t delay = POWER_FULL;
  double percent = 0.;
  const std::string &opt = mode_->current_option();
  if (opt == "On") {
    target = POWER_FULL;
    percent = 100.;
    delay = 0;
  } else if (opt == "Auto" || opt == "Manual") {
    percent = power_->state;
    if (!std::isfinite(percent)) percent = 0.;  // state is NAN until first value/restore
    if (percent > 100.) percent = 100.;
    if (percent < 2.) percent = 0.;  // below 2 % is unreliable to time
    // Convert requested power into the firing delay that actually delivers it.
    delay = power_to_delay_us(percent / 100.);
    target = POWER_FULL - delay;
  }

  if (target == control_) {
    return;
  }

  // Drop any in-flight delay/pulse first, so a pending alarm can no longer
  // fire a stray pulse after an Off/On transition, and park the gate at the
  // level for the new target (high only for full power).
  gptimer_stop(delay_timer_);
  gptimer_stop(pulse_timer_);
  gpio_set_level((gpio_num_t)trigger_pin_number_, target == POWER_FULL ? 1 : 0);
  control_ = target;
  ESP_LOGD(TAG, "power %.1f %% -> delay %u us", percent, (unsigned)delay);
}

void MyComponent::setup() {
  ESP_LOGI(TAG, "boiler controller: clock=GPIO%d trigger=GPIO%d",
           clock_pin_number_, trigger_pin_number_);

  // The pins are configured directly with the raw numbers obtained in the
  // setters; the ESPHome pin objects are only the source of those numbers.
  gpio_reset_pin((gpio_num_t)trigger_pin_number_);
  gpio_set_direction((gpio_num_t)trigger_pin_number_, GPIO_MODE_OUTPUT);
  gpio_set_level((gpio_num_t)trigger_pin_number_, 0);  // start off

  // One timer tick is one microsecond.
  gptimer_config_t timer_cfg = {
      .clk_src = GPTIMER_CLK_SRC_DEFAULT,
      .direction = GPTIMER_COUNT_UP,
      .resolution_hz = 1000000,
  };

  ESP_ERROR_CHECK(gptimer_new_timer(&timer_cfg, &delay_timer_));
  gptimer_event_callbacks_t delay_cbs = {.on_alarm = delay_timer_cb};
  ESP_ERROR_CHECK(gptimer_register_event_callbacks(delay_timer_, &delay_cbs, this));
  ESP_ERROR_CHECK(gptimer_enable(delay_timer_));

  ESP_ERROR_CHECK(gptimer_new_timer(&timer_cfg, &pulse_timer_));
  gptimer_event_callbacks_t pulse_cbs = {.on_alarm = pulse_timer_cb};
  ESP_ERROR_CHECK(gptimer_register_event_callbacks(pulse_timer_, &pulse_cbs, this));
  ESP_ERROR_CHECK(gptimer_enable(pulse_timer_));

  // Zero-cross input with a pull-up and edge interrupts.
  gpio_config_t io_conf{};
  io_conf.intr_type = GPIO_INTR_ANYEDGE;
  io_conf.mode = GPIO_MODE_INPUT;
  io_conf.pin_bit_mask = (1ULL << clock_pin_number_);
  io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
  gpio_config(&io_conf);

  gpio_install_isr_service(0);
  gpio_isr_handler_add((gpio_num_t)clock_pin_number_, gpio_edge_isr, this);

  // Recompute whenever the mode or power changes; the call below also covers
  // state that was restored before these subscriptions were registered.
  mode_->add_on_state_callback([this](size_t) { this->update_target(); });
  power_->add_on_state_callback([this](float) { this->update_target(); });
  this->update_target();
}

void MyComponent::gpio_edge_isr(void *arg) {
  auto inst = (MyComponent *)arg;
  const uint32_t control = inst->control_;

  if (control == 0) {
    // Off: cancel any pending pulse and hold the output low.
    gptimer_stop(inst->delay_timer_);
    gptimer_stop(inst->pulse_timer_);
    gpio_set_level((gpio_num_t)inst->trigger_pin_number_, 0);
  } else if (control == POWER_FULL) {
    // Full power: cancel any pending pulse and hold the output high.
    gptimer_stop(inst->delay_timer_);
    gptimer_stop(inst->pulse_timer_);
    gpio_set_level((gpio_num_t)inst->trigger_pin_number_, 1);
  } else {
    // Generate a pulse after a delay proportional to the power.
    gptimer_set_raw_count(inst->delay_timer_, 0);
    gptimer_alarm_config_t alarm_cfg = {
        .alarm_count = POWER_FULL - control,
        .flags = {.auto_reload_on_alarm = false},
    };
    gptimer_set_alarm_action(inst->delay_timer_, &alarm_cfg);
    gptimer_start(inst->delay_timer_);
  }
}

bool MyComponent::delay_timer_cb(gptimer_handle_t timer,
                                 const gptimer_alarm_event_data_t *edata,
                                 void *arg) {
  auto inst = (MyComponent *)arg;

  // The target may have changed while the delay was armed; only fire the pulse
  // if we are still in a partial-power state.
  const uint32_t control = inst->control_;
  if (control == 0) {
    return false;
  }
  if (control == POWER_FULL) {
    gpio_set_level((gpio_num_t)inst->trigger_pin_number_, 1);
    return false;
  }

  gpio_set_level((gpio_num_t)inst->trigger_pin_number_, 1);
  gptimer_stop(timer);
  gptimer_set_raw_count(inst->pulse_timer_, 0);

  gptimer_alarm_config_t alarm_cfg = {
      .alarm_count = POWER_PULSE_US,
      .flags = {.auto_reload_on_alarm = false},
  };
  gptimer_set_alarm_action(inst->pulse_timer_, &alarm_cfg);
  gptimer_start(inst->pulse_timer_);
  return false;
}

bool MyComponent::pulse_timer_cb(gptimer_handle_t timer,
                                 const gptimer_alarm_event_data_t *edata,
                                 void *arg) {
  auto inst = (MyComponent *)arg;
  gpio_set_level((gpio_num_t)inst->trigger_pin_number_, 0);
  gptimer_stop(timer);
  return false;
}

}  // namespace my_component
}  // namespace esphome
