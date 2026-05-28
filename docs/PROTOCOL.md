# Beurer LR500 UART Protocol

This document describes the internal UART protocol used between the LR500's
two MCUs. Reverse-engineered in May 2026 by reading the FreshHome Android
APK, sniffing UART traffic, and fuzzing the PIC. See [README](../README.md)
for the why.

## Architecture

The LR500 is a two-MCU device:

- **PIC16F1936** (Microchip 8-bit) — the appliance controller. Drives motor,
  UV lamp, LEDs, sensors, debounces buttons. Owns the canonical device state.
- **RTL8710BX** (Realtek AmebaZ, ARM Cortex-M4F) — the network module.
  Runs WiFi + BLE radio, TLS, AWS IoT MQTT client, BLE provisioning. Stores
  the device's X.509 cert.

They talk over a 4-pin internal UART header labelled **WIFI** on the main
PCB: **9600 baud, 8N1, no flow control**. There is no authentication or
crypto at this layer — trust is purely physical.

## Wiring (replacing the WiFi module)

The 4-pin header on the main board is labelled from the **original WiFi
module's** perspective. Names line up directly with your replacement
microcontroller's pins:

| Header pad | Direction | Notes |
|---|---|---|
| `VCC` | **5 V** out to the daughterboard | use this to power your replacement. The stock RTL8710BX runs on 3.3 V because the daughterboard has its own local LDO; a Wemos D1 mini's `5V` pin accepts this directly. |
| `GND` | common ground | required |
| `TX` | module → PIC (heartbeats + commands) | connect to your replacement's **TX** pin — you'll be transmitting on this line, ending up at the PIC's RX. |
| `RX` | PIC → module (state reports) | connect to your replacement's **RX** pin — the PIC's `aa 24 a0 …` state frames arrive here. |

To impersonate the module: disconnect the 4-pin header, then wire
`TX → TX`, `RX → RX`, `GND → GND`, and use the `VCC` pin to power your
replacement.

**UART signal levels:** The 5 V VCC pin powers the original daughterboard,
which down-regulates to 3.3 V locally — so the VCC pin tells you nothing
about the UART signal voltage. Measure the PIC's TX pin directly with a
multimeter while traffic flows; the idle "high" reading is the signal
voltage. In most observed units it's **3.3 V** (the PIC runs on a 3.3 V
rail) and you can connect a 3.3 V UART adapter / ESP directly. If you
measure 5 V, drop the line to 3.3 V with a divider (e.g. 10 kΩ + 20 kΩ)
or a level shifter on the PIC-TX → ESP-RX direction. The opposite
direction (ESP-TX → PIC-RX at 3.3 V) is always safe because that exceeds
the PIC's ~2 V logic-high threshold.

## Frame format

All UART traffic in both directions uses the same envelope:

```
0xAA  LEN  <payload[LEN]>  CKSUM_HI  CKSUM_LO  0xEE
 │    │                    └────┬────┘         │
 │    │                         │              └─ end marker
 │    │                         └─ (LEN + sum(payload)) & 0xFFFF, big-endian
 │    └─ uint8: payload byte count
 └─ start marker
```

- `LEN` is just the payload byte count — it does **not** include the start
  byte, the length byte itself, the checksum, or the end byte.
- The checksum is a 16-bit big-endian sum of `LEN + every payload byte`,
  masked to 16 bits. For most frames the high byte is `0x00`; long state
  reports overflow into the low end of `0x02xx`.
- No escaping is used. The framing relies on the fixed start/end pair and an
  honest length prefix.

## Message catalogue

### Module → PIC

