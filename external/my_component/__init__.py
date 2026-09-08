import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import number, select
from esphome import pins

# ✅ Declare inside esphome namespace first, then 'my_component'
my_component_ns = cg.esphome_ns.namespace('my_component')
MyComponent = my_component_ns.class_('MyComponent', cg.Component)

CONFIG_SCHEMA = cv.Schema({
    cv.Required('id'): cv.declare_id(MyComponent),
    cv.Required('power'): cv.use_id(number.Number),
    cv.Required('mode'): cv.use_id(select.Select),
    cv.Required('clock'): pins.gpio_input_pin_schema,
    cv.Required('trigger'): pins.gpio_output_pin_schema,
})

async def to_code(config):
    var = cg.new_Pvariable(config['id'])
    await cg.register_component(var, config)

    # Get actual C++ pointers from YAML IDs
    power_num = await cg.get_variable(config['power'])
    mode_sel = await cg.get_variable(config['mode'])

    # ✅ These will generate var->set_power(...) and var->set_mode(...)
    cg.add(var.set_power(power_num))
    cg.add(var.set_mode(mode_sel))

    # Configure pins correctly
    clock_pin = await cg.gpio_pin_expression(config['clock'])
    trigger_pin = await cg.gpio_pin_expression(config['trigger'])

    cg.add(var.set_clock(clock_pin, config['clock']['number']))
    cg.add(var.set_trigger(trigger_pin, config['trigger']['number']))
