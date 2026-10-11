#include "lr500.h"
#include <cmath>
#include <cstring>
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
  initialize_tx_();
  // Keep the first heartbeat one second after setup; mute is already initialized.
  last_hb_ms_ = millis();
  communication_watch_ms_ = last_hb_ms_;
  publish_communication_(false);
}

void LR500::initialize_tx_() {
  if (tx_initialized_) return;
  tx_initialized_ = true;
#ifdef USE_SWITCH
  if (mute_switch_) {
    auto initial = mute_switch_->get_initial_state_with_restore_mode();
    set_tx_muted(initial.has_value() ? *initial : tx_muted_);
  }
#endif
}

bool LR500::is_tx_muted() {
  // Also covers control writes from an early on_boot automation.
  initialize_tx_();
  return tx_muted_;
}

void LR500::set_tx_muted(bool m) {
  tx_initialized_ = true;
  if (tx_muted_ != m) {
    // Resume with a fresh timeout window, allowing the next heartbeat to solicit a report.
    communication_watch_ms_ = millis();
    communication_stale_ = false;
    status_clear_warning();
  }
  tx_muted_ = m;
  // Republish via the bound switch entity so any programmatic mute toggle
  // (lambdas, future watchdogs, etc.) shows up in HA.
#ifdef USE_SWITCH
  if (mute_switch_) mute_switch_->publish_state(m);
#endif
}

void LR500::loop() {
  uint32_t now = millis();
  if (rx_len_ != 0 && now - last_rx_byte_ms_ >= RX_TIMEOUT_MS) rx_len_ = 0;
  if (!is_tx_muted() && now - last_hb_ms_ >= 1000) {
    send_heartbeat_();
    last_hb_ms_ = now;
  }
  while (this->available()) {
    uint8_t b = 0;
    if (!this->read_byte(&b)) break;
    process_byte_(b);
  }
  check_communication_(millis());
}

void LR500::dump_config() {
  ESP_LOGCONFIG(TAG, "LR500 air purifier (UART 9600 8N1)");
  ESP_LOGCONFIG(TAG, "  Communication timeout: %u ms (paused while TX is muted)", communication_timeout_ms_);
  ESP_LOGCONFIG(TAG, "  UART TX muted: %s", tx_muted_ ? "YES" : "NO");
  this->check_uart_settings(9600, 1, uart::UART_CONFIG_PARITY_NONE, 8);
}

void LR500::publish_communication_(bool connected) {
#ifdef USE_BINARY_SENSOR
  if (communication_sensor_) communication_sensor_->publish_state(connected);
#endif
}

void LR500::check_communication_(uint32_t now) {
  // Without heartbeats, the PIC sends only on change. Silence while muted is expected.
  if (tx_muted_ || communication_stale_ || now - communication_watch_ms_ < communication_timeout_ms_) return;
  communication_stale_ = true;
  last_auto_ = -1;
  ESP_LOGW(TAG, "No valid PIC state report within %u ms", communication_timeout_ms_);
  status_set_warning("No valid PIC state report");
  publish_communication_(false);
#ifdef USE_SENSOR
  if (pm25_sensor_) pm25_sensor_->publish_state(NAN);
  if (temperature_sensor_) temperature_sensor_->publish_state(NAN);
  if (humidity_sensor_) humidity_sensor_->publish_state(NAN);
  if (filter_hours_sensor_) filter_hours_sensor_->publish_state(NAN);
#endif
}

void LR500::send_heartbeat_() {
  if (is_tx_muted()) return;
  uint8_t payload[3] = {OP_HEARTBEAT, 0x00, 0x00};
  uint8_t buf[MAX_FRAME];
  size_t n = build_frame(buf, payload, sizeof(payload));
  this->write_array(buf, n);
}

