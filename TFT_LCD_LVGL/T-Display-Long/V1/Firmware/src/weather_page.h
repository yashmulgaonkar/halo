#pragma once

#include <stdint.h>

#include <lvgl.h>

#include "weather_icon.h"
#include "weather_service.h"

// The weather screen: current conditions in a tinted hero card, three small
// stats, sunrise and sunset, then the next five days with temperature range
// bars. 180 x 640 portrait, built from the same card style as the air page.
class WeatherPage {
 public:
  void build(lv_obj_t* tile);
  // Cheap when nothing changed, so it can be called every second.
  void refresh(uint8_t tempUnit, bool clock24h, const char* ip);

 private:
  struct Row {
    lv_obj_t* box = nullptr;
    lv_obj_t* day = nullptr;
    lv_obj_t* rain = nullptr;
    lv_obj_t* lo = nullptr;
    lv_obj_t* hi = nullptr;
    lv_obj_t* fill = nullptr;
    lv_obj_t* rule = nullptr;
    WxIcon icon;
  };
  static constexpr int kRows = kWxDays - 1;

  void showMessage(const char* line1, const char* line2);
  void showData(const WxData& d, uint8_t unit, bool clock24h);

  lv_obj_t* hero_ = nullptr;
  lv_obj_t* place_ = nullptr;
  lv_obj_t* temp_ = nullptr;
  lv_obj_t* cond_ = nullptr;
  lv_obj_t* hiLo_ = nullptr;
  lv_obj_t* msg_ = nullptr;
  WxIcon icon_;
  lv_obj_t* feels_ = nullptr;
  lv_obj_t* humid_ = nullptr;
  lv_obj_t* wind_ = nullptr;
  lv_obj_t* sunrise_ = nullptr;
  lv_obj_t* sunset_ = nullptr;
  lv_obj_t* forecast_ = nullptr;
  Row rows_[kRows];

  bool built_ = false;
  uint32_t shownVersion_ = 0xFFFFFFFF;
  WxState shownState_ = WxState::Ok;
  bool shownHave_ = false;
  uint8_t shownUnit_ = 255;
  bool shownClock_ = false;
};
