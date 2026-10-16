# Contributing

The component lives in `components/lr500/`. Python modules validate YAML and
generate bindings; `lr500.h` and `lr500.cpp` implement the UART protocol and
entity behavior. Keep protocol changes in sync with [PROTOCOL.md](docs/PROTOCOL.md),
the [example](examples/lr500.yaml), and [CHANGELOG.md](CHANGELOG.md).

## Development setup

From the repository root, use the supported ESPHome baseline:

```bash
python3 -m venv .venv
source .venv/bin/activate
python -m pip install esphome==2026.5.1
```

Prepare an ignored working configuration:

```bash
mkdir -p .esphome/validation
cp examples/lr500.yaml .esphome/validation/lr500.yaml
cp examples/secrets.yaml.example .esphome/validation/secrets.yaml
```

In the copied YAML, replace the entire `external_components:` block with:

```yaml
external_components:
  - source:
      type: local
      path: ../../components
    components: [lr500]
```

This builds the checkout being edited. The published example's GitHub source
fetches remote code, which will not include local changes.

In the copied `secrets.yaml`, replace the `api_key` placeholder with a valid key
from `openssl rand -base64 32`. The other placeholder credentials are sufficient
for compilation; use your own credentials only if you intend to flash the device.
Keep secrets, generated configurations, and firmware out of commits.

## Local validation

```bash
esphome config .esphome/validation/lr500.yaml
esphome compile --only-generate .esphome/validation/lr500.yaml
esphome compile .esphome/validation/lr500.yaml
git diff --check
```

Source generation checks schemas and bindings. A full firmware compile also
checks C++ dependencies and linking; the first build downloads the ESP8266
toolchain and libraries.

When changing platform guards or entity bindings, repeat generation and
compilation for these configurations, always using the local source:

| Configuration | Entity platforms to keep |
| --- | --- |
| Parent only | None; keep `uart:` and `lr500:` |
| Sensor only | `sensor:` with at least PM2.5 |
| Switch only | `switch:` with an appliance switch and TX mute |
| Fan only | `fan:` |
| Button only | `button:` with filter reset |
| Diagnostic only | `binary_sensor:` |
| README reference | All platforms from the README's YAML reference |
| Full example | All entities in `examples/lr500.yaml` |

Remove unused top-level platform blocks entirely. Check that invalid UART
settings (baud rate, data bits, parity, stop bits, or missing TX/RX pins) and
appliance-switch restore modes other than `DISABLED` are rejected. Keep
`USE_*` guards around all references to optional entity types.

There is currently no automated test suite or CI workflow. Record the ESPHome
version, configurations checked, and whether validation stopped at generation
or included a complete firmware build in your PR.

## Hardware reports

For appliance behavior changes, include the board revision, ESP board, YAML
with secrets removed, and relevant UART captures or ESPHome logs. A successful
build does not verify PIC command timing or electrical compatibility. Keep
observed behavior separate from inferred protocol behavior.

See [RELEASING.md](docs/RELEASING.md) for the manual release checks.
