#include "lr500.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"

namespace esphome {
namespace lr500 {

static const char *const TAG = "lr500";
static const std::set<uint8_t> WIDE_IDS = {0x01, 0x02, 0x03, 0x06, 0x20};

// ---------------- frame helpers ----------------
static uint16_t sum16(const uint8_t *p, size_t n) {
  uint32_t s = 0;
  for (size_t i = 0; i < n; i++) s += p[i];
  return s & 0xFFFF;
}

static std::vector<uint8_t> make_frame(std::initializer_list<uint8_t> payload) {
  std::vector<uint8_t> f;
  f.reserve(payload.size() + 5);
  f.push_back(0xAA);
  f.push_back((uint8_t) payload.size());
  for (auto b : payload) f.push_back(b);
  uint16_t c = ((uint16_t) payload.size() + sum16(f.data() + 2, payload.size())) & 0xFFFF;
  f.push_back((uint8_t) (c >> 8));
  f.push_back((uint8_t) (c & 0xFF));
  f.push_back(0xEE);
  return f;
}

// ---------------- LR500 ----------------
void LR500::setup() {
  ESP_LOGCONFIG(TAG, "Setting up LR500");

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

  power_switch = new LR500Switch(this, 0x08);
  power_switch->set_name("LR500 Power");
  power_switch->set_object_id("lr500_power");
  App.register_switch(power_switch);

  uv_switch = new LR500Switch(this, 0x0B);
  uv_switch->set_name("LR500 UV");
  uv_switch->set_object_id("lr500_uv");
  App.register_switch(uv_switch);

  sleep_switch = new LR500Switch(this, 0x0D);
  sleep_switch->set_name("LR500 Sleep");
  sleep_switch->set_object_id("lr500_sleep");
  App.register_switch(sleep_switch);

  auto_switch = new LR500Switch(this, 0x0A);
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
  auto f = make_frame({0x14, 0x00, 0x00});
  this->write_array(f.data(), f.size());
}

void LR500::send_field(uint8_t field, uint32_t value, uint8_t width) {
  std::vector<uint8_t> payload;
  payload.push_back(field);
  if (width == 2) {
    payload.push_back((uint8_t) ((value >> 8) & 0xFF));
    payload.push_back((uint8_t) (value & 0xFF));
  } else {
    payload.push_back((uint8_t) (value & 0xFF));
  }
  uint8_t L = (uint8_t) payload.size();
  uint16_t c = ((uint16_t) L + sum16(payload.data(), L)) & 0xFFFF;
  std::vector<uint8_t> f;
  f.reserve(L + 5);
  f.push_back(0xAA);
  f.push_back(L);
  for (auto b : payload) f.push_back(b);
  f.push_back((uint8_t) (c >> 8));
  f.push_back((uint8_t) (c & 0xFF));
  f.push_back(0xEE);
  this->write_array(f.data(), f.size());
  ESP_LOGD(TAG, "Sent field 0x%02X = %u (w=%u)", field, value, width);
}

void LR500::process_byte_(uint8_t b) {
  if (rx_buf_.empty() && b != 0xAA) return;
  rx_buf_.push_back(b);
  if (rx_buf_.size() < 2) return;
  uint8_t L = rx_buf_[1];
  size_t need = 1u + 1u + (size_t) L + 2u + 1u;
  if (rx_buf_.size() < need) return;
  if (rx_buf_[need - 1] != 0xEE) {
    rx_buf_.erase(rx_buf_.begin());
    return;
  }
  uint16_t expected = ((uint16_t) L + sum16(rx_buf_.data() + 2, L)) & 0xFFFF;
  uint16_t got = ((uint16_t) rx_buf_[need - 3] << 8) | rx_buf_[need - 2];
  if (expected != got) {
    ESP_LOGW(TAG, "bad cksum");
    rx_buf_.clear();
    return;
  }
  if (L > 0 && rx_buf_[2] == 0xA0) {
    handle_state_(rx_buf_.data() + 3, (size_t) L - 1);
  }
  rx_buf_.clear();
}

void LR500::handle_state_(const uint8_t *body, size_t len) {
  size_t i = 0;
  while (i < len) {
    uint8_t fid = body[i];
    uint8_t w = WIDE_IDS.count(fid) ? 2 : 1;
    if (i + 1 + w > len) break;
    uint32_t val = (w == 2) ? (((uint32_t) body[i + 1] << 8) | body[i + 2]) : body[i + 1];
    i += 1 + w;

    int idx = -1;
    switch (fid) {
      case 0x02:
        if (pm25_sensor) pm25_sensor->publish_state(val / 10.0f);
        break;
      case 0x03:
        if (temperature_sensor) temperature_sensor->publish_state(val / 256.0f);
        break;
      case 0x04:
        if (humidity_sensor) humidity_sensor->publish_state((float) val);
        break;
      case 0x08: idx = 8; if (power_switch && (int) val != last_[idx]) { power_switch->publish_state(val != 0); last_[idx] = val; } break;
      case 0x0A: idx = 10; if (auto_switch && (int) val != last_[idx]) { auto_switch->publish_state(val != 0); last_[idx] = val; } break;
      case 0x0B: idx = 11; if (uv_switch && (int) val != last_[idx]) { uv_switch->publish_state(val != 0); last_[idx] = val; } break;
      case 0x0D: idx = 13; if (sleep_switch && (int) val != last_[idx]) { sleep_switch->publish_state(val != 0); last_[idx] = val; } break;
      case 0x09: idx = 9;
        if (fan_entity && (int) val != last_[idx]) {
          auto call = fan_entity->make_call();
          call.set_state(true);
          call.set_speed((int) val);
          call.perform();
          last_[idx] = val;
        }
        break;
      default: break;
    }
  }
}

// ---------------- LR500Switch ----------------
void LR500Switch::write_state(bool state) {
  parent_->send_field(field_, state ? 1 : 0, 1);
  this->publish_state(state);
}

// ---------------- LR500Fan ----------------
void LR500Fan::control(const fan::FanCall &call) {
  if (call.get_state().has_value() && !*call.get_state()) {
    parent_->send_field(0x08, 0, 1);
    this->state = false;
    this->publish_state();
    return;
  }
  if (call.get_state().has_value() && *call.get_state()) {
    parent_->send_field(0x08, 1, 1);
    this->state = true;
  }
  if (call.get_speed().has_value()) {
    int s = *call.get_speed();
    if (s < 1) s = 1;
    if (s > 5) s = 5;
    parent_->send_field(0x09, (uint8_t) s, 1);
    this->speed = s;
  }
  this->publish_state();
}

}  // namespace lr500
}  // namespace esphome
