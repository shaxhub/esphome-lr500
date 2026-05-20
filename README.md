# esphome-lr500

Local Home Assistant control for the **Beurer LR500 air purifier**, bypassing
its broken cloud entirely.

This is an [ESPHome](https://esphome.io/) external component that replaces
the device's stock Realtek RTL8710BX WiFi module with an ESP8266 (or ESP32)
running firmware *you* control. The ESP speaks the LR500's internal UART
protocol directly to its appliance MCU (a PIC16F1936) and exposes everything
as native Home Assistant entities.

## Why?

Beurer's FreshHome backend is, as of mid-2026, **broken for new pairings**.
The mobile app gets a `401 Basic realm="freshhome-cms"` on
`/api/users/list` for every normal user account — the user-facing API route
is misconfigured to require a CMS/admin role that ordinary tokens don't have.
Both the official app and any reimplementation hit the same wall, so no
amount of clever auth or BLE provisioning will make the cloud actually work.

The good news: the LR500's internals are wide open once you remove the
WiFi module. The appliance MCU listens on a plain UART with no
authentication, exposing read access to every sensor and write access to
every control. This project documents that protocol and ships a tidy
ESPHome integration around it.

See [`docs/PROTOCOL.md`](docs/PROTOCOL.md) for the full protocol writeup.

## What you get in Home Assistant

| Entity | Type | Notes |
|---|---|---|
| `sensor.lr500_pm25` | sensor | µg/m³, 0.1 precision |
| `sensor.lr500_temperature` | sensor | °C |
| `sensor.lr500_humidity` | sensor | % |
| `switch.lr500_power` | switch | full power on/off |
| `switch.lr500_uv` | switch | UV lamp |
| `switch.lr500_sleep` | switch | sleep mode |
| `switch.lr500_auto` | switch | auto mode (writing toggles auto on/off) |
| `fan.lr500` | fan | 5 speed levels (1–4 = explicit speeds, 5 = turbo); turning the fan off powers the device down |

All update at ~1 Hz from the device's own state report.

## Hardware

You need:

- A Beurer LR500 you're willing to open. The WiFi daughterboard is on the
  side of the main PCB; the main appliance MCU on the verified unit is a
  Microchip **PIC16F1936** and the WiFi module is a Realtek **RTL8710BX**.
  Different hardware revisions may exist — if your PCB looks materially
  different, please [open an issue](../../issues) with photos before
  proceeding.
- An **ESP8266** dev board (Wemos D1 mini is cheapest, ~$3). An ESP32 *should*
  work with the same code (swap the `esp8266:` block in YAML for `esp32:`),
  but it's currently untested.
- 4 short jumper wires.
- A multimeter (just for one signal-level check during install — see below).

### Wiring

The LR500's main board has a 4-pin header labelled **WIFI** that mates with
the daughterboard. Unplug the daughterboard and wire the ESP into the
header instead:

| ESP8266 (D1 mini) | LR500 "WIFI" header | Notes |
|---|---|---|
| `5V` | `VCC` | the header supplies **5 V**, not 3.3 V — feed it to the D1 mini's `5V` pin (its onboard regulator makes the 3.3 V the ESP needs). |
| `GND` | `GND` | required |
| `D5` (GPIO14, **ESP TX**) | `RX` (PIC's RX line) | the line where the original module's heartbeats appeared |
| `D6` (GPIO12, **ESP RX**) | `TX` (PIC's TX line) | the line where the PIC's `aa 24 a0 …` state frames originate |

> **Verify the UART signal level before connecting the data lines.** The
> header's VCC is 5 V, but the stock RTL8710BX is a 3.3 V part, so the
> daughterboard handles the conversion. The PIC's TX line going into that
> daughterboard could be either 3.3 V or 5 V depending on the design.
> Probe the PIC's TX pad with a multimeter while traffic flows — the idle
> "high" level should read close to the signal voltage.
> - **~3.3 V** → wire ESP RX directly; you're done.
> - **~5 V** → put a voltage divider on the PIC-TX → ESP-RX line (e.g.
>   10 kΩ + 20 kΩ → 3.3 V) or use a level shifter (BSS138, TXS0108E).
>   ESP-TX → PIC-RX is always fine: 3.3 V comfortably exceeds the PIC's
>   ~2 V logic-high threshold.

> **Pin direction is easy to flip by accident.** If, on first boot, the
> entities show stale values (`stale_seconds` climbing), swap `tx_pin` and
> `rx_pin` in the YAML.

## Installation

1. Install ESPHome on your machine if you don't already have it:
   ```bash
   pip install esphome
   ```
2. Copy [`examples/lr500.yaml`](examples/lr500.yaml) into your ESPHome
   config directory (the same place you keep other device YAMLs).
3. Copy [`examples/secrets.yaml.example`](examples/secrets.yaml.example) to
   `secrets.yaml` next to it, and fill in WiFi credentials, OTA password,
   and a freshly generated API key:
   ```bash
   openssl rand -base64 32        # paste into api_key
   ```
4. Plug your D1 mini into USB and flash:
   ```bash
   esphome run lr500.yaml
   ```
   First build compiles ~5 minutes. Subsequent updates go OTA.
5. When the ESP joins your network, Home Assistant auto-discovers it.
   In **Settings → Devices & Services** an *ESPHome: LR500* card appears —
   click *Configure*, paste in the API key from `secrets.yaml`, and you're
   done.

## YAML reference

```yaml
external_components:
  - source: github://shaxhub/esphome-lr500
    components: [lr500]

uart:
  id: lr500_uart
  tx_pin: GPIO14
  rx_pin: GPIO12
  baud_rate: 9600
  rx_buffer_size: 256

lr500:
  uart_id: lr500_uart
```

That's the whole user-facing config. All entities are registered by the
component itself — no per-entity YAML required.

## Caveats & known quirks

- **Auto-mode lockout.** When the device is in auto mode, most field writes
  are silently ignored by the PIC. Toggling `switch.lr500_auto` off (or
  setting an explicit fan speed via `fan.lr500`) leaves auto. This isn't a
  bug in the component — it's the appliance's actual behaviour.
- **Field 0x06.** There's an auxiliary 2-byte sensor we haven't fully
  identified — possibly TVOC, an AQI index, or raw particle count. Not
  currently exposed to HA. Pull requests welcome.
- **"Sleep" while powered off.** The PIC reports sleep mode for a few
  seconds after a power-off, then resets it. Don't be alarmed by the flicker.
- **OTA over the boot UART.** The example YAML sets `logger: baud_rate: 0`
  so the hardware UART isn't competing with our software-serial link.
  All logs are over WiFi from then on; if the ESP is unreachable, recover
  via USB reflash.

## Contributing

Issues and PRs welcome — especially:

- Identifying field 0x06 (point a known PM10/TVOC sensor at the same air
  and see if it correlates).
- Probing the unknown fields (`0x07`, `0x0E`, `0x20`, `0x22`) to see if any
  of them control anything besides the buzzer.
- ESP32 / ESP32-C3 hardware reports.

## Related files in this repo

- `components/lr500/` — the ESPHome external component (Python + C++)
- `examples/lr500.yaml` — minimal working device config
- `examples/secrets.yaml.example` — template for the secrets file
- `docs/PROTOCOL.md` — full reverse-engineered protocol writeup
- `CHANGELOG.md` — release notes

## License

[MIT](LICENSE).

## Credits

Protocol reverse-engineering done from scratch in May 2026, working from a
decompiled FreshHome APK, UART captures, and a lot of opcode fuzzing.
Thanks to the [LibreTiny](https://github.com/libretiny-eu/libretiny)
project for documentation on the Realtek AmebaZ family that informed the
hardware approach, even though the final solution doesn't end up reflashing
the original module.
