#pragma once

#include <lvgl.h>

// A weather icon drawn from LVGL shapes (discs, pills and lines), so it needs
// no bitmaps in flash and scales to any square size. Shapes use shared styles,
// which keeps the LVGL heap cost of each icon small.
class WxIcon {
 public:
  void create(lv_obj_t* parent, int size);
  // wmo is an Open-Meteo / WMO weather code. Rebuilds only when it changes.
  void set(int wmo, bool day);
  lv_obj_t* obj() const { return box_; }

 private:
  enum Group : uint8_t { Clear, Partly, Overcast, Fog, Drizzle, Rain, Snow, Thunder };
  static Group groupFor(int wmo, bool& heavy);

  int sc(float v) const { return (int)(v * size_ / 100.0f + 0.5f); }
  lv_obj_t* disc(float cx, float cy, float w, float h, int kind);
  lv_obj_t* line(float x1, float y1, float x2, float y2, int kind);
  void cloud(float cx, float cy, float k, int kind);
  void sun(float cx, float cy, float r, float rayIn, float rayOut);
  void moon(float cx, float cy, float r, bool stars);

  lv_obj_t* box_ = nullptr;
  int size_ = 0;
  int code_ = -1;
  int day_ = -1;
  int lineCount_ = 0;
  lv_point_t pts_[20][2] = {};
};
