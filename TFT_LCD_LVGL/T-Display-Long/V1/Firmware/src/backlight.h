#pragma once

#include <stdint.h>

class Backlight {
 public:
  void begin();
  void setLevel(uint8_t level);
  uint8_t level() const { return level_; }
  bool power() const { return level_ > 0; }

 private:
  uint8_t level_ = 255;
  bool ready_ = false;
};

Backlight& backlight();
