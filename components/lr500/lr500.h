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
constexpr uint8_t FILTER_HOURS = 0x06; // 2 B, filter life remaining (hours)
constexpr uint8_t POWER       = 0x08;  // 1 B bool
constexpr uint8_t FAN_SPEED   = 0x09;  // 1 B, 1..5
constexpr uint8_t AUTO_MODE   = 0x0A;  // 1 B bool — any non-zero write enters auto
constexpr uint8_t UV          = 0x0B;  // 1 B bool
constexpr uint8_t SLEEP       = 0x0D;  // 1 B bool
constexpr uint8_t BUZZER      = 0x0E;  // 1 B — only value 1 enables key tones, anything else silences
constexpr uint8_t UNIT        = 0x12;  // 1 B, 0=°C, 1=°F
}  // namespace fid

constexpr uint8_t FRAME_START  = 0xAA;
constexpr uint8_t FRAME_END    = 0xEE;
constexpr uint8_t OP_STATE     = 0xA0;  // PIC → module
constexpr uint8_t OP_HEARTBEAT = 0x14;  // module → PIC keepalive

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

// Toggling this ON tells the LR500 component to stop transmitting on the
// shared UART line. Used when an external probe (e.g. a laptop on the same
// PIC-RX line) needs the bus to itself. RX parsing keeps running.
class LR500MuteSwitch : public switch_::Switch {
 public:
  explicit LR500MuteSwitch(LR500 *parent) : parent_(parent) {}
  void write_state(bool state) override;

 protected:
  LR500 *parent_;
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

  // When muted, no heartbeats are sent and writes from HA are dropped. RX
  // continues so sensors keep updating from whatever the PIC reports. The
  // mute switch entity (if bound) is republished whenever this flag flips,
  // so anyone calling this programmatically stays in sync with HA.
  void set_tx_muted(bool m);
  bool is_tx_muted() const { return tx_muted_; }

  // Setters used by codegen (sensor.py / switch.py / fan.py).
  void set_pm25_sensor(sensor::Sensor *s) { pm25_sensor_ = s; }
  void set_temperature_sensor(sensor::Sensor *s) { temperature_sensor_ = s; }
  void set_humidity_sensor(sensor::Sensor *s) { humidity_sensor_ = s; }
  void set_filter_hours_sensor(sensor::Sensor *s) { filter_hours_sensor_ = s; }
  void set_power_switch(LR500Switch *s) { power_switch_ = s; }
  void set_uv_switch(LR500Switch *s) { uv_switch_ = s; }
  void set_sleep_switch(LR500Switch *s) { sleep_switch_ = s; }
  void set_auto_switch(LR500Switch *s) { auto_switch_ = s; }
  void set_buzzer_switch(LR500Switch *s) { buzzer_switch_ = s; }
  void set_unit_switch(LR500Switch *s) { unit_switch_ = s; }
  void set_mute_switch(LR500MuteSwitch *s) { mute_switch_ = s; }
  void set_fan(LR500Fan *f) { fan_entity_ = f; }

 protected:
  static constexpr size_t MAX_FRAME = 8;
  static constexpr size_t RX_BUF_CAP = 64;

  void send_heartbeat_();
  void process_byte_(uint8_t b);
  void handle_state_(const uint8_t *body, size_t len);

  uint8_t rx_buf_[RX_BUF_CAP];
  size_t rx_len_{0};
  uint32_t last_hb_ms_{0};
  bool tx_muted_{false};

  // Last seen POWER value: -1 = never observed, 0 = off, 1 = on. Used to
  // gate fan_entity_ state so a FAN_SPEED echo while POWER=0 doesn't make
  // the fan entity report ON.
  int last_power_{-1};

  // All entities are optional — each is bound only if the user declared a
  // platform entry for it in YAML.
  sensor::Sensor *pm25_sensor_{nullptr};
  sensor::Sensor *temperature_sensor_{nullptr};
  sensor::Sensor *humidity_sensor_{nullptr};
  sensor::Sensor *filter_hours_sensor_{nullptr};
  LR500Switch *power_switch_{nullptr};
  LR500Switch *uv_switch_{nullptr};
  LR500Switch *sleep_switch_{nullptr};
  LR500Switch *auto_switch_{nullptr};
  LR500Switch *buzzer_switch_{nullptr};
  LR500Switch *unit_switch_{nullptr};
  LR500MuteSwitch *mute_switch_{nullptr};
  LR500Fan *fan_entity_{nullptr};
};

}  // namespace lr500
}  // namespace esphome
