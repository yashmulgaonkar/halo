#include "backlight.h"

#include <Arduino.h>

#include "board_pins.h"

namespace {
Backlight g_backlight;
constexpr int kChannel = 0;
}

Backlight& backlight() { return g_backlight; }

void Backlight::begin() {
  // 100 Hz at 8-bit does not fit the ESP32-S3 LEDC divider. 5 kHz does.
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ready_ = ledcAttach(PIN_LCD_BL, 5000, 8);
#else
  ready_ = ledcSetup(kChannel, 5000, 8) != 0;
  if (ready_) ledcAttachPin(PIN_LCD_BL, kChannel);
#endif
  if (ready_) setLevel(255);
}

void Backlight::setLevel(uint8_t level) {
  level_ = level;
  if (!ready_) return;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(PIN_LCD_BL, level_);
#else
  ledcWrite(kChannel, level_);
#endif
}
