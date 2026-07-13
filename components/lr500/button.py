"""Button platform — stateless one-shot writes to the LR500.

Each button, when pressed, writes a fixed value to a single field. Unlike a
switch there is no state to hold or read back; these map to fields that
trigger an action on the PIC rather than store a value.
"""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import button

from . import CONF_LR500_ID, LR500, lr500_ns

DEPENDENCIES = ["lr500"]

LR500Button = lr500_ns.class_("LR500Button", button.Button)

# yaml-key -> (field-id byte, value to write on press)
#
# filter_reset: field 0x07 is the prime `filterReset` suspect (see
# docs/PROTOCOL.md quirk #7). It's still unconfirmed — a press beeps and, if
# the hypothesis holds, resets the 0x06 filter-life counter to 4320. Any value
# works; we send 1.
TYPES = {
    "filter_reset": (0x07, 0x01),
}

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(CONF_LR500_ID): cv.use_id(LR500),
    **{cv.Optional(key): button.button_schema(LR500Button) for key in TYPES},
})


async def to_code(config):
    parent = await cg.get_variable(config[CONF_LR500_ID])
    for key, (fid, value) in TYPES.items():
        if key not in config:
            continue
        await button.new_button(config[key], parent, fid, value)
