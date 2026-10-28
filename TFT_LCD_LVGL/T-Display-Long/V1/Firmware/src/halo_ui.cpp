#include "halo_ui.h"
#include "status_icons.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "aqi.h"
#include "board_pins.h"
#include "halo_version.h"
#include "settings.h"
#include "touch_in.h"

namespace {
HaloUi g_ui;

constexpr uint32_t kCard = 0x141820;
constexpr uint32_t kText = 0xF4F7FB;
constexpr uint32_t kMuted = 0x93A0B5;
constexpr uint32_t kGood = 0x3DDC97;
constexpr uint32_t kMod = 0xE6C85C;
constexpr uint32_t kUsg = 0xF0A04B;
constexpr uint32_t kBad = 0xF07167;
constexpr uint32_t kWorse = 0xC084FC;
constexpr uint32_t kWorst = 0xC43B4E;

lv_color_t hex(uint32_t c) { return lv_color_hex(c); }

void styleTile(lv_obj_t* tile) {
  lv_obj_set_style_bg_color(tile, hex(0x000000), 0);
  lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(tile, 0, 0);
  lv_obj_set_style_pad_left(tile, 8, 0);
  lv_obj_set_style_pad_right(tile, 8, 0);
  lv_obj_set_style_pad_top(tile, 8, 0);
  lv_obj_set_style_pad_bottom(tile, 22, 0);
  lv_obj_set_style_pad_row(tile, 6, 0);
  lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t* card(lv_obj_t* parent, lv_coord_t height) {
  lv_obj_t* obj = lv_obj_create(parent);
  lv_obj_set_width(obj, lv_pct(100));
  lv_obj_set_height(obj, height);
  lv_obj_set_style_bg_color(obj, hex(kCard), 0);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(obj, 0, 0);
  lv_obj_set_style_radius(obj, 16, 0);
  lv_obj_set_style_pad_all(obj, 8, 0);
  lv_obj_set_style_shadow_width(obj, 0, 0);
  lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
  return obj;
}

lv_obj_t* text(lv_obj_t* parent, const char* value, const lv_font_t* font, uint32_t color) {
  lv_obj_t* label = lv_label_create(parent);
  lv_label_set_text(label, value);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_color(label, hex(color), 0);
  return label;
}

lv_color_t aqiColor(int aqi, bool evacuate) {
  if (evacuate || aqi > 300) return hex(kWorst);
  if (aqi <= 50) return hex(kGood);
  if (aqi <= 100) return hex(kMod);
  if (aqi <= 150) return hex(kUsg);
  if (aqi <= 200) return hex(kBad);
  return hex(kWorse);
}

lv_color_t upperBand(float v, float green, float yellow, float orange) {
  if (v <= green) return hex(kGood);
  if (v <= yellow) return hex(kMod);
  if (orange >= 0 && v <= orange) return hex(kUsg);
  return hex(kBad);
}

lv_color_t tempColor(float c) {
  if (c >= 18 && c <= 24) return hex(kGood);
  if ((c > 24 && c <= 27) || (c >= 16 && c < 18)) return hex(kMod);
  if ((c > 27 && c <= 30) || (c >= 10 && c < 16)) return hex(kUsg);
  return hex(kBad);
}

lv_color_t humColor(float h) {
  if (h >= 30 && h <= 50) return hex(kGood);
  if ((h > 50 && h <= 60) || (h >= 20 && h < 30)) return hex(kMod);
  if ((h > 60 && h <= 70) || (h >= 10 && h < 20)) return hex(kUsg);
  return hex(kBad);
}

float shownTemp(float celsius, uint8_t unit) {
  if (unit == TEMP_C) return celsius;
  if (unit == TEMP_K) return celsius + 273.15f;
  return celsius * 9.0f / 5.0f + 32.0f;
}

const char* unitSuffix(uint8_t unit) {
  if (unit == TEMP_C) return "C";
  if (unit == TEMP_K) return "K";
  return "F";
}
}  // namespace

HaloUi& ui() { return g_ui; }

void HaloUi::updateDots() {
  for (int i = 0; i < 3; i++) {
    if (dots_[i] == nullptr) continue;
    lv_obj_set_style_bg_color(dots_[i], hex(i == page_ ? kText : 0x2C3544), 0);
  }
}

void HaloUi::showPage(int page) {
  if (page < 0) page = 2;
  if (page > 2) page = 0;
  page_ = page;
  // Snap. A sliding animation redraws the whole panel on every frame.
  if (tv_) lv_obj_set_tile_id(tv_, 0, page_, LV_ANIM_OFF);
  lastAutoMs_ = millis();
  updateDots();
}

void HaloUi::nextPage() { showPage(page_ + 1); }

void HaloUi::prevPage() { showPage(page_ - 1); }

void HaloUi::begin() {
  lv_obj_t* screen = lv_scr_act();
  lv_obj_set_style_bg_color(screen, hex(0x000000), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

  tv_ = lv_tileview_create(screen);
  lv_obj_set_size(tv_, HALO_LCD_W, HALO_LCD_H);
  lv_obj_align(tv_, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_bg_color(tv_, hex(0x000000), 0);
  lv_obj_set_style_border_width(tv_, 0, 0);
  lv_obj_set_style_pad_all(tv_, 0, 0);
  lv_obj_set_scrollbar_mode(tv_, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_event_cb(tv_, [](lv_event_t* e) {
    HaloUi* self = static_cast<HaloUi*>(lv_event_get_user_data(e));
    lv_obj_t* active = lv_tileview_get_tile_act(self->tv_);
    for (int i = 0; i < 3; i++) {
      if (self->tiles_[i] == active) self->page_ = i;
    }
    self->updateDots();
    self->lastAutoMs_ = millis();
  }, LV_EVENT_VALUE_CHANGED, this);

  // Finger drags are handled as a completed swipe. Live scrolling fights the
  // full-screen refresh, so the tiles do not scroll under the finger.
  tiles_[0] = lv_tileview_add_tile(tv_, 0, 0, LV_DIR_NONE);
  tiles_[1] = lv_tileview_add_tile(tv_, 0, 1, LV_DIR_NONE);
  tiles_[2] = lv_tileview_add_tile(tv_, 0, 2, LV_DIR_NONE);
  for (int i = 0; i < 3; i++) styleTile(tiles_[i]);

  buildAir(tiles_[0]);
  buildGases(tiles_[1]);
  buildDevice(tiles_[2]);

  lv_obj_t* dock = lv_obj_create(lv_layer_top());
  lv_obj_set_size(dock, 52, 14);
  lv_obj_align(dock, LV_ALIGN_BOTTOM_MID, 0, -4);
  lv_obj_set_style_bg_opa(dock, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(dock, 0, 0);
  lv_obj_set_style_pad_all(dock, 0, 0);
  lv_obj_set_flex_flow(dock, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(dock, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(dock, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  for (int i = 0; i < 3; i++) {
    dots_[i] = lv_obj_create(dock);
    lv_obj_set_size(dots_[i], 7, 7);
    lv_obj_set_style_radius(dots_[i], LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(dots_[i], 0, 0);
    lv_obj_clear_flag(dots_[i], LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  }
  updateDots();
  lastAutoMs_ = millis();
}

void HaloUi::buildAir(lv_obj_t* tile) {
  lv_obj_t* header = lv_obj_create(tile);
  lv_obj_set_width(header, lv_pct(100));
  lv_obj_set_height(header, 34);
  lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(header, 0, 0);
  lv_obj_set_style_pad_all(header, 0, 0);
  lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* brand = lv_obj_create(header);
  lv_obj_set_height(brand, 34);
  lv_obj_set_width(brand, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(brand, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(brand, 0, 0);
  lv_obj_set_style_pad_all(brand, 0, 0);
  lv_obj_set_flex_flow(brand, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(brand, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(brand, 4, 0);
  lv_obj_clear_flag(brand, LV_OBJ_FLAG_SCROLLABLE);
  wifiIcon_ = lv_img_create(brand);
  lv_img_set_src(wifiIcon_, &kWifiIcon);
  lv_obj_set_style_img_recolor_opa(wifiIcon_, LV_OPA_COVER, 0);
  lv_obj_set_style_img_recolor(wifiIcon_, hex(kBad), 0);
  lv_obj_clear_flag(wifiIcon_, LV_OBJ_FLAG_CLICKABLE);
  mqttIcon_ = lv_img_create(brand);
  lv_img_set_src(mqttIcon_, &kMqttOffIcon);
  lv_obj_clear_flag(mqttIcon_, LV_OBJ_FLAG_CLICKABLE);
  timeLabel_ = text(header, "--:--", &lv_font_montserrat_18, kText);

  lv_obj_t* aqi = card(tile, 116);
  aqiRail_ = lv_obj_create(aqi);
  lv_obj_set_size(aqiRail_, 6, lv_pct(78));
  lv_obj_add_flag(aqiRail_, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_align(aqiRail_, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_set_style_radius(aqiRail_, 4, 0);
  lv_obj_set_style_border_width(aqiRail_, 0, 0);
  lv_obj_set_style_bg_color(aqiRail_, hex(kMuted), 0);
  lv_obj_set_flex_flow(aqi, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(aqi, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(aqi, 0, 0);
  text(aqi, "AIR QUALITY", &lv_font_montserrat_12, kMuted);
  aqiValue_ = text(aqi, "--", &lv_font_montserrat_48, kText);
  aqiCat_ = text(aqi, "Waiting for air", &lv_font_montserrat_14, kMuted);

  lv_obj_t* climate = lv_obj_create(tile);
  lv_obj_set_width(climate, lv_pct(100));
  lv_obj_set_height(climate, 74);
  lv_obj_set_style_bg_opa(climate, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(climate, 0, 0);
  lv_obj_set_style_pad_all(climate, 0, 0);
  lv_obj_set_style_pad_column(climate, 6, 0);
  lv_obj_set_flex_flow(climate, LV_FLEX_FLOW_ROW);
  lv_obj_clear_flag(climate, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* temp = card(climate, 74);
  lv_obj_set_width(temp, 78);
  lv_obj_set_flex_flow(temp, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(temp, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  text(temp, "TEMP", &lv_font_montserrat_12, kMuted);
  tempValue_ = text(temp, "--", &lv_font_montserrat_28, kText);
  tempUnit_ = text(temp, "F", &lv_font_montserrat_12, kMuted);

  lv_obj_t* hum = card(climate, 74);
  lv_obj_set_width(hum, 78);
  lv_obj_set_flex_flow(hum, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(hum, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  text(hum, "HUMIDITY", &lv_font_montserrat_12, kMuted);
  humValue_ = text(hum, "--", &lv_font_montserrat_28, kText);
  text(hum, "%", &lv_font_montserrat_12, kMuted);

  lv_obj_t* co2 = card(tile, 62);
  lv_obj_set_flex_flow(co2, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(co2, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  text(co2, "CO2", &lv_font_montserrat_14, kMuted);
  co2Value_ = text(co2, "--", &lv_font_montserrat_28, kText);
  text(co2, "ppm", &lv_font_montserrat_12, kMuted);

  const char* pmNames[] = {"PM1", "PM2.5", "PM4", "PM10"};
  for (int i = 0; i < 4; i++) {
    lv_obj_t* row = card(tile, 55);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(row, 12, 0);
    lv_obj_set_style_pad_right(row, 12, 0);
    text(row, pmNames[i], &lv_font_montserrat_14, kMuted);
    pmValue_[i] = text(row, "--", &lv_font_montserrat_28, kText);
    text(row, "ug/m3", &lv_font_montserrat_12, kMuted);
  }

  lv_obj_t* voc = card(tile, 58);
  lv_obj_set_flex_flow(voc, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(voc, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(voc, 0, 0);
  text(voc, "VOC INDEX", &lv_font_montserrat_12, kMuted);
  lv_obj_t* vocRow = lv_obj_create(voc);
  lv_obj_set_width(vocRow, lv_pct(100));
  lv_obj_set_height(vocRow, 28);
  lv_obj_set_style_bg_opa(vocRow, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(vocRow, 0, 0);
  lv_obj_set_style_pad_all(vocRow, 0, 0);
  lv_obj_set_flex_flow(vocRow, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(vocRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(vocRow, LV_OBJ_FLAG_SCROLLABLE);
  vocValue_ = text(vocRow, "--", &lv_font_montserrat_28, kText);
  vocWord_ = text(vocRow, "--", &lv_font_montserrat_14, kMuted);
}

void HaloUi::buildGases(lv_obj_t* tile) {
  lv_obj_t* header = lv_obj_create(tile);
  lv_obj_set_width(header, lv_pct(100));
  lv_obj_set_height(header, 28);
  lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(header, 0, 0);
  lv_obj_set_style_pad_all(header, 0, 0);
  lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
  text(header, "GASES", &lv_font_montserrat_14, kMuted);

  const char* names[] = {"CO", "NO2", "H2", "CH4", "Ethanol", "NH3", "NOx", "Pressure"};
  const char* units[] = {"ppm", "ppm", "ppm", "ppm", "ppm", "ppm", "idx", "hPa"};
  for (int i = 0; i < 8; i++) {
    lv_obj_t* row = card(tile, 58);
    lv_obj_set_style_pad_left(row, 14, 0);
    gasPip_[i] = lv_obj_create(row);
    lv_obj_set_size(gasPip_[i], 5, 28);
    lv_obj_add_flag(gasPip_[i], LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_align(gasPip_[i], LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_radius(gasPip_[i], 3, 0);
    lv_obj_set_style_border_width(gasPip_[i], 0, 0);
    lv_obj_set_style_bg_color(gasPip_[i], hex(0x2C3544), 0);
    text(row, names[i], &lv_font_montserrat_12, kMuted);
    lv_obj_align(lv_obj_get_child(row, 1), LV_ALIGN_LEFT_MID, 12, -10);
    text(row, units[i], &lv_font_montserrat_12, kMuted);
    lv_obj_align(lv_obj_get_child(row, 2), LV_ALIGN_LEFT_MID, 12, 10);
    gasValue_[i] = text(row, "--", &lv_font_montserrat_28, kText);
    lv_obj_align(gasValue_[i], LV_ALIGN_RIGHT_MID, -4, 0);
  }
}

void HaloUi::buildDevice(lv_obj_t* tile) {
  text(tile, "DEVICE", &lv_font_montserrat_14, kMuted);
  lv_obj_t* body = card(tile, 430);
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(body, 8, 0);
  devBody_ = text(body, "Starting", &lv_font_montserrat_14, kText);
  lv_label_set_long_mode(devBody_, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(devBody_, lv_pct(100));
  text(body, "Swipe left for the next page, right for the previous. Hold BOOT for 5 seconds to forget Wi-Fi.",
       &lv_font_montserrat_12, kMuted);
  lv_label_set_long_mode(lv_obj_get_child(body, -1), LV_LABEL_LONG_WRAP);
  lv_obj_set_width(lv_obj_get_child(body, -1), lv_pct(100));
}

void HaloUi::refreshClock(const UiStatus& status) {
  char buf[16];
  time_t now = time(nullptr);
  if (now < 1700000000) {
    snprintf(buf, sizeof(buf), "--:--");
  } else {
    struct tm local;
    localtime_r(&now, &local);
    if (status.clock24h) strftime(buf, sizeof(buf), "%H:%M", &local);
    else {
      int hour = local.tm_hour % 12;
      if (hour == 0) hour = 12;
      snprintf(buf, sizeof(buf), "%d:%02d%s", hour, local.tm_min, local.tm_hour >= 12 ? "PM" : "AM");
    }
  }
  lv_label_set_text(timeLabel_, buf);
  lv_obj_set_style_img_recolor(wifiIcon_, hex(status.wifi ? kGood : kBad), 0);
  lv_img_set_src(mqttIcon_, status.mqtt ? &kMqttOnIcon : &kMqttOffIcon);
}

void HaloUi::refresh(const UiStatus& status) {
  const Readings& r = *status.readings;
  if (r.aqiOk) {
    if (r.aqiEvac) lv_label_set_text(aqiValue_, "EVAC");
    else lv_label_set_text_fmt(aqiValue_, "%d", r.aqi);
    lv_label_set_text(aqiCat_, aqiCategory(r.aqi, r.aqiEvac));
    lv_color_t color = aqiColor(r.aqi, r.aqiEvac);
    lv_obj_set_style_text_color(aqiValue_, color, 0);
    lv_obj_set_style_text_color(aqiCat_, color, 0);
    lv_obj_set_style_bg_color(aqiRail_, color, 0);
  } else {
    lv_label_set_text(aqiValue_, "--");
    lv_label_set_text(aqiCat_, r.senPresent ? "Warming up" : "SEN55 missing");
    lv_obj_set_style_text_color(aqiValue_, hex(kText), 0);
    lv_obj_set_style_text_color(aqiCat_, hex(kMuted), 0);
    lv_obj_set_style_bg_color(aqiRail_, hex(kMuted), 0);
  }

  lv_label_set_text(tempUnit_, unitSuffix(status.tempUnit));
  if (r.senOk) {
    lv_label_set_text_fmt(tempValue_, "%.1f", shownTemp(r.senTempC, status.tempUnit));
    lv_obj_set_style_text_color(tempValue_, tempColor(r.senTempC), 0);
    lv_label_set_text_fmt(humValue_, "%.0f", r.senHum);
    lv_obj_set_style_text_color(humValue_, humColor(r.senHum), 0);
    lv_label_set_text_fmt(pmValue_[0], "%.1f", r.pm1);
    lv_label_set_text_fmt(pmValue_[1], "%.1f", r.pm25);
    lv_label_set_text_fmt(pmValue_[2], "%.1f", r.pm4);
    lv_label_set_text_fmt(pmValue_[3], "%.1f", r.pm10);
    lv_obj_set_style_text_color(pmValue_[1], r.aqiOk ? aqiColor(r.aqi, r.aqiEvac) : hex(kText), 0);
    lv_label_set_text_fmt(vocValue_, "%.0f", r.voc);
    lv_label_set_text(vocWord_, r.vocQuality);
    lv_color_t vocColor = upperBand(r.voc, 150, 250, 400);
    lv_obj_set_style_text_color(vocValue_, vocColor, 0);
    lv_obj_set_style_text_color(vocWord_, vocColor, 0);
  }

  if (r.co2Ok) {
    lv_label_set_text_fmt(co2Value_, "%.0f", r.co2);
    lv_obj_set_style_text_color(co2Value_, upperBand(r.co2, 600, 1000, 1500), 0);
  }

  const float gasValues[] = {r.co, r.no2, r.h2, r.ch4, r.ethanol, r.nh3, r.nox, r.bmePressureHpa};
  const bool gasOk[] = {r.micsOk, r.micsOk, r.micsOk, r.micsOk, r.micsOk, r.micsOk, r.senOk, r.bmeOk};
  const int decimals[] = {2, 2, 2, 2, 2, 2, 0, 0};
  const float g1[] = {15, 0.03f, 0.4f, 2, 0.5f, 0.5f, 150, 0};
  const float g2[] = {30, 0.06f, 1, 10, 1.5f, 1, 250, 0};
  const float g3[] = {36, 0.1f, 2, -1, 3, 2, 400, -1};
  for (int i = 0; i < 8; i++) {
    if (!gasOk[i]) {
      lv_label_set_text(gasValue_[i], "--");
      lv_obj_set_style_text_color(gasValue_[i], hex(kMuted), 0);
      lv_obj_set_style_bg_color(gasPip_[i], hex(0x2C3544), 0);
      continue;
    }
    lv_label_set_text_fmt(gasValue_[i], decimals[i] == 0 ? "%.0f" : "%.2f", gasValues[i]);
    if (i == 7) {
      lv_obj_set_style_text_color(gasValue_[i], hex(kText), 0);
      lv_obj_set_style_bg_color(gasPip_[i], hex(kMuted), 0);
    } else {
      lv_color_t color = upperBand(gasValues[i], g1[i], g2[i], g3[i]);
      lv_obj_set_style_text_color(gasValue_[i], color, 0);
      lv_obj_set_style_bg_color(gasPip_[i], color, 0);
    }
  }

  char uptime[32];
  unsigned long sec = status.uptimeSec;
  unsigned long days = sec / 86400UL;
  unsigned long hours = (sec % 86400UL) / 3600UL;
  unsigned long mins = (sec % 3600UL) / 60UL;
  if (days) snprintf(uptime, sizeof(uptime), "%lud %luh %lum", days, hours, mins);
  else snprintf(uptime, sizeof(uptime), "%luh %lum", hours, mins);

  const char* mics = "missing";
  if (r.micsPresent && r.micsReady) mics = r.micsOk ? "live" : "calibrating";
  else if (r.micsPresent) mics = "warming";

  lv_label_set_text_fmt(devBody_,
                        "Firmware  " HALO_FW_VERSION
                        "\nWi-Fi  %s\nIP  %s\nMQTT  %s\nUptime  %s\n\n"
                        "SCD40  %s\nSEN55  %s\nBME280  %s\nMiCS-4514  %s",
                        status.wifi ? "connected" : "offline", status.ip ? status.ip : "--",
                        status.mqtt ? "connected" : (status.mqttConfigured ? "waiting" : "not set"), uptime,
                        r.scdPresent ? "ok" : "missing", r.senPresent ? "ok" : "missing",
                        r.bmePresent ? "ok" : "missing", mics);
}

void HaloUi::loop(const UiStatus& status) {
  int swipe = touchTakeSwipe();
  if (swipe > 0) nextPage();
  else if (swipe < 0) prevPage();
  if (status.readings == nullptr) return;
  uint32_t now = millis();
  bool readingChanged = status.readings->updatedMs != lastReadingMs_;
  bool unitChanged = status.tempUnit != lastUnit_;
  bool linkChanged = status.wifi != lastWifi_ || status.mqtt != lastMqtt_;
  if (now - lastClockMs_ >= 1000 || linkChanged) {
    refreshClock(status);
    lastClockMs_ = now;
    lastWifi_ = status.wifi;
    lastMqtt_ = status.mqtt;
  }
  if (readingChanged || unitChanged || linkChanged || lastReadingMs_ == 0) {
    refresh(status);
    lastReadingMs_ = status.readings->updatedMs == 0 ? 1 : status.readings->updatedMs;
    lastUnit_ = status.tempUnit;
  }

  const Settings& cfg = settings();
  uint32_t quiet = touchLastActivityMs();
  uint32_t intervalMs = (uint32_t)cfg.pageIntervalS * 1000UL;
  if (cfg.pageRotation && intervalMs >= 5000 && (now - quiet) > intervalMs && (now - lastAutoMs_) > intervalMs) {
    nextPage();
  }
  lv_timer_handler();
}
