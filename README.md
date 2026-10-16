# esphome-lr500

Local Home Assistant control for the **Beurer LR500 air purifier**, without
the FreshHome cloud. This [ESPHome](https://esphome.io/) component replaces the
stock WiFi module with an ESP8266 and talks to the appliance over UART.

The working setup uses a **Wemos D1 mini and ESPHome 2026.5.1**. ESP32 hardware
has not been verified. Changes on `main` are unreleased; see the
[changelog](CHANGELOG.md) before upgrading.

## Features

| Entity | Controls or readings |
| --- | --- |
| Fan | Appliance power, speeds 1–4, and turbo (5) |
| Sensors | PM2.5 (µg/m³), temperature (°C), humidity (%), filter life remaining (hours) |
| Switches | UV, sleep, auto, buzzer, display units, and optional separate power |
| Diagnostics | Appliance communication status and UART TX mute |
| Button | Filter reset to 4320 hours |

All entities are optional. Readings normally update about once per second.

## Wiring

Disconnect the stock WiFi module from the main board's four-pin **WIFI**
header and connect the D1 mini in its place. This table matches
[`examples/lr500.yaml`](examples/lr500.yaml):

| LR500 header label | D1 mini pin | Purpose |
| --- | --- | --- |
| `VCC` | `5V` | Power through the D1 mini's onboard regulator |
| `GND` | `GND` | Common ground |
| `TX` | `D5` / GPIO14 | ESP transmits commands to the appliance |
| `RX` | `D6` / GPIO12 | ESP receives appliance reports |

The header labels are from the original WiFi module's perspective. The example
uses **D5 as ESP TX and D6 as ESP RX** through a software UART; match the wiring
to your YAML's `tx_pin` and `rx_pin` settings.

Direct UART wiring works on the installed unit, but **the signal voltage has
not been measured**. The header's 5 V supply does not establish the UART signal
voltage. Check signal levels before copying the setup; see the
[electrical notes](docs/PROTOCOL.md#wiring-replacing-the-wifi-module).

## Installation

1. Install the supported ESPHome version, or use an existing installation:

   ```bash
   python3 -m venv .venv
   source .venv/bin/activate
   python -m pip install esphome==2026.5.1
   ```

2. Copy [`examples/lr500.yaml`](examples/lr500.yaml) to your ESPHome config
   directory. Copy [`examples/secrets.yaml.example`](examples/secrets.yaml.example)
   alongside it as `secrets.yaml`. Fill in the WiFi credentials, fallback AP
   password, OTA password, and a new API encryption key:

   ```bash
   openssl rand -base64 32
   ```

3. Connect the D1 mini over USB and flash:

   ```bash
   esphome run lr500.yaml
   ```

4. Add the discovered ESPHome device in Home Assistant and enter the API key
   from `secrets.yaml`. Subsequent firmware updates can use OTA.

The example follows `main`. Pin a compatible tag or commit in
`external_components.source` to keep a deployment on a fixed revision.

## YAML reference

The component portion of a minimal fan and PM2.5 configuration is below.
Use the [full example](examples/lr500.yaml) for board, WiFi, API, OTA, and
additional entities. UART must be **9600 baud, 8N1**, with both TX and RX pins.

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

fan:
  - platform: lr500
    lr500_id: lr500_dev
    name: "LR500 Fan"

sensor:
  - platform: lr500
    lr500_id: lr500_dev
    pm25: { name: "LR500 PM2.5" }
```

See the [configuration reference](docs/CONFIGURATION.md) for all entity options,
communication timeouts, and switch restore behavior.

## Behavior and limits

- **Auto mode:** turning auto off selects manual speed 2. Speeds 2–5 exit auto
  directly. To request speed 1 from auto, the component sends speed 2 then
  speed 1; that consecutive-write timing still needs hardware verification.
- **Lost communication:** by default, after 10 seconds without a valid report,
  the communication diagnostic turns off and sensor readings become unavailable.
  Switches and the fan retain their previous state.
- **UART TX mute:** stops heartbeats and commands, while incoming reports can
  still update entities. Communication timeout monitoring pauses while muted.

If readings never arrive, check the pin mapping, common ground, UART settings,
and TX mute. More detail is in the
[configuration notes](docs/CONFIGURATION.md#behavior-and-troubleshooting) and
[protocol reference](docs/PROTOCOL.md).

## Upgrading

The original `v0.1.0` configuration created entities from the bare `lr500:`
block. Current versions require explicit `sensor:`, `switch:`, `fan:`,
`binary_sensor:`, or `button:` platform entries. Use the full example when
migrating; the current YAML is not compatible with the old tag.

## Development

See [CONTRIBUTING.md](CONTRIBUTING.md) for local builds and CI, and
[RELEASING.md](docs/RELEASING.md) for release checks. Hardware reports are welcome,
especially for auto-to-speed-1 timing and ESP32 boards.

## Credits and license

AI tools were used to assist with development, code review, and documentation.
The protocol was reverse-engineered using the FreshHome Android APK and UART
captures. [LibreTiny](https://github.com/libretiny-eu/libretiny) documentation
helped identify the original WiFi hardware.

[MIT license](LICENSE).
