"""ESPHome external component for the Beurer LR500 air purifier.

Declares the parent `lr500` component, which speaks the device's internal
UART protocol (9600 8N1, framed 0xAA … 0xEE). Concrete entities (sensors,
switches, fan, buttons, and communication diagnostic) are wired in by the
platform plugins in this same folder.
"""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart
from esphome.const import CONF_ID

CODEOWNERS = ["@shaxhub"]
DEPENDENCIES = ["uart"]

lr500_ns = cg.esphome_ns.namespace("lr500")
LR500 = lr500_ns.class_("LR500", cg.Component, uart.UARTDevice)

CONF_LR500_ID = "lr500_id"
CONF_COMMUNICATION_TIMEOUT = "communication_timeout"

CONFIG_SCHEMA = (
    cv.Schema({
        cv.GenerateID(): cv.declare_id(LR500),
        cv.Optional(CONF_COMMUNICATION_TIMEOUT, default="10s"): cv.positive_time_period_milliseconds,
    })
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA)
)

FINAL_VALIDATE_SCHEMA = uart.final_validate_device_schema(
    "lr500", baud_rate=9600, data_bits=8, parity="NONE", stop_bits=1,
    require_tx=True, require_rx=True,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    cg.add(var.set_communication_timeout(config[CONF_COMMUNICATION_TIMEOUT]))
