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
  void buildMenu(lv_obj_t* tile, int slot);
  void buildAir(lv_obj_t* tile);
  void buildGases(lv_obj_t* tile);
  void buildDevice(lv_obj_t* tile);
  void refresh(const UiStatus& status);
  void refreshClock(const UiStatus& status);
  void updateDots();
  void showPage(int page);
  void swapDataPages();
  void openBias(int id);
  void closeBias();
  void stepBias(int dir);
  void showBiasValue();
  void watchTap(lv_obj_t* obj, int id);
  static void onTileTap(lv_event_t* e);
  static void onBiasStep(lv_event_t* e);
  static void onBiasClose(lv_event_t* e);

  lv_obj_t* tv_ = nullptr;
  lv_obj_t* tiles_[3] = {};
  lv_obj_t* dots_[3] = {};
  int page_ = 0;
  uint32_t lastClockMs_ = 0;
  uint32_t lastReadingMs_ = 0;
  uint32_t lastAutoMs_ = 0;
  bool heldMain_ = false;
  bool lastWifi_ = false;
  bool lastMqtt_ = false;
  uint8_t lastUnit_ = 255;

  lv_obj_t* timeLabel_[2] = {};
  lv_obj_t* wifiIcon_[2] = {};
  lv_obj_t* mqttIcon_[2] = {};
  lv_obj_t* aqiValue_ = nullptr;
  lv_obj_t* aqiCat_ = nullptr;
  lv_obj_t* aqiRail_ = nullptr;
  lv_obj_t* tempRail_ = nullptr;
  lv_obj_t* humRail_ = nullptr;
  lv_obj_t* presRail_ = nullptr;
  lv_obj_t* co2Rail_ = nullptr;
  lv_obj_t* pmRail_[4] = {};
  lv_obj_t* vocRail_ = nullptr;
  lv_obj_t* tempValue_ = nullptr;
  lv_obj_t* tempUnit_ = nullptr;
  lv_obj_t* humValue_ = nullptr;
  lv_obj_t* pressureValue_ = nullptr;
  lv_obj_t* co2Value_ = nullptr;
  lv_obj_t* pmValue_[4] = {};
  lv_obj_t* vocValue_ = nullptr;
  lv_obj_t* vocWord_ = nullptr;
  lv_obj_t* gasValue_[7] = {};
  lv_obj_t* gasPip_[7] = {};
  lv_obj_t* devBody_ = nullptr;
  lv_obj_t* biasShade_ = nullptr;
  lv_obj_t* biasTitle_ = nullptr;
  lv_obj_t* biasValue_ = nullptr;
  int biasEdit_ = -1;
};

HaloUi& ui();
