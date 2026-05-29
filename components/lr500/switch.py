"""Switch platform — one switch per writable boolean field."""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch

from . import CONF_LR500_ID, LR500, lr500_ns

DEPENDENCIES = ["lr500"]

LR500Switch = lr500_ns.class_("LR500Switch", switch.Switch)
LR500MuteSwitch = lr500_ns.class_("LR500MuteSwitch", switch.Switch)

# Each entry: yaml-key -> (C++ setter on LR500, field-id byte)
TYPES = {
    "power":     ("set_power_switch",  0x08),
    "uv":        ("set_uv_switch",     0x0B),
    "sleep":     ("set_sleep_switch",  0x0D),
    "auto_mode": ("set_auto_switch",   0x0A),
    "buzzer":    ("set_buzzer_switch", 0x0E),
}

# Mute is a diagnostic switch that gates the component's TX. Toggling ON stops
# heartbeats and drops any further writes from HA — lets an external probe
# share the UART line without bus contention.
MUTE_KEY = "tx_mute"

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(CONF_LR500_ID): cv.use_id(LR500),
    # `block_inverted=True` refuses `inverted: true` on the YAML side.
    # The PIC's field semantics are physically fixed (POWER=1=on, BUZZER=1=on,
    # …), so an inverted switch would silently send wrong bytes to the device.
    **{cv.Optional(key): switch.switch_schema(LR500Switch, block_inverted=True)
       for key in TYPES},
    # tx_mute is a diagnostic; inverting it would just be confusing UX.
    cv.Optional(MUTE_KEY): switch.switch_schema(LR500MuteSwitch, block_inverted=True),
})


async def to_code(config):
    parent = await cg.get_variable(config[CONF_LR500_ID])
    for key, (setter, fid) in TYPES.items():
        if key not in config:
            continue
        sw = await switch.new_switch(config[key], parent, fid)
        cg.add(getattr(parent, setter)(sw))

    if MUTE_KEY in config:
        sw = await switch.new_switch(config[MUTE_KEY], parent)
        cg.add(parent.set_mute_switch(sw))
