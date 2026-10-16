# esphome-lr500

Local Home Assistant control for the **Beurer LR500 air purifier**, without
the FreshHome cloud.

This is an [ESPHome](https://esphome.io/) external component that replaces
the device's stock Realtek RTL8710BX WiFi module with an ESP8266 (or ESP32)
running firmware *you* control. The ESP speaks the LR500's internal UART
protocol directly to its appliance MCU (a PIC16F1936) and exposes its sensors
and controls as native Home Assistant entities.

The supported baseline is **ESPHome 2026.5.1 on an ESP8266 D1 mini**.
ESP32 support has not been verified. The configuration on `main` includes
unreleased changes; see [upgrading](#upgrading-from-a-pre-platform-plugin-config)
and the [changelog](CHANGELOG.md) before updating an existing installation.

## Why?

During the original investigation, new FreshHome pairings failed with
`401 Basic realm="freshhome-cms"` on `/api/users/list`, including through the
official app. This project grew out of that failure and keeps control local,
independent of the app and backend's availability.

The good news: the LR500's internals are wide open once you remove the
WiFi module. The appliance MCU listens on a plain UART with no
authentication, exposing read access to every sensor and write access to
every control. This project documents that protocol and ships a tidy
ESPHome integration around it.

See [`docs/PROTOCOL.md`](docs/PROTOCOL.md) for the full protocol writeup.

## What you get in Home Assistant

Entity IDs are slugified from the `name:` you supply in YAML (the example
below uses the names shown here). Appliance readings update at ~1 Hz from
the device's own state report while UART TX is enabled.

| Entity | Type | Notes |
|---|---|---|
| `sensor.lr500_pm2_5` | sensor | µg/m³, 0.1 precision (the dot in "PM2.5" slugifies to an underscore) |
| `sensor.lr500_temperature` | sensor | °C |
| `sensor.lr500_humidity` | sensor | % |
| `sensor.lr500_filter_life_remaining` | sensor | remaining HEPA filter life in hours; full = 4320 h |
| `switch.lr500_uv` | switch | UV lamp |
| `switch.lr500_sleep` | switch | sleep mode |
| `switch.lr500_auto` | switch | auto mode — ON enters auto, OFF returns to manual at speed 2 |
| `switch.lr500_buzzer` | switch | key tones / confirmation beeps |
| `switch.lr500_fahrenheit` | switch | display unit: ON = °F, OFF = °C; temperature sensor stays in °C |
| `switch.lr500_uart_mute` | switch | diagnostic — silences UART TX so an external probe can share the bus |
| `fan.lr500_fan` | fan | 5 speed levels (1–4 = explicit speeds, 5 = turbo); turning the fan off powers the device down — i.e. acts as the master on/off |
| `binary_sensor.lr500_pic_communication` | binary sensor | diagnostic; ON after a valid state report, OFF after the communication timeout |
| `button.lr500_filter_reset` | button | resets filter life remaining to 4320 h via confirmed field `0x07` |

## Hardware

You need:

- A Beurer LR500 you're willing to open. The WiFi daughterboard is on the
  side of the main PCB; the main appliance MCU on the verified unit is a
  Microchip **PIC16F1936** and the WiFi module is a Realtek **RTL8710BX**.
  Different hardware revisions may exist — if your PCB looks materially
  different, please [open an issue](../../issues) with photos before
  proceeding.
- An **ESP8266 Wemos D1 mini**. ESP32 boards require a board-specific
  configuration and remain unverified.
- 4 short jumper wires.
- A multimeter (just for one signal-level check during install — see below).

### Wiring

The LR500's main board has a 4-pin header labelled **WIFI** that mates with
the daughterboard via a cable. Unplug the cable and wire the ESP into the
header instead. The `TX`/`RX` labels on the header are from the
*original module's* perspective, so the names line up directly with the ESP's
`TX`/`RX` pins:

| ESP8266 (D1 mini) | LR500 "WIFI" header | Function |
|---|---|---|
| `5V` | `VCC` | 5 V supply from the LR500's main board — feed it into the D1 mini's `5V` pin (its onboard regulator drops it to 3.3 V for the ESP). |
| `GND` | `GND` | common ground |
| `D5` (GPIO14, **ESP TX**) | `TX` | we send heartbeats + control commands; ends up at the PIC's RX. |
| `D6` (GPIO12, **ESP RX**) | `RX` | we receive the PIC's `aa 24 a0 …` state reports. |

> **Verify the UART signal level before connecting the data lines.** The
> header's VCC is 5 V, but the stock RTL8710BX is a 3.3 V part, so the
> supply voltage alone does not establish the UART signal voltage. The PIC's
> TX line could be either 3.3 V or 5 V depending on the design.
> Measure the PIC's TX line at idle; a multimeter reading during traffic can
> average below the actual logic-high voltage.
> - **~3.3 V** → wire ESP RX directly.
> - **~5 V** → put a voltage divider on the PIC-TX → ESP-RX line (e.g.
>   10 kΩ + 20 kΩ → 3.3 V) or use a level shifter (BSS138, TXS0108E).
>
> Also verify that the PIC's RX input accepts a 3.3 V logic-high signal;
> use level shifting in that direction if your hardware revision requires it.

> **If entities show `Unknown` and never update**, you most likely have TX/RX
> swapped. The ESP will send heartbeats but never receive a state report.
> Either rewire (`TX → TX`, `RX → RX` matches the silkscreen) or swap
> `tx_pin` and `rx_pin` in your YAML.

## Installation

1. Install the supported ESPHome version in a virtual environment, or use an
   existing installation of that version:
   ```bash
   python3 -m venv .venv
   source .venv/bin/activate
   python -m pip install esphome==2026.5.1
   ```
2. Copy [`examples/lr500.yaml`](examples/lr500.yaml) into your ESPHome
   config directory (the same place you keep other device YAMLs).
3. Copy [`examples/secrets.yaml.example`](examples/secrets.yaml.example) to
   `secrets.yaml` next to it, and fill in WiFi credentials, fallback access-point
   password, OTA password, and a freshly generated API key:
   ```bash
   openssl rand -base64 32        # paste into api_key
   ```
4. The example follows the development branch (`@main`). To keep an installation
   on a particular revision, replace `main` in `external_components.source` with
   a release tag or full commit SHA. Choose a revision that supports the entity
   platforms below; the original `v0.1.0` tag uses the older configuration.
5. Plug your D1 mini into USB, validate the configuration, and flash:
   ```bash
   esphome config lr500.yaml
   esphome run lr500.yaml
   ```
   First build compiles ~5 minutes. Subsequent updates go OTA.
6. When the ESP joins your network, Home Assistant auto-discovers it.
   In **Settings → Devices & Services** an *ESPHome: LR500* card appears —
   click *Configure*, paste in the API key from `secrets.yaml`, and you're
   done.

## YAML reference

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

Any of the `sensor` / `switch` / `fan` / `binary_sensor` / `button` platforms
is optional — omit a platform or sub-key
to skip that entity entirely. Each one accepts the full set of standard
ESPHome options (`icon`, `disabled_by_default`, `filters`, etc.) *except*
`inverted:`, which is blocked on the switches because PIC field semantics
are fixed.

The parent requires both UART pins and 9600 baud, 8 data bits, no parity,
and 1 stop bit; configuration validation rejects other settings.
`communication_timeout` defaults to `10s`; choose a value comfortably above
the one-second heartbeat interval. After that interval without a valid decoded
state report, the component raises a warning, reports PIC communication OFF,
and publishes unavailable (`NaN`) sensor readings. Recovery clears the warning
and restores readings. Switch and fan values retain the last reported state;
use the diagnostic before relying on those values in automations.

TX mute is initialized before the first heartbeat or control write. It defaults
to `ALWAYS_OFF` and supports the standard switch `restore_mode` options;
`DISABLED` uses the current local mute state (OFF on a fresh boot).
To persist mute, choose `RESTORE_DEFAULT_OFF` (or another restore mode) and
configure ESPHome preferences for your board. On ESP8266, persistence across
power loss requires `esp8266.restore_from_flash: true`; preference writes may
be delayed by `preferences.flash_write_interval`. Appliance-backed switches
accept only `restore_mode: DISABLED`, because their state comes from the PIC.

While TX is muted, communication monitoring pauses and the diagnostic retains
its last value: the PIC may legitimately send nothing until a physical or
sensor change. RX still updates entities. Unmuting starts a fresh timeout
window. The mute switch indicates when this diagnostic is paused.

The optional `filter_reset` button sends the confirmed `0x07` command to reset
filter life remaining to 4320 hours; see
[the protocol notes](docs/PROTOCOL.md#behavioural-notes--quirks).

ESPHome 2026.5.1 is the supported baseline for these changes. Compatibility
with other versions has not been established.

Note that `power:` is intentionally omitted above — the `fan.lr500_fan`
entity's own on/off toggle already drives power. Re-add `power: { name: …}`
to the `switch:` block if you want a separate switch entity (e.g. for an
automation that needs a non-fan trigger).

## Upgrading from a pre-platform-plugin config

If your YAML used to be just `lr500: { uart_id: … }` (no top-level
`sensor:` / `switch:` / `fan:` blocks), you'll need to add the platform
entries from the YAML reference above when upgrading — entities are no
longer auto-loaded from the bare `lr500:` block.

## Caveats & known quirks

- **Auto-mode lockout.** When the device is in auto mode, most field writes
  are silently ignored by the PIC. Toggling `switch.lr500_auto` off (or
  setting manual speed 2–5 via the fan entity) sends the documented escape.
  For manual speed 1, the component sends speed 2 followed by speed 1 when
  auto is active or has not yet been reported. These writes are consecutive;
  whether the PIC needs a delay or confirming report between them still needs
  hardware verification. The next PIC report corrects optimistic entity state.
- **Filter fields.** `0x06` is confirmed filter life remaining, exposed as
  `filter_hours`. `0x07` is the confirmed filter-reset command, exposed as
  `filter_reset`; it resets the counter to 4320 hours.
- **"Sleep" while powered off.** The PIC reports sleep mode for a few
  seconds after a power-off, then resets it. Don't be alarmed by the flicker.
- **Serial logging is disabled.** The example sets `logger: baud_rate: 0`;
  ESPHome logs are available over WiFi. If the ESP is unreachable, recovery
  may require a USB reflash.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for local validation and
[the release checklist](docs/RELEASING.md) for release preparation.
Issues and PRs welcome — especially:

- Verifying auto → manual speed 1 with consecutive speed 2 / speed 1 writes.
- Probing the remaining unknown fields (`0x20`, `0x22`); `0x0E` is buzzer enable.
- ESP32 / ESP32-C3 hardware reports.

## Related files in this repo

- `components/lr500/` — the ESPHome external component (Python + C++)
- `examples/lr500.yaml` — device config with optional entities
- `examples/secrets.yaml.example` — template for the secrets file
- `docs/PROTOCOL.md` — full reverse-engineered protocol writeup
- `CHANGELOG.md` — release notes
- `CONTRIBUTING.md` — development setup and validation
- `docs/RELEASING.md` — release checklist and hardware verification gaps

## License

[MIT](LICENSE).

## Credits

AI tools were used to assist with development, code review, and documentation.

Protocol reverse-engineering done from scratch in May 2026, working from a
decompiled FreshHome APK, UART captures, and a lot of opcode fuzzing.
Thanks to the [LibreTiny](https://github.com/libretiny-eu/libretiny)
project for documentation on the Realtek AmebaZ family that informed the
hardware approach, even though the final solution doesn't end up reflashing
the original module.
