#pragma once
#include <cstdint>
#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/fan/fan.h"

namespace esphome {
namespace lr500 {

namespace fid {
constexpr uint8_t PM25        = 0x02;  // 2 B, raw / 10 = µg/m³
constexpr uint8_t TEMPERATURE = 0x03;  // 2 B, raw / 256 = °C
constexpr uint8_t HUMIDITY    = 0x04;  // 1 B, %
constexpr uint8_t POWER       = 0x08;  // 1 B bool
constexpr uint8_t FAN_SPEED   = 0x09;  // 1 B, 1..5
constexpr uint8_t AUTO_MODE   = 0x0A;  // 1 B bool — any non-zero write enters auto
constexpr uint8_t UV          = 0x0B;  // 1 B bool
constexpr uint8_t SLEEP       = 0x0D;  // 1 B bool
constexpr uint8_t UNIT        = 0x12;  // 1 B, 0=°C, 1=°F
}  // namespace fid

constexpr uint8_t FRAME_START  = 0xAA;
constexpr uint8_t FRAME_END    = 0xEE;
constexpr uint8_t OP_STATE     = 0xA0;  // PIC → module
constexpr uint8_t OP_HEARTBEAT = 0x14;  // module → PIC keepalive

// Field IDs whose value occupies two big-endian bytes. Everything else is 1 byte.
inline bool is_wide_id(uint8_t f) {
  return f == 0x01 || f == fid::PM25 || f == fid::TEMPERATURE ||
         f == 0x06 || f == 0x20;
}

class LR500;

class LR500Switch : public switch_::Switch {
 public:
  LR500Switch(LR500 *parent, uint8_t field) : parent_(parent), field_(field) {}
  void write_state(bool state) override;

 protected:
  LR500 *parent_;
  uint8_t field_;
};

class LR500Fan : public fan::Fan {
 public:
  explicit LR500Fan(LR500 *parent) : parent_(parent) {}
  fan::FanTraits get_traits() override { return fan::FanTraits(false, true, false, 5); }
  void control(const fan::FanCall &call) override;

 protected:
  LR500 *parent_;
};

class LR500 : public Component, public uart::UARTDevice {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void send_field(uint8_t field, uint32_t value, uint8_t width);

  sensor::Sensor *pm25_sensor{nullptr};
  sensor::Sensor *humidity_sensor{nullptr};
  sensor::Sensor *temperature_sensor{nullptr};
  LR500Switch *power_switch{nullptr};
  LR500Switch *uv_switch{nullptr};
  LR500Switch *sleep_switch{nullptr};
  LR500Switch *auto_switch{nullptr};
  LR500Fan *fan_entity{nullptr};

 protected:
  // The longest LR500 frame in either direction is 8 bytes (1+1+3+2+1).
  // RX_BUF_CAP is generous — drops runaway garbage frames started by a stray
  // 0xAA + huge LEN, while still admitting any legitimate frame.
  static constexpr size_t MAX_FRAME = 8;
  static constexpr size_t RX_BUF_CAP = 64;

  void send_heartbeat_();
  void process_byte_(uint8_t b);
  void handle_state_(const uint8_t *body, size_t len);
  bool publish_if_changed_(uint8_t fid, int32_t val);

  uint8_t rx_buf_[RX_BUF_CAP];
  size_t rx_len_{0};
  uint32_t last_hb_ms_{0};

  // Last raw value published per field ID; -1 == never seen. 1 KB, indexed
  // directly by the field byte so any future field ID is automatically covered.
  int32_t last_[256];
};

}  // namespace lr500
}  // namespace esphome
