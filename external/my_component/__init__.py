import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components import number, select

my_component_ns = cg.esphome_ns.namespace("my_component")
MyComponent = my_component_ns.class_("MyComponent", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.Required("id"): cv.declare_id(MyComponent),
        cv.Required("power"): cv.use_id(number.Number),
        cv.Required("mode"): cv.use_id(select.Select),
        cv.Required("clock"): pins.gpio_input_pin_schema,
        cv.Required("trigger"): pins.gpio_output_pin_schema,
    }
)


async def to_code(config):
    var = cg.new_Pvariable(config["id"])
    await cg.register_component(var, config)

    cg.add(var.set_power(await cg.get_variable(config["power"])))
    cg.add(var.set_mode(await cg.get_variable(config["mode"])))
    cg.add(var.set_clock(await cg.gpio_pin_expression(config["clock"])))
    cg.add(var.set_trigger(await cg.gpio_pin_expression(config["trigger"])))
