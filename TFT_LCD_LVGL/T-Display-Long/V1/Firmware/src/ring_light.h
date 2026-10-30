#pragma once

#include <stdint.h>

enum class RingEffect : uint8_t { None = 0, Pulse, Rainbow, Scan, Twinkle, ColorWipe };

class RingLight {
 public:
  void begin();
  void loop(bool startupBlink, bool mqttConnected, bool wifiUp);
  void markStage(uint8_t stage, uint32_t elapsedUs);
  void suspend(bool suspended);

  void setPower(bool on);
  void setBrightness(uint8_t brightness);
  void setColor(uint8_t r, uint8_t g, uint8_t b);
  void showAir(uint8_t r, uint8_t g, uint8_t b);
  void setEffect(const char* name);
  bool consumePureBlueAsWhite();

  bool power() const { return power_; }
  uint8_t brightness() const { return brightness_; }
  uint32_t frames() const { return frames_; }
  uint32_t lateFrames() const { return late_; }
  uint32_t maxGapMs() const { return maxGapMs_; }
  uint32_t lastGapMs() const { return lastGapMs_; }
  uint32_t maxFreezeUs() const { return maxFreezeUs_; }
  uint32_t freezeFails() const { return freezeFail_; }
  uint8_t lateStage() const { return lateStage_; }
  uint8_t core() const { return core_; }
  uint32_t rmtHz() const { return rmtHz_; }
  uint32_t stageMaxUs(uint8_t stage) const { return stage < 4 ? stageMaxUs_[stage] : 0; }
  uint8_t red() const { return red_; }
  uint8_t green() const { return green_; }
  uint8_t blue() const { return blue_; }
  const char* effectName() const;
  bool takeDirty();

 private:
  static void workerTrampoline(void* arg);
  void worker();
  void pump();
  void render(uint32_t now, bool startupRainbow);
  void setPixel(int index, uint8_t r, uint8_t g, uint8_t b, uint8_t scale);
  bool show();
  bool commit();

  uint8_t buffer_[11 * 3] = {};
  uint8_t shown_[11 * 3] = {};
  bool power_ = false;
  uint8_t brightness_ = 255;
  uint8_t red_ = 255;
  uint8_t green_ = 255;
  uint8_t blue_ = 255;
  RingEffect effect_ = RingEffect::None;
  bool startupFinished_ = false;
  bool startupBlink_ = false;
  bool mqttSeen_ = false;
  bool wifiUp_ = false;
  bool primed_ = false;
  bool holdOff_ = false;
  bool suspended_ = false;
  bool cleared_ = true;
  bool dirty_ = true;
  uint32_t phase_ = 0;
  uint32_t lastShowUs_ = 0;
  uint32_t frames_ = 0;
  uint32_t late_ = 0;
  uint32_t maxGapMs_ = 0;
  uint32_t lastGapMs_ = 0;
  uint32_t maxFreezeUs_ = 0;
  uint32_t freezeFail_ = 0;
  uint32_t stageMaxUs_[4] = {};
  uint8_t lateStage_ = 0;
  uint8_t core_ = 0;
  uint32_t rmtHz_ = 0;
  volatile uint8_t stage_ = 0;
  uint32_t t0h_ = 96;
  uint32_t t1h_ = 192;
  uint32_t period_ = 300;
  uint8_t twinkle_[11] = {};
};

RingLight& ring();
