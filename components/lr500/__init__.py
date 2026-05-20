"""ESPHome external component for the Beurer LR500 air purifier.

Speaks the device's internal UART protocol (9600 8N1, framed 0xAA … 0xEE),
pretends to be the dead RTL8710BX WiFi module, and publishes entities to HA:
  sensor.lr500_pm25, sensor.lr500_temperature, sensor.lr500_humidity,
  switch.lr500_power, switch.lr500_uv, switch.lr500_sleep, switch.lr500_auto,
  fan.lr500
"""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart
from esphome.const import CONF_ID

CODEOWNERS = ["@local"]
DEPENDENCIES = ["uart"]
AUTO_LOAD = ["sensor", "switch", "fan"]

lr500_ns = cg.esphome_ns.namespace("lr500")
LR500 = lr500_ns.class_("LR500", cg.Component, uart.UARTDevice)

CONFIG_SCHEMA = (
    cv.Schema({cv.GenerateID(): cv.declare_id(LR500)})
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
