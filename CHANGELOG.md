# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/).

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
