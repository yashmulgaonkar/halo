#include "weather_icon.h"

#include <math.h>

namespace {
enum DiscKind { kSun, kMoon, kCrater, kCloud, kCloudDark, kCloudNight, kSnow, kStar, kDiscCount };
enum LineKind { kRay, kDrop, kBolt, kFog, kLineCount };

lv_style_t g_disc[kDiscCount];
lv_style_t g_line[2][kLineCount];  // [small, large]
bool g_ready = false;

void initStyles() {
  if (g_ready) return;
  g_ready = true;
  const uint32_t colors[kDiscCount] = {0xFFC93C, 0xE6ECFA, 0xC3CCE2, 0xEDF2FA, 0xAEBBCF, 0x9AA8C0, 0xFFFFFF, 0xFFFFFF};
  for (int i = 0; i < kDiscCount; i++) {
    lv_style_init(&g_disc[i]);
    lv_style_set_bg_color(&g_disc[i], lv_color_hex(colors[i]));
    lv_style_set_bg_opa(&g_disc[i], LV_OPA_COVER);
    lv_style_set_radius(&g_disc[i], LV_RADIUS_CIRCLE);
    lv_style_set_border_width(&g_disc[i], 0);
    lv_style_set_pad_all(&g_disc[i], 0);
    lv_style_set_shadow_width(&g_disc[i], 0);
    lv_style_set_outline_width(&g_disc[i], 0);
  }
  const uint32_t lineColors[kLineCount] = {0xFFB02E, 0x4FA3FF, 0xFFD43B, 0xB9C4D6};
  const int widths[2][kLineCount] = {{2, 2, 2, 2}, {5, 4, 4, 5}};
  for (int s = 0; s < 2; s++) {
    for (int i = 0; i < kLineCount; i++) {
      lv_style_init(&g_line[s][i]);
      lv_style_set_line_color(&g_line[s][i], lv_color_hex(lineColors[i]));
      lv_style_set_line_width(&g_line[s][i], widths[s][i]);
      lv_style_set_line_rounded(&g_line[s][i], true);
    }
  }
}
}  // namespace