void LR500::send_field(uint8_t field, uint32_t value, uint8_t width) {
  if (is_tx_muted()) {
    ESP_LOGD(TAG, "muted — dropping write field=0x%02X value=%u", field, value);
    return;
  }
  if (width != 1 && width != 2) {
    ESP_LOGW(TAG, "Invalid write width %u for field 0x%02X", width, field);
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

void LR500::set_manual_speed(uint8_t speed) {
  if (is_tx_muted()) return;
  if (speed < 1 || speed > 5) return;
  // Speed 1 cannot exit auto. Unknown state gets the same escape to cover boot
  // before the first report. The next PIC report remains the source of truth.
  if (speed == 1 && last_auto_ != 0) send_field(fid::FAN_SPEED, 2, 1);
  send_field(fid::FAN_SPEED, speed, 1);
}

void LR500::consume_rx_(size_t count) {
  rx_len_ -= count;
  std::memmove(rx_buf_, rx_buf_ + count, rx_len_);
}

void LR500::process_byte_(uint8_t b) {
  uint32_t now = millis();
  if (rx_len_ != 0 && now - last_rx_byte_ms_ >= RX_TIMEOUT_MS) rx_len_ = 0;
  last_rx_byte_ms_ = now;
  if (rx_len_ == 0 && b != FRAME_START) return;

  if (rx_len_ >= RX_BUF_CAP) {
    rx_len_ = 0;
    if (b != FRAME_START) return;
  }
  rx_buf_[rx_len_++] = b;

  while (rx_len_ != 0) {
    if (rx_buf_[0] != FRAME_START) {
      consume_rx_(1);
      continue;
    }
    if (rx_len_ < 2) return;
    uint8_t L = rx_buf_[1];
    size_t need = (size_t) L + 5;
    if (need > RX_BUF_CAP) {
      consume_rx_(1);
      continue;
    }
    if (rx_len_ < need) return;
    uint16_t expected = ((uint16_t) L + sum16(rx_buf_ + 2, L)) & 0xFFFF;
    uint16_t got = ((uint16_t) rx_buf_[need - 3] << 8) | rx_buf_[need - 2];
    if (rx_buf_[need - 1] != FRAME_END || expected != got) {
      ESP_LOGD(TAG, "Discarding invalid UART frame candidate");
      consume_rx_(1);
      continue;
    }
    if (L > 0 && rx_buf_[2] == OP_STATE) handle_state_(rx_buf_ + 3, (size_t) L - 1);
    // A recovered candidate can be followed by another frame already in the buffer.
    consume_rx_(need);
  }
}

void LR500::handle_state_(const uint8_t *body, size_t len) {
  // Validate the entire TLV stream before publishing anything or declaring the
  // connection healthy. A truncated field must not partially update entities.
  if (len == 0) return;
  for (size_t pos = 0; pos < len;) {
    size_t width = is_wide_id(body[pos]) ? 2 : 1;
    if (pos + 1 + width > len) {
      ESP_LOGW(TAG, "Truncated PIC state field");
      return;
    }
    pos += 1 + width;
  }
  last_state_ms_ = millis();
  communication_watch_ms_ = last_state_ms_;
  communication_stale_ = false;
  status_clear_warning();
  publish_communication_(true);
  // We publish unconditionally on every state report. The previous design
  // cached the raw echo byte and short-circuited identical publishes, but
  // that meant any time the PIC silently ignored a write (auto-mode lockout,
  // tx_mute window, AUTO_MODE write=0) the corrective echo also matched the
  // cache and the entity stayed stuck at the optimistic-but-wrong value.
  // Switch::publish_state and the sensor filter chain already dedupe
  // efficiently for the no-change case.
#ifdef USE_FAN
  bool have_power = false, power_on = false;
  bool have_fan_speed = false;
  uint32_t fan_speed_val = 0;
#endif

  size_t i = 0;
  while (i < len) {
    uint8_t f = body[i];
    uint8_t w = is_wide_id(f) ? 2 : 1;
    if (i + 1 + w > len) break;
    uint32_t val = (w == 2) ? (((uint32_t) body[i + 1] << 8) | body[i + 2]) : body[i + 1];
    i += 1 + w;

    switch (f) {
#ifdef USE_SENSOR
      case fid::PM25:
        if (pm25_sensor_) pm25_sensor_->publish_state(val / 10.0f);
        break;
      case fid::TEMPERATURE:
        if (temperature_sensor_) temperature_sensor_->publish_state(val / 256.0f);
        break;
      case fid::HUMIDITY:
        if (humidity_sensor_) humidity_sensor_->publish_state((float) val);
        break;
      case fid::FILTER_HOURS:
        if (filter_hours_sensor_) filter_hours_sensor_->publish_state((float) val);
        break;
#endif
      case fid::POWER:
#ifdef USE_FAN
        have_power = true;
        power_on = (val != 0);
#endif
        last_power_ = val != 0 ? 1 : 0;
#ifdef USE_SWITCH
        if (power_switch_) power_switch_->publish_state(val != 0);
#endif
        break;
      case fid::AUTO_MODE:
        last_auto_ = val != 0 ? 1 : 0;
#ifdef USE_SWITCH
        if (auto_switch_) auto_switch_->publish_state(val != 0);
#endif
        break;
#ifdef USE_SWITCH
      case fid::UV:
        if (uv_switch_) uv_switch_->publish_state(val != 0);
        break;
      case fid::SLEEP:
        if (sleep_switch_) sleep_switch_->publish_state(val != 0);
        break;
      case fid::UNIT:
        // 0 = °C, 1 = °F. The Fahrenheit switch is ON when the display is °F.
        if (unit_switch_) unit_switch_->publish_state(val != 0);
        break;
      case fid::BUZZER:
        // PIC only treats value 1 as "buzzer enabled"; 0, 2, 0xA5, 0xFF
        // are all silent. State report echoes the literal byte.
        if (buzzer_switch_) buzzer_switch_->publish_state(val == 1);
        break;
#endif
#ifdef USE_FAN
      case fid::FAN_SPEED:
        have_fan_speed = true;
        fan_speed_val = val;
        break;
#endif
      default:
        break;
    }
  }

  // Fan publish is deferred so it sees both POWER and FAN_SPEED from this
  // frame regardless of TLV order, and so we never momentarily report
  // state=true with speed=0 (or fan-on while POWER=0).
#ifdef USE_FAN
  if (have_fan_speed && fan_entity_) {
    bool is_on = have_power ? power_on : (last_power_ != 0);
    fan_entity_->state = is_on;
    fan_entity_->speed = (int) fan_speed_val;
    fan_entity_->publish_state();
  }
#endif
}

#ifdef USE_SWITCH
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
    parent_->set_manual_speed(2);
  } else {
    parent_->send_field(field_, state ? 1 : 0, 1);
  }
  this->publish_state(state);
}
#endif

#ifdef USE_BUTTON
void LR500Button::press_action() {
  // send_field already no-ops when TX is muted; a button has no state to
  // restore, so a dropped press is simply silent.
  parent_->send_field(field_, value_, 1);
}
#endif

#ifdef USE_SWITCH
void LR500MuteSwitch::write_state(bool state) {
  // set_tx_muted republishes via mute_switch_, so no extra publish_state
  // call is needed here. Same-state calls still publish the initial state.
  parent_->set_tx_muted(state);
}
#endif

#ifdef USE_FAN
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
    parent_->set_manual_speed((uint8_t) want_speed);
  } else {
    parent_->send_field(fid::POWER, 0, 1);
  }

  this->state = want_state;
  this->speed = want_speed;
  this->publish_state();
}
#endif

}  // namespace lr500
}  // namespace esphome
