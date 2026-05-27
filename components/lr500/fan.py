"""Fan platform — exposes the 5-speed fan."""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import fan

from . import CONF_LR500_ID, LR500, lr500_ns

DEPENDENCIES = ["lr500"]

LR500Fan = lr500_ns.class_("LR500Fan", fan.Fan)

CONFIG_SCHEMA = fan.fan_schema(LR500Fan).extend({
    cv.GenerateID(CONF_LR500_ID): cv.use_id(LR500),
})


async def to_code(config):
    parent = await cg.get_variable(config[CONF_LR500_ID])
    var = await fan.new_fan(config, parent)
    cg.add(parent.set_fan(var))
