"""Diagnostic platform — reports communication with the appliance PIC."""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from esphome.const import DEVICE_CLASS_CONNECTIVITY, ENTITY_CATEGORY_DIAGNOSTIC

from . import CONF_LR500_ID, LR500

DEPENDENCIES = ["lr500"]

CONFIG_SCHEMA = binary_sensor.binary_sensor_schema(
    device_class=DEVICE_CLASS_CONNECTIVITY,
    entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
).extend({cv.GenerateID(CONF_LR500_ID): cv.use_id(LR500)})


async def to_code(config):
    parent = await cg.get_variable(config[CONF_LR500_ID])
    sens = await binary_sensor.new_binary_sensor(config)
    cg.add(parent.set_communication_sensor(sens))
