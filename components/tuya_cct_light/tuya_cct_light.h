#pragma once

#include "esphome/core/component.h"
#include "esphome/components/tuya/tuya.h"
#include "esphome/components/light/light_output.h"

namespace esphome {
namespace tuya_cct_light {

// Drives one Tuya MCU light that exposes brightness (value-type datapoint),
// on/off (bool-type datapoint) and a 3-position color temperature
// (enum-type datapoint, raw values 0=cold, 1=neutral, 2=warm) as a single
// Home Assistant light entity with brightness + color_temp support.
class TuyaCctLight : public Component, public light::LightOutput {
 public:
  void setup() override;
  void dump_config() override;

  void set_tuya_parent(tuya::Tuya *parent) { this->parent_ = parent; }
  void set_switch_id(uint8_t switch_id) { this->switch_id_ = switch_id; }
  void set_dimmer_id(uint8_t dimmer_id) { this->dimmer_id_ = dimmer_id; }
  void set_cct_id(uint8_t cct_id) { this->cct_id_ = cct_id; }
  void set_min_value(uint32_t min_value) { this->min_value_ = min_value; }
  void set_max_value(uint32_t max_value) { this->max_value_ = max_value; }
  void set_cold_white_temperature(float cold_white_temperature) {
    this->cold_white_temperature_ = cold_white_temperature;
  }
  void set_warm_white_temperature(float warm_white_temperature) {
    this->warm_white_temperature_ = warm_white_temperature;
  }

  light::LightTraits get_traits() override;
  void setup_state(light::LightState *state) override;
  void write_state(light::LightState *state) override;

 protected:
  // True while we recently wrote to the MCU - incoming datapoint updates in
  // this window are just echoes of our own writes and must not be pushed
  // back into the light state (a stale echo would fight rapid dimming from
  // e.g. ControllerX remotes, making brightness bounce back and forth).
  bool in_echo_cooldown_() const;

  uint32_t last_write_ms_{0};
  // Last raw values actually sent to the MCU, so unchanged values are never
  // retransmitted (each redundant frame is a chance for the MCU to beep).
  int last_sent_switch_{-1};
  int last_sent_brightness_{-1};
  int last_sent_cct_{-1};

  tuya::Tuya *parent_{nullptr};
  uint8_t switch_id_{0};
  uint8_t dimmer_id_{0};
  uint8_t cct_id_{0};
  uint32_t min_value_{10};
  uint32_t max_value_{100};
  float cold_white_temperature_{153};
  float warm_white_temperature_{370};
  light::LightState *state_{nullptr};
};

}  // namespace tuya_cct_light
}  // namespace esphome
