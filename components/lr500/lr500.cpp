#include "lr500.h"
#include "esphome/core/log.h"

namespace esphome {
namespace lr500 {

static const char *const TAG = "lr500";

static uint16_t sum16(const uint8_t *p, size_t n) {
  uint32_t s = 0;
  for (size_t i = 0; i < n; i++) s += p[i];
  return s & 0xFFFF;
}

static size_t build_frame(uint8_t *out, const uint8_t *payload, uint8_t L) {
  out[0] = FRAME_START;
  out[1] = L;
  for (uint8_t i = 0; i < L; i++) out[2 + i] = payload[i];
  uint16_t c = ((uint16_t) L + sum16(payload, L)) & 0xFFFF;
  out[2 + L] = (uint8_t) (c >> 8);
  out[3 + L] = (uint8_t) (c & 0xFF);
  out[4 + L] = FRAME_END;
  return (size_t) L + 5;
}

void LR500::setup() {
  ESP_LOGCONFIG(TAG, "Setting up LR500");
  // First heartbeat is deferred to loop() so it fires ~1 s after boot,
  // giving the mute switch's restore mechanism time to settle. Stamping
  // last_hb_ms_ with the current time means the loop's 1 s gate becomes
  // an actual 1 s window rather than firing on the very next tick.
  last_hb_ms_ = millis();
}

void LR500::set_tx_muted(bool m) {
  if (tx_muted_ == m) return;
  tx_muted_ = m;
  // Republish via the bound switch entity so any programmatic mute toggle
  // (lambdas, future watchdogs, etc.) shows up in HA.
  if (mute_switch_) mute_switch_->publish_state(m);
}

void LR500::loop() {
  uint32_t now = millis();
  if (!tx_muted_ && now - last_hb_ms_ >= 1000) {
    send_heartbeat_();
    last_hb_ms_ = now;
  }
  while (this->available()) {
    uint8_t b = 0;
    this->read_byte(&b);
    process_byte_(b);
  }
}

void LR500::dump_config() {
  ESP_LOGCONFIG(TAG, "LR500 air purifier (UART 9600 8N1)");
}

void LR500::send_heartbeat_() {
  if (tx_muted_) return;
  uint8_t payload[3] = {OP_HEARTBEAT, 0x00, 0x00};
  uint8_t buf[MAX_FRAME];
  size_t n = build_frame(buf, payload, sizeof(payload));
  this->write_array(buf, n);
}

void LR500::send_field(uint8_t field, uint32_t value, uint8_t width) {
  if (tx_muted_) {
    ESP_LOGD(TAG, "muted — dropping write field=0x%02X value=%u", field, value);
    return;
  }
  uint8_t payload[3];
  payload[0] = field;
  if (width == 2) {
    payload[1] = (uint8_t) ((value >> 8) & 0xFF);
    payload[2] = (uint8_t) (value & 0xFF);
  } else {
    payload[1] = (uint8_t) (value & 0xFF);
  }
  uint8_t L = (uint8_t) (1 + width);
  uint8_t buf[MAX_FRAME];
  size_t n = build_frame(buf, payload, L);
  this->write_array(buf, n);
  ESP_LOGD(TAG, "Sent field 0x%02X = %u (w=%u)", field, value, width);
}

void LR500::process_byte_(uint8_t b) {
  if (rx_len_ == 0 && b != FRAME_START) return;

  if (rx_len_ >= RX_BUF_CAP) {
    rx_len_ = 0;
    if (b != FRAME_START) return;
  }
  rx_buf_[rx_len_++] = b;

  if (rx_len_ < 2) return;
  uint8_t L = rx_buf_[1];
  size_t need = 1u + 1u + (size_t) L + 2u + 1u;
  if (need > RX_BUF_CAP) {
    // Implausible LEN — most commonly a stray 0xAA preceded the real frame
    // start, so the *second* 0xAA landed in rx_buf_[1] and got read as a
    // ridiculous length. Resync forward to the next 0xAA so we don't drop
    // the real frame that's about to arrive.
    size_t i = 1;
    while (i < rx_len_ && rx_buf_[i] != FRAME_START) i++;
    if (i < rx_len_) {
      rx_len_ -= i;
      for (size_t j = 0; j < rx_len_; j++) rx_buf_[j] = rx_buf_[i + j];
    } else {
      rx_len_ = 0;
    }
    return;
  }
  if (rx_len_ < need) return;

  if (rx_buf_[need - 1] != FRAME_END) {
    size_t i = 1;
    while (i < rx_len_ && rx_buf_[i] != FRAME_START) i++;
    rx_len_ = (i < rx_len_) ? rx_len_ - i : 0;
    for (size_t j = 0; j < rx_len_; j++) rx_buf_[j] = rx_buf_[i + j];
    return;
  }

  uint16_t expected = ((uint16_t) L + sum16(rx_buf_ + 2, L)) & 0xFFFF;
  uint16_t got = ((uint16_t) rx_buf_[need - 3] << 8) | rx_buf_[need - 2];
  if (expected != got) {
    ESP_LOGW(TAG, "bad cksum");
    rx_len_ = 0;
    return;
  }

  if (L > 0 && rx_buf_[2] == OP_STATE) {
    handle_state_(rx_buf_ + 3, (size_t) L - 1);
  }
  rx_len_ = 0;
}

void LR500::handle_state_(const uint8_t *body, size_t len) {
  // We publish unconditionally on every state report. The previous design
  // cached the raw echo byte and short-circuited identical publishes, but
  // that meant any time the PIC silently ignored a write (auto-mode lockout,
  // tx_mute window, AUTO_MODE write=0) the corrective echo also matched the
  // cache and the entity stayed stuck at the optimistic-but-wrong value.
  // Switch::publish_state and the sensor filter chain already dedupe
  // efficiently for the no-change case.
  bool have_power = false, power_on = false;
  bool have_fan_speed = false;
  uint32_t fan_speed_val = 0;

  size_t i = 0;
  while (i < len) {
    uint8_t f = body[i];
    uint8_t w = is_wide_id(f) ? 2 : 1;
    if (i + 1 + w > len) break;
    uint32_t val = (w == 2) ? (((uint32_t) body[i + 1] << 8) | body[i + 2]) : body[i + 1];
    i += 1 + w;

    switch (f) {
      case fid::PM25:
        if (pm25_sensor_) pm25_sensor_->publish_state(val / 10.0f);
        break;
      case fid::TEMPERATURE:
        if (temperature_sensor_) temperature_sensor_->publish_state(val / 256.0f);
        break;
      case fid::HUMIDITY:
        if (humidity_sensor_) humidity_sensor_->publish_state((float) val);
        break;
      case fid::POWER:
        have_power = true;
        power_on = (val != 0);
        last_power_ = power_on ? 1 : 0;
        if (power_switch_) power_switch_->publish_state(power_on);
        break;
      case fid::AUTO_MODE:
        if (auto_switch_) auto_switch_->publish_state(val != 0);
        break;
      case fid::UV:
        if (uv_switch_) uv_switch_->publish_state(val != 0);
        break;
      case fid::SLEEP:
        if (sleep_switch_) sleep_switch_->publish_state(val != 0);
        break;
      case fid::BUZZER:
        // PIC only treats value 1 as "buzzer enabled"; 0, 2, 0xA5, 0xFF
        // are all silent. State report echoes the literal byte.
        if (buzzer_switch_) buzzer_switch_->publish_state(val == 1);
        break;
      case fid::FAN_SPEED:
        have_fan_speed = true;
        fan_speed_val = val;
        break;
      default:
        break;
    }
  }

  // Fan publish is deferred so it sees both POWER and FAN_SPEED from this
  // frame regardless of TLV order, and so we never momentarily report
  // state=true with speed=0 (or fan-on while POWER=0).
  if (have_fan_speed && fan_entity_) {
    bool is_on = have_power ? power_on : (last_power_ != 0);
    fan_entity_->state = is_on;
    fan_entity_->speed = (int) fan_speed_val;
    fan_entity_->publish_state();
  }
}

void LR500Switch::write_state(bool state) {
  if (parent_->is_tx_muted()) {
    // The write would be dropped — don't lie to HA. Republish current state
    // so the UI snaps back from the user's optimistic toggle.
    this->publish_state(this->state);
    return;
  }

  if (field_ == fid::AUTO_MODE && !state) {
    // PIC ignores the value byte on writes to 0x0A: aa 02 0a 00 still
    // triggers auto rather than exiting it. The documented escape (see
    // docs/PROTOCOL.md "Behavioural notes & quirks" #2) is a non-1 write
    // to FAN_SPEED. Default to speed 2.
    parent_->send_field(fid::FAN_SPEED, 2, 1);
  } else {
    parent_->send_field(field_, state ? 1 : 0, 1);
  }
  this->publish_state(state);
}

void LR500MuteSwitch::write_state(bool state) {
  // set_tx_muted republishes via mute_switch_, so no extra publish_state
  // call is needed here — and it short-circuits on no-op flips.
  parent_->set_tx_muted(state);
}

void LR500Fan::control(const fan::FanCall &call) {
  if (parent_->is_tx_muted()) {
    // Snap HA back to the actual state; the write would be a no-op.
    this->publish_state();
    return;
  }

  bool want_state = this->state;
  // Use 3 as a fallback when speed is uninitialized (boot before first
  // FAN_SPEED echo). Without this, a state=true call with no speed would
  // publish "on at speed 0".
  int want_speed = this->speed > 0 ? this->speed : 3;

  if (call.get_state().has_value()) want_state = *call.get_state();
  if (call.get_speed().has_value()) {
    int s = *call.get_speed();
    if (s < 1) s = 1;
    if (s > 5) s = 5;
    want_speed = s;
    // Setting a speed without an explicit state implies "turn on at this
    // speed" — matches the percentage-slider UX in HA.
    if (!call.get_state().has_value()) want_state = true;
  }

  if (want_state) {
    parent_->send_field(fid::POWER, 1, 1);
    parent_->send_field(fid::FAN_SPEED, (uint8_t) want_speed, 1);
  } else {
    parent_->send_field(fid::POWER, 0, 1);
  }

  this->state = want_state;
  this->speed = want_speed;
  this->publish_state();
}

}  // namespace lr500
}  // namespace esphome
