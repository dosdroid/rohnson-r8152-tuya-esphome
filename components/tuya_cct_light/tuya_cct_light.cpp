#include <algorithm>
#include <cmath>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "tuya_cct_light.h"

namespace esphome {
namespace tuya_cct_light {

static const char *const TAG = "tuya_cct_light";

// How long after one of our own writes an incoming datapoint update is
// treated as an echo of that write rather than a real remote-driven change.
static const uint32_t ECHO_COOLDOWN_MS = 1000;

bool TuyaCctLight::in_echo_cooldown_() const {
  return this->last_write_ms_ != 0 && (millis() - this->last_write_ms_) < ECHO_COOLDOWN_MS;
}

void TuyaCctLight::setup() {
  this->parent_->register_listener(this->switch_id_, [this](const tuya::TuyaDatapoint &datapoint) {
    // The MCU report is the ground truth for what was actually applied -
    // resync the dedupe cache even when the state update below is skipped.
    this->last_sent_switch_ = datapoint.value_bool ? 1 : 0;
    if (this->in_echo_cooldown_()) {
      ESP_LOGD(TAG, "Ignoring switch datapoint echo of our own write");
      return;
    }
    if (this->state_->current_values != this->state_->remote_values) {
      ESP_LOGD(TAG, "Light is transitioning, switch datapoint change ignored");
      return;
    }
    auto call = this->state_->make_call();
    call.set_state(datapoint.value_bool);
    call.perform();
  });

  this->parent_->register_listener(this->dimmer_id_, [this](const tuya::TuyaDatapoint &datapoint) {
    this->last_sent_brightness_ = int(datapoint.value_uint);
    if (this->in_echo_cooldown_()) {
      ESP_LOGD(TAG, "Ignoring dimmer datapoint echo of our own write");
      return;
    }
    if (this->state_->current_values != this->state_->remote_values) {
      ESP_LOGD(TAG, "Light is transitioning, dimmer datapoint change ignored");
      return;
    }
    auto call = this->state_->make_call();
    call.set_brightness(float(datapoint.value_uint) / this->max_value_);
    call.perform();
  });

  this->parent_->register_listener(this->cct_id_, [this](const tuya::TuyaDatapoint &datapoint) {
    this->last_sent_cct_ = int(datapoint.value_enum);
    if (this->in_echo_cooldown_()) {
      ESP_LOGD(TAG, "Ignoring cct datapoint echo of our own write");
      return;
    }
    if (this->state_->current_values != this->state_->remote_values) {
      ESP_LOGD(TAG, "Light is transitioning, cct datapoint change ignored");
      return;
    }
    // Raw 0/1/2 on this MCU runs warm->cool, not cool->warm.
    uint8_t enum_value = std::min<uint8_t>(datapoint.value_enum, 2);
    float mireds = this->warm_white_temperature_ +
                   (this->cold_white_temperature_ - this->warm_white_temperature_) * (float(enum_value) / 2.0f);
    auto call = this->state_->make_call();
    call.set_color_temperature(mireds);
    call.perform();
  });
}

void TuyaCctLight::dump_config() {
  ESP_LOGCONFIG(TAG, "Tuya CCT Light:");
  ESP_LOGCONFIG(TAG, "  Switch datapoint: %u", this->switch_id_);
  ESP_LOGCONFIG(TAG, "  Dimmer datapoint: %u (range %u-%u)", this->dimmer_id_, this->min_value_, this->max_value_);
  ESP_LOGCONFIG(TAG, "  CCT datapoint: %u (enum 0=cold,1=neutral,2=warm)", this->cct_id_);
}

light::LightTraits TuyaCctLight::get_traits() {
  auto traits = light::LightTraits();
  traits.set_supported_color_modes({light::ColorMode::COLOR_TEMPERATURE});
  traits.set_min_mireds(this->cold_white_temperature_);
  traits.set_max_mireds(this->warm_white_temperature_);
  return traits;
}

void TuyaCctLight::setup_state(light::LightState *state) { this->state_ = state; }

void TuyaCctLight::write_state(light::LightState *state) {
  // During a transition (e.g. ControllerX hold-to-dim sends turn_on with a
  // transition time) ESPHome calls write_state on every loop tick with
  // intermediate values. The Tuya MCU can't absorb frames at that rate - it
  // rejects them (5 retries + beep each). Skip the intermediate frames; when
  // the transformer finishes, LightState clears the flag and calls
  // write_state once more with the final target values, so the end state
  // always gets sent.
  if (state->is_transformer_active())
    return;

  if (!state->current_values.is_on()) {
    if (this->last_sent_switch_ != 0) {
      this->parent_->set_boolean_datapoint_value(this->switch_id_, false);
      this->last_sent_switch_ = 0;
      this->last_write_ms_ = millis();
    }
    return;
  }

  float color_temperature, brightness;
  state->current_values_as_ct(&color_temperature, &brightness);

  // The dimmer datapoint has a hardware floor of min_value_ - below that the
  // MCU doesn't actually dim any further. Snap HA's displayed brightness up
  // to that floor too, so the UI doesn't show a value the hardware can't
  // reach (and so it doesn't get stuck showing a too-low value if the
  // clamped datapoint write below is skipped as "unchanged").
  float min_brightness = float(this->min_value_) / float(this->max_value_);
  if (brightness < min_brightness) {
    brightness = min_brightness;
    state->current_values.set_brightness(brightness);
    state->remote_values.set_brightness(brightness);
    state->publish_state();
  }

  // color_temperature is 0.0 (cold/min_mireds) .. 1.0 (warm/max_mireds), but raw
  // 0/1/2 on this MCU runs warm->cool, so invert before snapping to the nearest step.
  uint8_t cct_value = static_cast<uint8_t>(std::min(2.0f, roundf((1.0f - color_temperature) * 2.0f)));
  if (int(cct_value) != this->last_sent_cct_) {
    this->parent_->set_enum_datapoint_value(this->cct_id_, cct_value);
    this->last_sent_cct_ = int(cct_value);
    this->last_write_ms_ = millis();
  }

  auto brightness_int = static_cast<uint32_t>(brightness * this->max_value_);
  brightness_int = std::max(brightness_int, this->min_value_);
  if (int(brightness_int) != this->last_sent_brightness_) {
    this->parent_->set_integer_datapoint_value(this->dimmer_id_, brightness_int);
    this->last_sent_brightness_ = int(brightness_int);
    this->last_write_ms_ = millis();
  }

  if (this->last_sent_switch_ != 1) {
    this->parent_->set_boolean_datapoint_value(this->switch_id_, true);
    this->last_sent_switch_ = 1;
    this->last_write_ms_ = millis();
  }
}

}  // namespace tuya_cct_light
}  // namespace esphome
