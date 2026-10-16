# Release checklist

Releases distribute the external component source. Users compile firmware with
their own board configuration and secrets. Keep the changelog's `Unreleased`
section until a release version and date have been selected.

## Prepare the source

- Review `git status --short` and `git diff`; include only intended files.
- Follow [CONTRIBUTING.md](../CONTRIBUTING.md) to validate the local checkout
  with ESPHome 2026.5.1. Generate and fully compile the optional-platform
  configurations, README reference, and full example. Record the results.
- Confirm all eight CI builds pass on the release candidate commit.
- Run `git diff --check` and check relative Markdown links.
- Keep the README, example, protocol reference, and changelog consistent.
- Check that no secrets, UART captures containing private data, generated
  configuration, or firmware binaries are included in the release.

## Recorded local validation — 2026-10-16

Validated the component sources and example configuration in this
release-preparation pass, using ESPHome 2026.5.1 and an ESP8266 D1 mini target:

- Source generation and full firmware builds passed for parent-only, sensor,
  switch, fan, button, binary sensor, README reference, and full example
  configurations. All used the local component source and dummy credentials.
- Invalid baud rate, data bits, parity, stop bits, missing TX/RX pins,
  appliance-switch restore mode, and switch inversion were rejected.
- Component Python modules parsed successfully. The protocol reference's
  Python examples handled valid frames and rejected malformed lengths,
  checksums, end markers, empty reports, and truncated fields.
- Relative documentation links and the documented local-source path checked
  successfully; `git diff --check` passed.

No device was flashed or tested during the local build-validation pass.
These results cover local compilation; published-tag validation and the
appliance checks below remain part of the release process.

## Recorded hardware validation — 2026-10-16

The maintainer repeatedly tested switching from auto to manual speed 1 on the
installed LR500 with a NodeMCU-style ESP8266 board. The current consecutive
speed 2 / speed 1 sequence worked reliably. No added delay was needed.

## Verify on an appliance

Record which checks were performed on the candidate revision:

- Readings update, and physical controls are reflected in Home Assistant.
- Fan power, speeds 1–5, UV, sleep, auto, buzzer, and Fahrenheit controls work.
- Auto can transition to every manual speed. In particular, verify the
  consecutive speed 2 / speed 1 sequence when requesting speed 1 from auto.
- TX mute starts in the configured state before any heartbeat or command,
  suppresses writes, and still permits RX updates. If using persistent mute,
  verify restoration across a real power cycle with flash persistence enabled.
- Missing PIC reports trigger the communication warning and unavailable sensor
  readings; reconnecting restores them. Intentional mute pauses monitoring.
- Filter reset returns the filter-life reading to 4320 hours. Perform this
  only when a filter reset is intended; the command changes the appliance's
  maintenance counter.

Filter reset via `0x07` was previously confirmed on hardware, and auto-to-speed-1
transitions are confirmed above. Persistent mute across real power cycles and
ESP32 hardware remain unverified. Carry any unresolved limits into the release
notes.

## Cut the release

1. Choose the next pre-1.0 version under Semantic Versioning.
2. Move `Unreleased` entries into a section for the chosen version and release
   date, and leave a new `Unreleased` section for future changes.
3. Pin the README and example's GitHub component source to the intended release
   tag. Verify that the tagged source supports the documented YAML.
4. Commit the reviewed release changes, create the matching `vX.Y.Z` tag at
   that commit, and publish the release with the supported ESPHome version,
   validation results, and remaining hardware limits.
5. Verify the published tag's configuration in a fresh directory using the
   GitHub source, then compile it. This checks distribution as well as local
   compilation.

Do not publish firmware built with personal credentials. The source and example
configuration are the release deliverables.
