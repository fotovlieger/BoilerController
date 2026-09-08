#include "my_component.h"
#include "esphome/core/log.h"

// The control target is derived from the select/number components and published
// to the ISR as a single 32-bit value (control_, scaled x100). Instead of
// polling in loop(), we subscribe to select/number state changes and only
// recompute when they actually change. The set_* wiring calls only store
// pointers: they run once at startup, before the select/number state is
// restored, so they must not read any state.

namespace esphome {
namespace my_component {

static const char *TAG = "my_component";

void MyComponent::update_target() {
  uint32_t target = 0;  // "Off" (and anything unknown) -> 0
  const std::string &opt = mode_->current_option();
  if (opt == "On") {
    target = POWER_FULL;
  } else if (opt == "Auto" || opt == "Manual") {
    double p = power_->state;
    if (p > 100.) p = 100.;
    if (p < 2.) p = 0.;  // less than 2 % gives timing risks
    target = (uint32_t)(p * 100.);
  }

  if (target != control_) {
    // Drop any in-flight delay/pulse first, so a pending alarm can no longer
    // fire a stray pulse after an Off/On transition.
    gptimer_stop(delay_timer_);
    gptimer_stop(pulse_timer_);
    control_ = target;
    ESP_LOGD(TAG, "power target -> %u.%02u %%", (unsigned)(target / 100), (unsigned)(target % 100));
  }
}

void MyComponent::set_power(number::Number *num) {
  this->power_ = num;
}

void MyComponent::set_mode(select::Select *sel) {
  this->mode_ = sel;
}

void MyComponent::set_clock(InternalGPIOPin *pin) {
  this->clock_pin_number_ = pin->get_pin();
  ESP_LOGI(TAG, "Clock pin set to GPIO%d", this->clock_pin_number_);
}

void MyComponent::set_trigger(InternalGPIOPin *pin) {
  this->trigger_pin_number_ = pin->get_pin();
  ESP_LOGI(TAG, "Trigger pin set to GPIO%d", this->trigger_pin_number_);
}

void MyComponent::setup() {
  ESP_LOGI(TAG, "Setting up MyComponent (GPTimer hardware pulse)...");

  // Single owner for pin I/O: the pins are configured directly here with the
  // raw GPIO numbers obtained in set_clock()/set_trigger(). ESPHome's pin
  // objects are only used as the source of those numbers, not configured twice.
  gpio_reset_pin((gpio_num_t)trigger_pin_number_);
  gpio_set_direction((gpio_num_t)trigger_pin_number_, GPIO_MODE_OUTPUT);
  gpio_set_level((gpio_num_t)trigger_pin_number_, 0);  // start 'Off' (inverted by optocoupler)

  // Configure timers
  gptimer_config_t timer_cfg = {
      .clk_src = GPTIMER_CLK_SRC_DEFAULT,
      .direction = GPTIMER_COUNT_UP,
      .resolution_hz = 1000000  // 1 tick = 1 us
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

    // React to external changes instead of polling. The callbacks fire whenever
    // the select or number state changes (incl. when it is restored during
    // startup); update_target() below also covers state restored before these
    // subscriptions were registered.
    mode_->add_on_state_callback([this](size_t) { this->update_target(); });
    power_->add_on_state_callback([this](float) { this->update_target(); });
    this->update_target();
  }

void IRAM_ATTR MyComponent::gpio_edge_isr(void *arg) {
  auto inst = (MyComponent *)arg;
  const uint32_t control = inst->control_;

  if (control == 0) {
    // Off: cancel any pending pulse and hold the output low.
    gptimer_stop(inst->delay_timer_);
    gptimer_stop(inst->pulse_timer_);
    gpio_set_level((gpio_num_t)inst->trigger_pin_number_, 0);  // inverted by optocoupler
  } else if (control == POWER_FULL) {
    // Full power: cancel any pending pulse and hold the output high.
    gptimer_stop(inst->delay_timer_);
    gptimer_stop(inst->pulse_timer_);
    gpio_set_level((gpio_num_t)inst->trigger_pin_number_, 1);  // inverted by optocoupler
  } else {
    // generate a delayed pulse
    gptimer_set_raw_count(inst->delay_timer_, 0);
    gptimer_alarm_config_t alarm_cfg = {
        .alarm_count = POWER_FULL - control,  // ~us of delay (1 tick = 1 us)
        .flags = {.auto_reload_on_alarm = false}};
    gptimer_set_alarm_action(inst->delay_timer_, &alarm_cfg);
    gptimer_start(inst->delay_timer_);
  }
}

bool IRAM_ATTR MyComponent::delay_timer_cb(gptimer_handle_t timer,
                                           const gptimer_alarm_event_data_t *edata,
                                           void *arg) {
  auto inst = (MyComponent *)arg;  // this

  // Target may have changed to Off/Full while the delay was armed; only fire
  // the pulse if we are still in a partial-power state.
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
      .flags = {.auto_reload_on_alarm = false}};
  gptimer_set_alarm_action(inst->pulse_timer_, &alarm_cfg);
  gptimer_start(inst->pulse_timer_);
  return false;
}

bool IRAM_ATTR MyComponent::pulse_timer_cb(gptimer_handle_t timer,
                                           const gptimer_alarm_event_data_t *edata,
                                           void *arg) {
  auto inst = (MyComponent *)arg;  // this
  gpio_set_level((gpio_num_t)inst->trigger_pin_number_, 0);
  gptimer_stop(timer);
  return false;
}

}  // namespace my_component
}  // namespace esphome
