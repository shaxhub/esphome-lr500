"""Sensor platform — exposes the LR500's three sensor fields."""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import (
    DEVICE_CLASS_HUMIDITY,
    DEVICE_CLASS_PM25,
    DEVICE_CLASS_TEMPERATURE,
    STATE_CLASS_MEASUREMENT,
    UNIT_CELSIUS,
    UNIT_PERCENT,
)

from . import CONF_LR500_ID, LR500

DEPENDENCIES = ["lr500"]

UNIT_MICROGRAMS_PER_CUBIC_METER = "µg/m³"

# Each entry: yaml-key -> (C++ setter, sensor_schema kwargs)
TYPES = {
    "pm25": ("set_pm25_sensor", dict(
        unit_of_measurement=UNIT_MICROGRAMS_PER_CUBIC_METER,
        accuracy_decimals=1,
        device_class=DEVICE_CLASS_PM25,
        state_class=STATE_CLASS_MEASUREMENT,
    )),
    "temperature": ("set_temperature_sensor", dict(
        unit_of_measurement=UNIT_CELSIUS,
        accuracy_decimals=1,
        device_class=DEVICE_CLASS_TEMPERATURE,
        state_class=STATE_CLASS_MEASUREMENT,
    )),
    "humidity": ("set_humidity_sensor", dict(
        unit_of_measurement=UNIT_PERCENT,
        accuracy_decimals=0,
        device_class=DEVICE_CLASS_HUMIDITY,
        state_class=STATE_CLASS_MEASUREMENT,
    )),
}

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(CONF_LR500_ID): cv.use_id(LR500),
    **{cv.Optional(key): sensor.sensor_schema(**defaults) for key, (_, defaults) in TYPES.items()},
})


async def to_code(config):
    parent = await cg.get_variable(config[CONF_LR500_ID])
    for key, (setter, _) in TYPES.items():
        if key not in config:
            continue
        sens = await sensor.new_sensor(config[key])
        cg.add(getattr(parent, setter)(sens))
