#pragma once

#include <stdint.h>

class Backlight {
 public:
  void begin();
  void setLevel(uint8_t level);
  // Rewrites the PWM duty. reattach recovers the pin if LEDC dropped it.
  void refresh(bool reattach = false);
  uint8_t level() const { return level_; }
  bool power() const { return level_ > 0; }

 private:
  uint8_t level_ = 255;
  bool ready_ = false;
};

Backlight& backlight();