The stock module sends ~1 Hz of traffic. Recommended to keep doing this in
any replacement, but it's **not strictly required**: the PIC accepts writes
indefinitely with a silent neighbour. What the heartbeat actually does is
*solicit* the PIC's periodic state report — see [Periodic vs. event-driven
state](#periodic-vs-event-driven-state) below.

| Frame | Meaning |
|---|---|
| `aa 03 14 00 00 00 17 ee` | **Idle heartbeat** — sent ~1× per second. Payload is always `14 00 00`. The PIC responds with a full state report. |
| `aa 03 a4 NN 00 00 …` | **WiFi state notification.** `NN` cycles through `01 02 03 04` as the radio progresses through connection states. |
| `aa 03 a5 00 00 00 a8 ee` | **WiFi connected.** Tells the PIC the module is on the network. |

### Module → PIC (control writes — what we send)

```
aa  LEN  <FIELD_ID>  <VALUE…>  CKSUM_HI CKSUM_LO  ee
```

- `LEN = 1 + sizeof(VALUE)` — i.e. `02` for 1-byte fields, `03` for 2-byte.
- There is **no opcode byte.** The field ID is the first byte of the payload
  and the PIC dispatches directly on it.
- Each field has a fixed width (see below). Sending the wrong width produces
  unpredictable behaviour.

Examples:
```
aa 02 08 01 00 0b ee     # power on
aa 02 08 00 00 0a ee     # power off
aa 02 09 03 00 0e ee     # fan = speed 3
aa 02 0b 01 00 0e ee     # UV on
```

### PIC → Module (state report)

```
aa 24 a0 <TLV stream> CKSUM_HI CKSUM_LO ee
```

The leading payload byte `0xA0` identifies the frame as a state report;
everything after it is a fixed-order stream of `<field_id> <value(s)>` pairs
covering the full device state. There is no length byte inside the TLV —
each field has a known width.

The state report is the only feedback channel for writes: the PIC does **not**
ack individual commands. Confirmation that a write took effect comes from
seeing the corresponding field change in the next state report.

#### Periodic vs. event-driven state

By default the PIC pushes a state report **for every heartbeat it receives**,
giving an effective ~1 Hz refresh rate. *Plus* it pushes an unsolicited state
report whenever a value actually changes (e.g. the user presses a physical
button, a sensor reading crosses an internal threshold).

If you stop sending heartbeats, the PIC keeps accepting writes normally, but
its state pushes become purely event-driven — you only hear from it on
change. That's fine for a controller that doesn't need constant readouts;
keep heartbeating if you want a heartbeat-rate refresh of the sensor values.

## Field reference

| ID | Width | Reported meaning | Writable | Notes |
|----|-------|------------------|----------|-------|
| **0x01** | 2 B | **air quality LED state** | — | Four bands matching the box's colours: `0x0100` = blue (≤ 12 µg/m³), `0x0101` = yellow (12.1–35.4), `0x0102` = orange (35.5–55.4), `0x0103` = red (≥ 55.5). Observed falling-edge transitions are 1–3 µg/m³ below the rising thresholds (small hysteresis to prevent boundary flicker). PM2.5 ≥ 192 µg/m³ still reports `0x0103` — there is no fifth band. |
| 0x02 | 2 B | PM2.5 × 10 (µg/m³) | — | sensor; e.g. `0x0049 = 73 → 7.3 µg/m³` |
| 0x03 | 2 B | temperature × 256 (°C) | — | sensor; e.g. `0x1800 → 24.0 °C` |
| 0x04 | 1 B | humidity % | — | sensor |
| 0x06 | 2 B | likely **filter life remaining (hours)** | — | Constant `4320` (= 180 days × 24 h, a standard HEPA filter lifetime) across captures with wildly varying PM2.5. Almost certainly not a real-time sensor — needs a multi-day capture to confirm it ticks down at ~24/day. |
| 0x07 | 1 B | **buzzer trigger** (one-shot) | yes | write of *any* value produces one beep; no persistent state. Likely the PIC's internal "command-received" tone exposed as a field. |
| **0x08** | 1 B | **power** (`0`/`1`) | yes | |
| **0x09** | 1 B | **fan speed**: `1`–`4` = speeds, `5` = turbo | yes | reads back as `1` while in auto mode |
| **0x0A** | 1 B | **auto-mode flag** | yes | writing **any non-zero value** triggers auto |
| **0x0B** | 1 B | **UV lamp** (`0`/`1`) | yes | |
| **0x0D** | 1 B | **sleep mode** (`0`/`1`) | yes | |
| **0x0E** | 1 B | **buzzer / key-tone enable** | yes | only value `1` enables the buzzer; any other value (`0`, `2`, `0xA5`, `0xFF`, …) silences it. State report echoes the literal byte you wrote — so the field is reported but the *behaviour* is binary. |
| **0x12** | 1 B | **display unit** (`0`=°C, `1`=°F) | yes | |
| 0x20 | 2 B | unknown (always `0x0000`) | — | |
| 0x22 | 1 B | unknown (always `0x02`) | — | possibly filter status code |

Width is implicit per field ID — the parser must know it. Wide IDs observed:
`{0x01, 0x02, 0x03, 0x06, 0x20}`. All others are single-byte. There is no
length byte to recover from a guess.

## Behavioural notes & quirks

1. **No write acknowledgement.** Writes succeed silently; their effect is only
   visible in the next state report.
2. **Auto-mode lockout.** Once `field 0x0A = 1`, most field writes are silently
   ignored. Exit by writing a non-`1` value to field `0x09` (e.g.
   `aa 02 09 02` to switch to manual fan speed 2), or pressing the device's
   physical mode button.
3. **Heartbeat is optional but recommended.** The PIC accepts writes
   indefinitely without it. What it does is solicit a state report — silent
   neighbour means event-driven state pushes only.
4. **Auto-flag writes ignore the value byte.** `aa 03 0a XX YY` always triggers
   auto regardless of `XX`/`YY`. The PIC only checks "is this addressing
   field 0x0a?"
5. **Buzzer fields.** `0x07` is a one-shot tone trigger (write any value, hear
   one beep, no persistent state). `0x0E` is a persistent buzzer enable —
   value `1` keeps key tones on, anything else silences. `0x0E`'s state
   report echoes the literal written byte, so a read of `0x0E = 0x02` still
   means "buzzer off."
6. **Multi-byte values are big-endian.**

## Reference Python implementation

```python
WIDE_IDS = {0x01, 0x02, 0x03, 0x06, 0x20}

def frame(payload: bytes) -> bytes:
    """Wrap a payload in the AA … EE envelope with checksum."""
    L = len(payload)
    c = (L + sum(payload)) & 0xFFFF
    return bytes([0xAA, L]) + bytes(payload) + bytes([c >> 8, c & 0xFF, 0xEE])

def parse_state(f: bytes) -> dict[int, int] | None:
    """Decode an `aa .. a0 .. ee` state report into {field_id: value}."""
    if len(f) < 5 or f[0] != 0xAA or f[-1] != 0xEE or f[2] != 0xA0:
        return None
    L = f[1]
    body = f[3 : 2 + L]
    out, i = {}, 0
    while i < len(body):
        fid = body[i]
        w = 2 if fid in WIDE_IDS else 1
        if i + 1 + w > len(body):
            break
        out[fid] = int.from_bytes(body[i + 1 : i + 1 + w], "big")
        i += 1 + w
    return out

HEARTBEAT = frame([0x14, 0x00, 0x00])    # must be sent ~1 Hz

power_on   = frame([0x08, 0x01])
fan_speed3 = frame([0x09, 0x03])
uv_on      = frame([0x0B, 0x01])
enter_auto = frame([0x0A, 0x01])
```
