# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added
- Optional filter-life sensor, buzzer and Fahrenheit switches, and filter-reset
  button using the confirmed `0x07` command to reset the counter to 4320 hours.
- Optional PIC communication binary sensor and configurable `communication_timeout`
  (default 10 seconds). Missing reports raise a warning and invalidate sensor
  readings; monitoring pauses while TX is muted.
- UART schema validation for 9600 8N1 with both TX and RX pins.

### Changed
- Entities use explicit sensor, switch, fan, button, and binary-sensor platforms;
  the bare `lr500` parent no longer creates entities. See the README migration notes.
- Appliance-backed switches accept only `restore_mode: DISABLED`; state is read
  from the PIC. The local TX mute switch supports standard restore modes.
- Document ESPHome 2026.5.1 as the supported baseline.

### Fixed
- Optional platforms no longer cause references to missing entity headers.
- UART recovery processes buffered candidates without discarding trailing frames,
  resynchronizes after checksum/end-marker failures, and expires partial frames
  after a 100 ms idle interval. Truncated state fields are rejected before publication.
- TX mute initializes and publishes before any UART write, including initial OFF.
- Manual speed 1 requests use speed 2 as an auto-mode escape before writing speed 1
  when the auto state is active or unknown. PIC timing still needs hardware verification.
- README, example, and protocol descriptions agree on confirmed filter-life and reset behavior.

## [0.1.0] - 2026-05-20

Initial release.

### Added
- ESPHome external component `lr500` that speaks the Beurer LR500's internal
  UART protocol (replacing the stock Realtek RTL8710BX WiFi module).
- Heartbeat + framed write helpers, full state-report parser.
- Auto-registered entities:
  - `sensor.lr500_pm25` (µg/m³)
  - `sensor.lr500_temperature` (°C)
  - `sensor.lr500_humidity` (%)
  - `switch.lr500_power`
  - `switch.lr500_uv`
  - `switch.lr500_sleep`
  - `switch.lr500_auto`
  - `fan.lr500` (5 speeds; off ⇒ power off)
- Example YAML for a Wemos D1 mini with software-serial on GPIO12/GPIO14.
- Full UART protocol reference in `docs/PROTOCOL.md`.
