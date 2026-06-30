#include <algorithm>
#include <cmath>

#include "esphome/core/log.h"
#include "tuya_cct_light.h"

namespace esphome {
namespace tuya_cct_light {

static const char *const TAG = "tuya_cct_light";

void TuyaCctLight::setup() {
  this->parent_->register_listener(this->switch_id_, [this](const tuya::TuyaDatapoint &datapoint) {
    if (this->state_->current_values != this->state_->remote_values) {
      ESP_LOGD(TAG, "Light is transitioning, switch datapoint change ignored");
      return;
    }
    auto call = this->state_->make_call();
    call.set_state(datapoint.value_bool);
    call.perform();
  });

  this->parent_->register_listener(this->dimmer_id_, [this](const tuya::TuyaDatapoint &datapoint) {
    if (this->state_->current_values != this->state_->remote_values) {
      ESP_LOGD(TAG, "Light is transitioning, dimmer datapoint change ignored");
      return;
    }
    auto call = this->state_->make_call();
    call.set_brightness(float(datapoint.value_uint) / this->max_value_);
    call.perform();
  });

  this->parent_->register_listener(this->cct_id_, [this](const tuya::TuyaDatapoint &datapoint) {
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
  if (!state->current_values.is_on()) {
    this->parent_->set_boolean_datapoint_value(this->switch_id_, false);
    return;
  }

  float color_temperature, brightness;
  state->current_values_as_ct(&color_temperature, &brightness);

  // color_temperature is 0.0 (cold/min_mireds) .. 1.0 (warm/max_mireds), but raw
  // 0/1/2 on this MCU runs warm->cool, so invert before snapping to the nearest step.
  uint8_t cct_value = static_cast<uint8_t>(std::min(2.0f, roundf((1.0f - color_temperature) * 2.0f)));
  this->parent_->set_enum_datapoint_value(this->cct_id_, cct_value);

  auto brightness_int = static_cast<uint32_t>(brightness * this->max_value_);
  brightness_int = std::max(brightness_int, this->min_value_);
  this->parent_->set_integer_datapoint_value(this->dimmer_id_, brightness_int);

  this->parent_->set_boolean_datapoint_value(this->switch_id_, true);
}

}  // namespace tuya_cct_light
}  // namespace esphome
