"""Switch platform — one switch per writable boolean field."""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch

from . import CONF_LR500_ID, LR500, lr500_ns

DEPENDENCIES = ["lr500"]

LR500Switch = lr500_ns.class_("LR500Switch", switch.Switch)

# Each entry: yaml-key -> (C++ setter on LR500, field-id byte)
TYPES = {
    "power":     ("set_power_switch", 0x08),
    "uv":        ("set_uv_switch",    0x0B),
    "sleep":     ("set_sleep_switch", 0x0D),
    "auto_mode": ("set_auto_switch",  0x0A),
}

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(CONF_LR500_ID): cv.use_id(LR500),
    **{cv.Optional(key): switch.switch_schema(LR500Switch) for key in TYPES},
})


async def to_code(config):
    parent = await cg.get_variable(config[CONF_LR500_ID])
    for key, (setter, fid) in TYPES.items():
        if key not in config:
            continue
        sw = await switch.new_switch(config[key], parent, fid)
        cg.add(getattr(parent, setter)(sw))
