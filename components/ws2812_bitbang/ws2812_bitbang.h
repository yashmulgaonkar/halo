#pragma once

#ifdef USE_ESP32

#include "esphome/components/light/addressable_light.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"

#include <driver/gpio.h>

namespace esphome::ws2812_bitbang {

enum RGBOrder : uint8_t {
  ORDER_RGB,
  ORDER_RBG,
  ORDER_GRB,
  ORDER_GBR,
  ORDER_BGR,
  ORDER_BRG,
};

// Interrupt-masked GPIO bitbang for WS2812 / SK68XX (NEO_KHZ800).
// Freezes both ESP32-S3 cores and raises INTLEVEL to 15 for the short frame
// so Wi-Fi cannot corrupt the bitstream (stock RMT LED strip flickers here).
class WS2812BitbangLightOutput final : public light::AddressableLight {
 public:
  void setup() override;
  void write_state(light::LightState *state) override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }

  int32_t size() const override { return this->num_leds_; }
  light::LightTraits get_traits() override {
    auto traits = light::LightTraits();
    traits.set_supported_color_modes({light::ColorMode::RGB});
    return traits;
  }

  void set_pin(uint8_t pin) { this->pin_ = pin; }
  void set_num_leds(uint16_t num_leds) { this->num_leds_ = num_leds; }
  void set_rgb_order(RGBOrder rgb_order) { this->rgb_order_ = rgb_order; }
  void set_max_refresh_rate(uint32_t interval_us) { this->max_refresh_rate_ = interval_us; }

  void clear_effect_data() override {
    for (int i = 0; i < this->size(); i++)
      this->effect_data_[i] = 0;
  }

  void dump_config() override;

 protected:
  light::ESPColorView get_view_internal(int32_t index) const override;
  size_t get_buffer_size_() const { return this->num_leds_ * 3; }
  void show_();

  uint8_t *buf_{nullptr};
  uint8_t *effect_data_{nullptr};
  uint8_t pin_{0};
  uint16_t num_leds_{0};
  RGBOrder rgb_order_{ORDER_GRB};
  uint32_t last_refresh_{0};
  optional<uint32_t> max_refresh_rate_{};
};

}  // namespace esphome::ws2812_bitbang

#endif