void WxIcon::create(lv_obj_t* parent, int size) {
  initStyles();
  size_ = size;
  box_ = lv_obj_create(parent);
  lv_obj_remove_style_all(box_);
  lv_obj_set_size(box_, size, size);
  lv_obj_clear_flag(box_, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t* WxIcon::disc(float cx, float cy, float w, float h, int kind) {
  lv_obj_t* o = lv_obj_create(box_);
  lv_obj_remove_style_all(o);
  lv_obj_add_style(o, &g_disc[kind], 0);
  int pw = sc(w) < 2 ? 2 : sc(w);
  int ph = sc(h) < 2 ? 2 : sc(h);
  lv_obj_set_size(o, pw, ph);
  lv_obj_set_pos(o, sc(cx) - pw / 2, sc(cy) - ph / 2);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  return o;
}

lv_obj_t* WxIcon::line(float x1, float y1, float x2, float y2, int kind) {
  if (lineCount_ >= 20) return nullptr;
  lv_point_t* p = pts_[lineCount_++];
  p[0].x = sc(x1);
  p[0].y = sc(y1);
  p[1].x = sc(x2);
  p[1].y = sc(y2);
  lv_obj_t* o = lv_line_create(box_);
  lv_obj_remove_style_all(o);
  lv_obj_add_style(o, &g_line[size_ >= 60 ? 1 : 0][kind], 0);
  lv_line_set_points(o, p, 2);
  lv_obj_set_pos(o, 0, 0);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  return o;
}

// A cloud is a pill with three bumps. The local shape spans x 18..82, y 26..78,
// centred on (50, 52), and is scaled by k around the requested centre.
void WxIcon::cloud(float cx, float cy, float k, int kind) {
  auto X = [&](float x) { return cx + (x - 50.0f) * k; };
  auto Y = [&](float y) { return cy + (y - 52.0f) * k; };
  disc(X(50), Y(66), 64 * k, 24 * k, kind);
  disc(X(34), Y(58), 26 * k, 26 * k, kind);
  disc(X(54), Y(46), 40 * k, 40 * k, kind);
  disc(X(72), Y(58), 26 * k, 26 * k, kind);
}

void WxIcon::sun(float cx, float cy, float r, float rayIn, float rayOut) {
  disc(cx, cy, r * 2, r * 2, kSun);
  for (int i = 0; i < 8; i++) {
    float a = i * 0.7853982f;
    float c = cosf(a), s = sinf(a);
    line(cx + c * rayIn, cy + s * rayIn, cx + c * rayOut, cy + s * rayOut, kRay);
  }
}

void WxIcon::moon(float cx, float cy, float r, bool stars) {
  disc(cx, cy, r * 2, r * 2, kMoon);
  disc(cx - r * 0.35f, cy - r * 0.25f, r * 0.55f, r * 0.55f, kCrater);
  disc(cx + r * 0.38f, cy + r * 0.2f, r * 0.38f, r * 0.38f, kCrater);
  disc(cx - r * 0.1f, cy + r * 0.55f, r * 0.26f, r * 0.26f, kCrater);
  if (stars) {
    disc(cx + r * 1.25f, cy - r * 0.9f, 5, 5, kStar);
    disc(cx - r * 1.35f, cy + r * 0.7f, 4, 4, kStar);
    disc(cx + r * 0.95f, cy + r * 1.1f, 3, 3, kStar);
  }
}

WxIcon::Group WxIcon::groupFor(int wmo, bool& heavy) {
  heavy = false;
  if (wmo == 0 || wmo == 1) return Clear;
  if (wmo == 2) return Partly;
  if (wmo == 3) return Overcast;
  if (wmo == 45 || wmo == 48) return Fog;
  if (wmo >= 51 && wmo <= 57) return Drizzle;
  if ((wmo >= 61 && wmo <= 67) || (wmo >= 80 && wmo <= 82)) {
    heavy = wmo == 65 || wmo == 67 || wmo == 82;
    return Rain;
  }
  if ((wmo >= 71 && wmo <= 77) || wmo == 85 || wmo == 86) return Snow;
  if (wmo >= 95) return Thunder;
  return Overcast;
}

void WxIcon::set(int wmo, bool day) {
  if (box_ == nullptr || (wmo == code_ && (day ? 1 : 0) == day_)) return;
  code_ = wmo;
  day_ = day ? 1 : 0;
  lv_obj_clean(box_);
  lineCount_ = 0;
  bool heavy = false;
  Group g = groupFor(wmo, heavy);
  int cl = day ? kCloud : kCloudNight;
  int dark = day ? kCloudDark : kCloudNight;

  switch (g) {
    case Clear:
      if (day) sun(50, 50, 19, 29, 43);
      else moon(50, 50, 24, true);
      break;
    case Partly:
      if (day) sun(36, 36, 14, 21, 30);
      else moon(34, 34, 15, false);
      cloud(58, 62, 0.82f, cl);
      break;
    case Overcast:
      cloud(38, 42, 0.72f, dark);
      cloud(58, 60, 0.85f, cl);
      break;
    case Fog:
      cloud(50, 34, 0.85f, cl);
      line(20, 62, 80, 62, kFog);
      line(28, 75, 72, 75, kFog);
      line(36, 88, 64, 88, kFog);
      break;
    case Drizzle:
      cloud(50, 38, 0.95f, dark);
      disc(36, 72, 5, 5, kStar);
      disc(52, 80, 5, 5, kStar);
      disc(68, 72, 5, 5, kStar);
      disc(44, 90, 5, 5, kStar);
      break;
    case Rain:
      cloud(50, 36, 0.95f, dark);
      line(34, 68, 28, 82, kDrop);
      line(50, 68, 44, 82, kDrop);
      line(66, 68, 60, 82, kDrop);
      if (heavy) {
        line(42, 80, 36, 94, kDrop);
        line(58, 80, 52, 94, kDrop);
      }
      break;
    case Snow:
      cloud(50, 36, 0.95f, cl);
      disc(34, 72, 7, 7, kSnow);
      disc(50, 82, 7, 7, kSnow);
      disc(66, 72, 7, 7, kSnow);
      disc(42, 92, 6, 6, kSnow);
      disc(60, 94, 6, 6, kSnow);
      break;
    case Thunder: {
      cloud(50, 34, 0.95f, dark);
      // Three connected segments form the bolt.
      line(56, 56, 44, 76, kBolt);
      line(44, 76, 55, 76, kBolt);
      line(55, 76, 45, 96, kBolt);
      break;
    }
  }
}
