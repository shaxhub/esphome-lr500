#include "lr500.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"

namespace esphome {
namespace lr500 {

static const char *const TAG = "lr500";

static uint16_t sum16(const uint8_t *p, size_t n) {
  uint32_t s = 0;
  for (size_t i = 0; i < n; i++) s += p[i];
  return s & 0xFFFF;
}

// Build a frame in `out` from a payload already laid out in `payload` of length `L`.
// Returns total frame length. Caller's buffer must hold at least L + 5 bytes.
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

  for (auto &v : last_) v = -1;

  pm25_sensor = new sensor::Sensor();
  pm25_sensor->set_name("LR500 PM2.5");
  pm25_sensor->set_object_id("lr500_pm25");
  pm25_sensor->set_unit_of_measurement("µg/m³");
  pm25_sensor->set_device_class("pm25");
  pm25_sensor->set_accuracy_decimals(1);
  pm25_sensor->set_state_class(sensor::STATE_CLASS_MEASUREMENT);
  App.register_sensor(pm25_sensor);

  temperature_sensor = new sensor::Sensor();
  temperature_sensor->set_name("LR500 Temperature");
  temperature_sensor->set_object_id("lr500_temperature");
  temperature_sensor->set_unit_of_measurement("°C");
  temperature_sensor->set_device_class("temperature");
  temperature_sensor->set_accuracy_decimals(1);
  temperature_sensor->set_state_class(sensor::STATE_CLASS_MEASUREMENT);
  App.register_sensor(temperature_sensor);

  humidity_sensor = new sensor::Sensor();
  humidity_sensor->set_name("LR500 Humidity");
  humidity_sensor->set_object_id("lr500_humidity");
  humidity_sensor->set_unit_of_measurement("%");
  humidity_sensor->set_device_class("humidity");
  humidity_sensor->set_accuracy_decimals(0);
  humidity_sensor->set_state_class(sensor::STATE_CLASS_MEASUREMENT);
  App.register_sensor(humidity_sensor);

  power_switch = new LR500Switch(this, fid::POWER);
  power_switch->set_name("LR500 Power");
  power_switch->set_object_id("lr500_power");
  App.register_switch(power_switch);

  uv_switch = new LR500Switch(this, fid::UV);
  uv_switch->set_name("LR500 UV");
  uv_switch->set_object_id("lr500_uv");
  App.register_switch(uv_switch);

  sleep_switch = new LR500Switch(this, fid::SLEEP);
  sleep_switch->set_name("LR500 Sleep");
  sleep_switch->set_object_id("lr500_sleep");
  App.register_switch(sleep_switch);

  auto_switch = new LR500Switch(this, fid::AUTO_MODE);
  auto_switch->set_name("LR500 Auto");
  auto_switch->set_object_id("lr500_auto");
  App.register_switch(auto_switch);

  fan_entity = new LR500Fan(this);
  fan_entity->set_name("LR500 Fan");
  fan_entity->set_object_id("lr500_fan");
  App.register_fan(fan_entity);

  send_heartbeat_();
}

void LR500::loop() {
  uint32_t now = millis();
  if (now - last_hb_ms_ >= 1000) {
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
  uint8_t payload[3] = {OP_HEARTBEAT, 0x00, 0x00};
  uint8_t buf[MAX_FRAME];
  size_t n = build_frame(buf, payload, sizeof(payload));
  this->write_array(buf, n);
}

void LR500::send_field(uint8_t field, uint32_t value, uint8_t width) {
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
    // Runaway frame (bad LEN byte after a stray 0xAA); resync.
    rx_len_ = 0;
    if (b != FRAME_START) return;
  }
  rx_buf_[rx_len_++] = b;

  if (rx_len_ < 2) return;
  uint8_t L = rx_buf_[1];
  size_t need = 1u + 1u + (size_t) L + 2u + 1u;
  if (need > RX_BUF_CAP) {
    rx_len_ = 0;  // declared LEN can't fit any legitimate frame
    return;
  }
  if (rx_len_ < need) return;

  if (rx_buf_[need - 1] != FRAME_END) {
    // Drop one byte and resync to the next 0xAA already in the buffer.
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

bool LR500::publish_if_changed_(uint8_t fid, int32_t val) {
  if (last_[fid] == val) return false;
  last_[fid] = val;
  return true;
}

void LR500::handle_state_(const uint8_t *body, size_t len) {
  size_t i = 0;
  while (i < len) {
    uint8_t f = body[i];
    uint8_t w = is_wide_id(f) ? 2 : 1;
    if (i + 1 + w > len) break;
    uint32_t val = (w == 2) ? (((uint32_t) body[i + 1] << 8) | body[i + 2]) : body[i + 1];
    i += 1 + w;

    if (!publish_if_changed_(f, (int32_t) val)) continue;

    switch (f) {
      case fid::PM25:
        if (pm25_sensor) pm25_sensor->publish_state(val / 10.0f);
        break;
      case fid::TEMPERATURE:
        if (temperature_sensor) temperature_sensor->publish_state(val / 256.0f);
        break;
      case fid::HUMIDITY:
        if (humidity_sensor) humidity_sensor->publish_state((float) val);
        break;
      case fid::POWER:
        if (power_switch) power_switch->publish_state(val != 0);
        break;
      case fid::AUTO_MODE:
        if (auto_switch) auto_switch->publish_state(val != 0);
        break;
      case fid::UV:
        if (uv_switch) uv_switch->publish_state(val != 0);
        break;
      case fid::SLEEP:
        if (sleep_switch) sleep_switch->publish_state(val != 0);
        break;
      case fid::FAN_SPEED:
        if (fan_entity) {
          // Update HA-side state directly — make_call().perform() would re-enter
          // LR500Fan::control() and bounce the same value back to the PIC.
          fan_entity->state = true;
          fan_entity->speed = (int) val;
          fan_entity->publish_state();
        }
        break;
      default:
        break;
    }
  }
}

void LR500Switch::write_state(bool state) {
  parent_->send_field(field_, state ? 1 : 0, 1);
  // Optimistic publish. If the PIC ignores us (e.g. auto-mode lockout), the
  // next state report drives the canonical value back through publish_state.
  this->publish_state(state);
}

void LR500Fan::control(const fan::FanCall &call) {
  bool dirty = false;

  if (call.get_state().has_value()) {
    bool want_on = *call.get_state();
    parent_->send_field(fid::POWER, want_on ? 1 : 0, 1);
    this->state = want_on;
    dirty = true;
    if (!want_on) {  // off => done, leave fan speed alone
      this->publish_state();
      return;
    }
  }
  if (call.get_speed().has_value()) {
    int s = *call.get_speed();
    if (s < 1) s = 1;
    if (s > 5) s = 5;
    parent_->send_field(fid::FAN_SPEED, (uint8_t) s, 1);
    this->speed = s;
    dirty = true;
  }

  if (dirty) this->publish_state();
}

}  // namespace lr500
}  // namespace esphome
