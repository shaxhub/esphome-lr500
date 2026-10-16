# LR500 configuration reference

For installation and wiring, start with the [README](../README.md). The
[complete device example](../examples/lr500.yaml) includes the board, networking,
and firmware-update settings. ESPHome 2026.5.1 is the supported baseline.

## Entity platforms

All entity platforms and their sub-keys are optional. The parent `lr500:`
component alone creates no Home Assistant entities. UART requires TX and RX
pins, 9600 baud, 8 data bits, no parity, and 1 stop bit.

```yaml
external_components:
  - source: github://shaxhub/esphome-lr500@main
    components: [lr500]

uart:
  id: lr500_uart
  tx_pin: GPIO14
  rx_pin: GPIO12
  baud_rate: 9600
  rx_buffer_size: 256

lr500:
  id: lr500_dev
  uart_id: lr500_uart
  communication_timeout: 10s

sensor:
  - platform: lr500
    lr500_id: lr500_dev
    pm25:        { name: "LR500 PM2.5" }
    temperature: { name: "LR500 Temperature" }
    humidity:    { name: "LR500 Humidity" }
    filter_hours: { name: "LR500 Filter life remaining" }

switch:
  - platform: lr500
    lr500_id: lr500_dev
    uv:        { name: "LR500 UV" }
    sleep:     { name: "LR500 Sleep" }
    auto_mode: { name: "LR500 Auto" }
    buzzer:    { name: "LR500 Buzzer" }
    fahrenheit: { name: "LR500 Fahrenheit" }
    tx_mute:   { name: "LR500 UART mute", entity_category: diagnostic }

fan:
  - platform: lr500
    lr500_id: lr500_dev
    name: "LR500 Fan"

binary_sensor:
  - platform: lr500
    lr500_id: lr500_dev
    name: "LR500 PIC communication"

button:
  - platform: lr500
    lr500_id: lr500_dev
    filter_reset: { name: "LR500 Filter reset", icon: "mdi:air-filter" }
```

Add `power: { name: "LR500 Power" }` inside the LR500 switch entry if you want
an independent power switch. The fan's on/off control already drives appliance
power. Fan speeds 1–4 select the corresponding levels; speed 5 selects turbo.

The Fahrenheit switch changes the appliance display units; the temperature
sensor continues to report °C. Filter life is the remaining hours, with a
full counter of 4320. The filter-reset button resets that counter through the
hardware-confirmed `0x07` command.

Entity names determine the initial Home Assistant entity IDs. Standard options
are available for each platform; for example, sensor filters and entity icons.
Switch inversion is rejected because the PIC's field semantics are fixed.

## Communication timeout

`communication_timeout` defaults to `10s`. Choose a value comfortably above
the one-second heartbeat interval. After that time without a valid decoded
state report, the component:

- Raises a component warning.
- Reports PIC communication OFF, if the diagnostic entity is configured.
- Publishes unavailable (`NaN`) readings for configured sensors.

Switch and fan entities keep their previous values, so use the communication
diagnostic when freshness matters in automations. A valid report clears the
warning and restores readings.

## TX mute and restore modes

`tx_mute` is a local diagnostic switch. When ON, it stops heartbeats and drops
outgoing controls; RX parsing continues. Communication monitoring pauses and
the PIC diagnostic retains its last value, since a silent PIC is expected
without heartbeats. Unmuting starts a fresh timeout window.

The mute state is initialized before the first heartbeat or control write.
Its default restore mode is `ALWAYS_OFF`; standard switch restore modes are
supported. `DISABLED` uses the current local state, which is OFF on a fresh boot.

For persistent mute, select a mode such as `RESTORE_DEFAULT_OFF` and configure
ESPHome preferences. On ESP8266, persistence across power loss requires
`esp8266.restore_from_flash: true`. Preference writes may be delayed by
`preferences.flash_write_interval`. Restoration across real power cycles still
needs hardware verification.

Appliance-backed switches accept only `restore_mode: DISABLED`; their state
comes from the PIC.

## Behavior and troubleshooting

- **Auto mode:** the PIC can ignore controls while auto is active. Turning
  the auto switch OFF selects manual speed 2; writing `0` to the auto field
  would enter auto again. Manual speed 1 uses a speed 2 / speed 1 sequence
  when auto is active or unknown. The PIC's timing requirements for that
  sequence remain unverified. The next report corrects optimistic entity state.
- **Sleep after power-off:** the PIC can briefly report sleep mode after
  powering down, then clear it.
- **No readings:** check the YAML's GPIOs against the physical wiring, common
  ground, 9600 8N1 UART settings, and TX mute. The header's TX/RX labels are
  from the original WiFi module's perspective.
- **Logs:** the example disables serial logging with `logger.baud_rate: 0`.
  Read ESPHome logs over WiFi; loss of network access may require USB recovery.

The [protocol reference](PROTOCOL.md) documents the UART fields and observed
appliance behavior. The [release checklist](RELEASING.md) tracks remaining
hardware checks.
