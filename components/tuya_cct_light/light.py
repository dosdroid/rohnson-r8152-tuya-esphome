import esphome.codegen as cg
from esphome.components import light
import esphome.config_validation as cv
from esphome.const import (
    CONF_COLD_WHITE_COLOR_TEMPERATURE,
    CONF_DEFAULT_TRANSITION_LENGTH,
    CONF_GAMMA_CORRECT,
    CONF_MAX_VALUE,
    CONF_MIN_VALUE,
    CONF_OUTPUT_ID,
    CONF_SWITCH_DATAPOINT,
    CONF_WARM_WHITE_COLOR_TEMPERATURE,
)

from esphome.components.tuya import CONF_TUYA_ID, Tuya, tuya_ns

DEPENDENCIES = ["tuya"]

CONF_DIMMER_DATAPOINT = "dimmer_datapoint"
CONF_CCT_DATAPOINT = "cct_datapoint"

tuya_cct_light_ns = cg.esphome_ns.namespace("tuya_cct_light")
TuyaCctLight = tuya_cct_light_ns.class_("TuyaCctLight", light.LightOutput, cg.Component)

CONFIG_SCHEMA = light.LIGHT_SCHEMA.extend(
    {
        cv.GenerateID(CONF_OUTPUT_ID): cv.declare_id(TuyaCctLight),
        cv.GenerateID(CONF_TUYA_ID): cv.use_id(Tuya),
        cv.Required(CONF_SWITCH_DATAPOINT): cv.uint8_t,
        cv.Required(CONF_DIMMER_DATAPOINT): cv.uint8_t,
        cv.Required(CONF_CCT_DATAPOINT): cv.uint8_t,
        cv.Optional(CONF_MIN_VALUE, default=10): cv.int_,
        cv.Optional(CONF_MAX_VALUE, default=100): cv.int_,
        cv.Optional(
            CONF_COLD_WHITE_COLOR_TEMPERATURE, default="153 mireds"
        ): cv.color_temperature,
        cv.Optional(
            CONF_WARM_WHITE_COLOR_TEMPERATURE, default="370 mireds"
        ): cv.color_temperature,
        # The Tuya MCU handles transitions and gamma correction on its own, and
        # client-side transitions would spam the slow 9600-baud UART link with
        # many intermediate SET frames.
        cv.Optional(CONF_GAMMA_CORRECT, default=1.0): cv.positive_float,
        cv.Optional(
            CONF_DEFAULT_TRANSITION_LENGTH, default="0s"
        ): cv.positive_time_period_milliseconds,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_OUTPUT_ID])
    await cg.register_component(var, config)
    await light.register_light(var, config)

    cg.add(var.set_switch_id(config[CONF_SWITCH_DATAPOINT]))
    cg.add(var.set_dimmer_id(config[CONF_DIMMER_DATAPOINT]))
    cg.add(var.set_cct_id(config[CONF_CCT_DATAPOINT]))
    cg.add(var.set_min_value(config[CONF_MIN_VALUE]))
    cg.add(var.set_max_value(config[CONF_MAX_VALUE]))
    cg.add(var.set_cold_white_temperature(config[CONF_COLD_WHITE_COLOR_TEMPERATURE]))
    cg.add(var.set_warm_white_temperature(config[CONF_WARM_WHITE_COLOR_TEMPERATURE]))

    parent = await cg.get_variable(config[CONF_TUYA_ID])
    cg.add(var.set_tuya_parent(parent))
