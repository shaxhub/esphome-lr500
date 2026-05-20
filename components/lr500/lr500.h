#pragma once
#include <set>
#include <vector>
#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/fan/fan.h"

namespace esphome {
namespace lr500 {

class LR500;

// One generic switch per writable boolean field on the device.
class LR500Switch : public switch_::Switch {
 public:
  LR500Switch(LR500 *parent, uint8_t field) : parent_(parent), field_(field) {}
  void write_state(bool state) override;

 protected:
  LR500 *parent_;
  uint8_t field_;
};

// Fan exposes 5 speeds (1..4 + turbo) and maps off => power off.
class LR500Fan : public fan::Fan {
 public:
  explicit LR500Fan(LR500 *parent) : parent_(parent) {}
  fan::FanTraits get_traits() override {
    return fan::FanTraits(/*oscillation=*/false, /*speed=*/true,
                          /*direction=*/false, /*speed_count=*/5);
  }
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

  // Public so the switch + fan helpers can call it.
  void send_field(uint8_t field, uint32_t value, uint8_t width);

  // Entities — populated in setup(), updated from state-report parser.
  sensor::Sensor *pm25_sensor{nullptr};
  sensor::Sensor *humidity_sensor{nullptr};
  sensor::Sensor *temperature_sensor{nullptr};
  LR500Switch *power_switch{nullptr};
  LR500Switch *uv_switch{nullptr};
  LR500Switch *sleep_switch{nullptr};
  LR500Switch *auto_switch{nullptr};
  LR500Fan *fan_entity{nullptr};

 protected:
  void send_heartbeat_();
  void process_byte_(uint8_t b);
  void handle_state_(const uint8_t *body, size_t len);

  std::vector<uint8_t> rx_buf_;
  uint32_t last_hb_ms_{0};

  // Cached last-published values so we don't spam HA every second.
  int last_[16] = {[0 ... 15] = -1};
};

}  // namespace lr500
}  // namespace esphome
