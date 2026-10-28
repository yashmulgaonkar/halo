#pragma once

#include <stdint.h>

#include <lvgl.h>

#include "readings.h"

struct UiStatus {
  const Readings* readings;
  bool wifi;
  bool mqtt;
  bool mqttConfigured;
  bool clock24h;
  uint8_t tempUnit;
  const char* ip;
  uint32_t uptimeSec;
};

class HaloUi {
 public:
  void begin();
  void loop(const UiStatus& status);
  void nextPage();
  void prevPage();

 private:
  void buildAir(lv_obj_t* tile);
  void buildGases(lv_obj_t* tile);
  void buildDevice(lv_obj_t* tile);
  void refresh(const UiStatus& status);
  void refreshClock(const UiStatus& status);
  void updateDots();
  void showPage(int page);

  lv_obj_t* tv_ = nullptr;
  lv_obj_t* tiles_[3] = {};
  lv_obj_t* dots_[3] = {};
  int page_ = 0;
  uint32_t lastClockMs_ = 0;
  uint32_t lastReadingMs_ = 0;
  uint32_t lastAutoMs_ = 0;
  bool lastWifi_ = false;
  bool lastMqtt_ = false;
  uint8_t lastUnit_ = 255;

  lv_obj_t* timeLabel_ = nullptr;
  lv_obj_t* wifiIcon_ = nullptr;
  lv_obj_t* mqttIcon_ = nullptr;
  lv_obj_t* aqiValue_ = nullptr;
  lv_obj_t* aqiCat_ = nullptr;
  lv_obj_t* aqiRail_ = nullptr;
  lv_obj_t* tempValue_ = nullptr;
  lv_obj_t* tempUnit_ = nullptr;
  lv_obj_t* humValue_ = nullptr;
  lv_obj_t* co2Value_ = nullptr;
  lv_obj_t* pmValue_[4] = {};
  lv_obj_t* vocValue_ = nullptr;
  lv_obj_t* vocWord_ = nullptr;
  lv_obj_t* gasValue_[8] = {};
  lv_obj_t* gasPip_[8] = {};
  lv_obj_t* devBody_ = nullptr;
};

HaloUi& ui();
