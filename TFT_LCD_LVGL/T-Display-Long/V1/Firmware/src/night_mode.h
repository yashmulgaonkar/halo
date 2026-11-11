#pragma once

#include <stdint.h>
#include <time.h>

// Auto Night Mode: between sunset and sunrise the ring drops to 5% and the screen to 15%.
// The daytime levels are restored at sunrise unless they were changed during the night.
class NightMode {
 public:
  void loop();
  // Re-evaluate on the next loop (after the setting or location changes).
  void refresh() { lastCheckMs_ = 0; }
  bool active() const { return active_; }
  // Location used for the sun times: the saved lat/lon, or an estimate from the timezone.
  void location(float& lat, float& lon, bool& estimated) const;
  // Next sunset and sunrise as Unix times; false until the clock is set (or in polar day/night).
  bool nextTimes(time_t& sunset, time_t& sunrise) const;

  static constexpr uint8_t kRingLevel = 13;       // 5% of 255
  static constexpr uint8_t kBacklightLevel = 38;  // 15% of 255

 private:
  void enter();
  void leave();

  bool active_ = false;
  uint8_t dayRing_ = 0;
  uint8_t dayBacklight_ = 0;
  uint32_t lastCheckMs_ = 0;
};

NightMode& nightMode();
