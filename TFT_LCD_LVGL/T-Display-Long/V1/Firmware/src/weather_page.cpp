#include "weather_page.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "settings.h"
#include "sun_icons.h"

namespace {
constexpr uint32_t kCard = 0x141820;
constexpr uint32_t kText = 0xF4F7FB;
constexpr uint32_t kMuted = 0x93A0B5;
constexpr uint32_t kFaint = 0x5B667A;
constexpr uint32_t kRainBlue = 0x5AA9FF;
constexpr uint32_t kTrack = 0x262D3A;

lv_color_t hex(uint32_t c) { return lv_color_hex(c); }

lv_obj_t* label(lv_obj_t* parent, const char* text, const lv_font_t* font, uint32_t color) {
  lv_obj_t* l = lv_label_create(parent);
  lv_label_set_text(l, text);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, hex(color), 0);
  return l;
}

lv_obj_t* plain(lv_obj_t* parent, int w, int h) {
  lv_obj_t* o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  if (w == LV_PCT(100)) lv_obj_set_width(o, lv_pct(100));
  else lv_obj_set_width(o, w);
  lv_obj_set_height(o, h);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  return o;
}

lv_obj_t* card(lv_obj_t* parent, int w, int h, int radius, int pad) {
  lv_obj_t* o = plain(parent, w, h);
  lv_obj_set_style_bg_color(o, hex(kCard), 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(o, radius, 0);
  lv_obj_set_style_pad_all(o, pad, 0);
  return o;
}

float convert(float c, uint8_t unit) {
  if (unit == TEMP_F) return c * 9.0f / 5.0f + 32.0f;
  if (unit == TEMP_K) return c + 273.15f;
  return c;
}

const char* unitLetter(uint8_t unit) { return unit == TEMP_C ? "C" : (unit == TEMP_K ? "K" : "F"); }

const char* conditionText(int wmo) {
  switch (wmo) {
    case 0: return "Clear sky";
    case 1: return "Mostly clear";
    case 2: return "Partly cloudy";
    case 3: return "Overcast";
    case 45:
    case 48: return "Fog";
    case 51:
    case 53:
    case 55: return "Drizzle";
    case 56:
    case 57: return "Freezing drizzle";
    case 61: return "Light rain";
    case 63: return "Rain";
    case 65: return "Heavy rain";
    case 66:
    case 67: return "Freezing rain";
    case 71: return "Light snow";
    case 73: return "Snow";
    case 75: return "Heavy snow";
    case 77: return "Snow grains";
    case 80:
    case 81: return "Rain showers";
    case 82: return "Heavy showers";
    case 85:
    case 86: return "Snow showers";
    case 95: return "Thunderstorm";
    case 96:
    case 99: return "Thunder and hail";
    default: return "Cloudy";
  }
}

// The hero card fades from a tint that suggests the weather into the card colour.
uint32_t heroTint(int wmo, bool day) {
  if (wmo == 0 || wmo == 1) return day ? 0x1F5FB0 : 0x1B1F52;
  if (wmo == 2) return day ? 0x2C6AA6 : 0x23294F;
  if (wmo == 45 || wmo == 48) return 0x3C4450;
  if (wmo >= 95) return 0x43295F;
  if ((wmo >= 71 && wmo <= 77) || wmo == 85 || wmo == 86) return 0x3A4F6C;
  if (wmo >= 51) return 0x23415F;
  return 0x3A4659;  // overcast
}

uint32_t lerpColor(uint32_t a, uint32_t b, float t) {
  if (t < 0) t = 0;
  if (t > 1) t = 1;
  auto ch = [&](int shift) {
    float x = (float)((a >> shift) & 0xFF);
    float y = (float)((b >> shift) & 0xFF);
    return (uint32_t)(x + (y - x) * t + 0.5f) & 0xFF;
  };
  return (ch(16) << 16) | (ch(8) << 8) | ch(0);
}

// Cool blues through green and yellow to orange and red, by degrees Celsius.
uint32_t tempColor(float c) {
  static const struct { float c; uint32_t color; } stops[] = {
      {-10, 0x4D8DFF}, {5, 0x4DD0E1}, {15, 0x7BE495}, {22, 0xFFD166}, {30, 0xFF8A4C}, {38, 0xFF5A5F}};
  const int n = sizeof(stops) / sizeof(stops[0]);
  if (c <= stops[0].c) return stops[0].color;
  for (int i = 1; i < n; i++) {
    if (c <= stops[i].c) return lerpColor(stops[i - 1].color, stops[i].color, (c - stops[i - 1].c) / (stops[i].c - stops[i - 1].c));
  }
  return stops[n - 1].color;
}

void clockText(uint32_t epoch, int32_t offset, bool clock24h, char* out, size_t n) {
  uint32_t local = epoch + (uint32_t)offset;
  int mins = (int)((local % 86400UL) / 60UL);
  int h = mins / 60, m = mins % 60;
  if (clock24h) snprintf(out, n, "%02d:%02d", h, m);
  else snprintf(out, n, "%d:%02d%c", h % 12 == 0 ? 12 : h % 12, m, h >= 12 ? 'p' : 'a');
}

const char* dayName(uint32_t date, int32_t offset) {
  static const char* names[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  int64_t days = ((int64_t)date + offset) / 86400;
  return names[(days + 4) % 7];
}

// "America/Los_Angeles" -> "LOS ANGELES"
void placeName(const char* zone, char* out, size_t n) {
  const char* slash = strrchr(zone, '/');
  const char* city = slash ? slash + 1 : zone;
  size_t i = 0;
  for (; city[i] && i + 1 < n; i++) out[i] = city[i] == '_' ? ' ' : (char)toupper((unsigned char)city[i]);
  out[i] = 0;
  if (i == 0) strlcpy(out, "WEATHER", n);
}
}  // namespace

void WeatherPage::build(lv_obj_t* tile) {
  hero_ = card(tile, LV_PCT(100), 206, 18, 8);
  lv_obj_set_flex_flow(hero_, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(hero_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(hero_, 0, 0);
  lv_obj_set_style_bg_main_stop(hero_, 0, 0);
  lv_obj_set_style_bg_grad_stop(hero_, 210, 0);
  lv_obj_set_style_bg_grad_dir(hero_, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_bg_grad_color(hero_, hex(kCard), 0);
  lv_obj_set_style_bg_color(hero_, hex(0x23415F), 0);

  place_ = label(hero_, "WEATHER", &lv_font_montserrat_12, 0xC9D6EA);
  lv_obj_set_style_text_letter_space(place_, 1, 0);
  icon_.create(hero_, 88);
  lv_obj_set_style_pad_top(icon_.obj(), 0, 0);
  temp_ = label(hero_, "--", &lv_font_montserrat_48, kText);
  cond_ = label(hero_, "", &lv_font_montserrat_14, kText);
  hiLo_ = label(hero_, "", &lv_font_montserrat_12, 0xC9D6EA);
  msg_ = label(hero_, "", &lv_font_montserrat_14, kText);
  lv_label_set_long_mode(msg_, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(msg_, 140);
  lv_obj_set_style_text_align(msg_, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_add_flag(msg_, LV_OBJ_FLAG_HIDDEN);

  // Three small stats. The wind chip is wider because "12 km/h" needs the room.
  lv_obj_t* chips = plain(tile, LV_PCT(100), 46);
  lv_obj_set_flex_flow(chips, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(chips, 6, 0);
  struct ChipDef { const char* tag; int w; lv_obj_t** value; const lv_font_t* font; } defs[] = {
      {"FEELS", 47, &feels_, &lv_font_montserrat_14},
      {"HUMID", 47, &humid_, &lv_font_montserrat_14},
      {"WIND", 58, &wind_, &lv_font_montserrat_12},
  };
  for (auto& d : defs) {
    lv_obj_t* c = card(chips, d.w, 46, 12, 3);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(c, 1, 0);
    label(c, d.tag, &lv_font_montserrat_12, kMuted);
    *d.value = label(c, "--", d.font, kText);
  }

  // Sunrise on the left, sunset on the right: the supplied glyph, then the time.
  lv_obj_t* sunBox = plain(tile, LV_PCT(100), 40);
  lv_obj_set_flex_flow(sunBox, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(sunBox, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  const lv_img_dsc_t* sunGlyph[2] = {&kSunriseIcon, &kSunsetIcon};
  lv_obj_t** sunTime[2] = {&sunrise_, &sunset_};
  for (int i = 0; i < 2; i++) {
    lv_obj_t* pair = plain(sunBox, LV_SIZE_CONTENT, 40);
    lv_obj_set_width(pair, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(pair, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(pair, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(pair, 2, 0);
    lv_obj_t* glyph = lv_img_create(pair);
    lv_img_set_src(glyph, sunGlyph[i]);
    lv_obj_clear_flag(glyph, LV_OBJ_FLAG_CLICKABLE);
    *sunTime[i] = label(pair, "--", &lv_font_montserrat_14, kText);
  }

  forecast_ = card(tile, LV_PCT(100), 252, 16, 8);
  lv_obj_set_style_pad_top(forecast_, 6, 0);
  lv_obj_t* title = label(forecast_, "NEXT 5 DAYS", &lv_font_montserrat_12, kMuted);
  lv_obj_set_style_text_letter_space(title, 1, 0);
  lv_obj_set_pos(title, 2, 0);
  for (int i = 0; i < kRows; i++) {
    Row& r = rows_[i];
    r.box = plain(forecast_, 148, 44);
    lv_obj_set_pos(r.box, 0, 18 + i * 44);
    r.day = label(r.box, "---", &lv_font_montserrat_14, kText);
    lv_obj_set_pos(r.day, 2, 5);
    r.rain = label(r.box, "", &lv_font_montserrat_12, kRainBlue);
    lv_obj_set_pos(r.rain, 2, 24);
    r.icon.create(r.box, 32);
    lv_obj_set_pos(r.icon.obj(), 46, 6);
    r.lo = label(r.box, "--", &lv_font_montserrat_12, kMuted);
    lv_obj_set_pos(r.lo, 86, 6);
    r.hi = label(r.box, "--", &lv_font_montserrat_14, kText);
    lv_obj_set_width(r.hi, 40);
    lv_obj_set_style_text_align(r.hi, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(r.hi, 108, 4);
    lv_obj_t* track = plain(r.box, 62, 5);
    lv_obj_set_style_bg_color(track, hex(kTrack), 0);
    lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(track, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_pos(track, 86, 28);
    r.fill = plain(track, 20, 5);
    lv_obj_set_style_bg_opa(r.fill, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(r.fill, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_grad_dir(r.fill, LV_GRAD_DIR_HOR, 0);
    if (i < kRows - 1) {
      r.rule = plain(r.box, 148, 1);
      lv_obj_set_style_bg_color(r.rule, hex(0x1E2531), 0);
      lv_obj_set_style_bg_opa(r.rule, LV_OPA_COVER, 0);
      lv_obj_set_pos(r.rule, 0, 43);
    }
  }

  lv_obj_t* credit = label(tile, "Weather: Open-Meteo.com", &lv_font_montserrat_12, kFaint);
  lv_obj_set_style_text_align(credit, LV_TEXT_ALIGN_CENTER, 0);
  built_ = true;
  showMessage("Loading weather...", "");
}

void WeatherPage::showMessage(const char* line1, const char* line2) {
  lv_obj_add_flag(icon_.obj(), LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(temp_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(cond_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(hiLo_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(msg_, LV_OBJ_FLAG_HIDDEN);
  char text[96];
  if (line2[0]) snprintf(text, sizeof(text), "%s\n%s", line1, line2);
  else snprintf(text, sizeof(text), "%s", line1);
  lv_label_set_text(msg_, text);
  lv_label_set_text(place_, "WEATHER");
  lv_obj_set_style_bg_color(hero_, hex(0x23415F), 0);
  lv_obj_set_flex_align(hero_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_label_set_text(feels_, "--");
  lv_label_set_text(humid_, "--");
  lv_label_set_text(wind_, "--");
  lv_label_set_text(sunrise_, "--");
  lv_label_set_text(sunset_, "--");
  for (Row& r : rows_) lv_obj_add_flag(r.box, LV_OBJ_FLAG_HIDDEN);
}

void WeatherPage::showData(const WxData& d, uint8_t unit, bool clock24h) {
  lv_obj_clear_flag(icon_.obj(), LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(temp_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(cond_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(hiLo_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(msg_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_flex_align(hero_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  char buf[48];
  const char* saved = settings().place;
  if (saved[0]) {
    size_t i = 0;
    // "Foster City, California" shows as "FOSTER CITY".
    for (; saved[i] && saved[i] != ',' && i + 1 < sizeof(buf); i++) buf[i] = (char)toupper((unsigned char)saved[i]);
    buf[i] = 0;
  } else {
    placeName(d.zone, buf, sizeof(buf));
  }
  lv_label_set_text(place_, buf);
  lv_obj_set_style_bg_color(hero_, hex(heroTint(d.code, d.isDay)), 0);
  icon_.set(d.code, d.isDay);
  snprintf(buf, sizeof(buf), "%.0f\xC2\xB0%s", convert(d.tempC, unit), unitLetter(unit));
  lv_label_set_text(temp_, buf);
  lv_label_set_text(cond_, conditionText(d.code));
  if (d.days > 0) {
    snprintf(buf, sizeof(buf), "H %.0f\xC2\xB0   L %.0f\xC2\xB0", convert(d.day[0].maxC, unit),
             convert(d.day[0].minC, unit));
  } else {
    buf[0] = 0;
  }
  lv_label_set_text(hiLo_, buf);

  snprintf(buf, sizeof(buf), "%.0f\xC2\xB0", convert(d.feelsC, unit));
  lv_label_set_text(feels_, buf);
  snprintf(buf, sizeof(buf), "%d%%", d.humidity);
  lv_label_set_text(humid_, buf);
  if (unit == TEMP_F) snprintf(buf, sizeof(buf), "%.0f mph", d.windKmh * 0.621371f);
  else snprintf(buf, sizeof(buf), "%.0f km/h", d.windKmh);
  lv_label_set_text(wind_, buf);

  if (d.sunrise > 0 && d.sunset > 0) {
    char up[12], down[12];
    clockText(d.sunrise, d.utcOffset, clock24h, up, sizeof(up));
    clockText(d.sunset, d.utcOffset, clock24h, down, sizeof(down));
    lv_label_set_text(sunrise_, up);
    lv_label_set_text(sunset_, down);
  } else {
    lv_label_set_text(sunrise_, "--");
    lv_label_set_text(sunset_, "--");
  }

  // The five days after today share one temperature scale, so the bars compare.
  float gmin = 1000, gmax = -1000;
  for (int i = 1; i < d.days; i++) {
    if (d.day[i].minC < gmin) gmin = d.day[i].minC;
    if (d.day[i].maxC > gmax) gmax = d.day[i].maxC;
  }
  float span = gmax - gmin;
  for (int i = 0; i < kRows; i++) {
    Row& r = rows_[i];
    int di = i + 1;
    if (di >= d.days) {
      lv_obj_add_flag(r.box, LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    lv_obj_clear_flag(r.box, LV_OBJ_FLAG_HIDDEN);
    const WxDay& day = d.day[di];
    lv_label_set_text(r.day, dayName(day.date, d.utcOffset));
    if (day.rain >= 10) {
      snprintf(buf, sizeof(buf), "%d%%", day.rain);
      lv_label_set_text(r.rain, buf);
      lv_obj_set_style_text_color(r.rain, hex(day.rain >= 40 ? kRainBlue : kFaint), 0);
    } else {
      lv_label_set_text(r.rain, "");
    }
    r.icon.set(day.code, true);
    snprintf(buf, sizeof(buf), "%.0f\xC2\xB0", convert(day.minC, unit));
    lv_label_set_text(r.lo, buf);
    snprintf(buf, sizeof(buf), "%.0f\xC2\xB0", convert(day.maxC, unit));
    lv_label_set_text(r.hi, buf);
    int trackW = 62;
    int x = 0, w = trackW;
    if (span > 0.5f) {
      x = (int)((day.minC - gmin) / span * trackW + 0.5f);
      w = (int)((day.maxC - day.minC) / span * trackW + 0.5f);
      if (w < 8) w = 8;
      if (x + w > trackW) x = trackW - w;
    }
    lv_obj_set_width(r.fill, w);
    lv_obj_set_x(r.fill, x);
    lv_obj_set_style_bg_color(r.fill, hex(tempColor(day.minC)), 0);
    lv_obj_set_style_bg_grad_color(r.fill, hex(tempColor(day.maxC)), 0);
  }
}

void WeatherPage::refresh(uint8_t tempUnit, bool clock24h, const char* ip) {
  if (!built_) return;
  WeatherService& svc = weather();
  WxState state = svc.state();
  uint32_t version = svc.version();
  bool have = svc.ageMs() != UINT32_MAX;
  if (have == shownHave_ && version == shownVersion_ && state == shownState_ && tempUnit == shownUnit_ &&
      clock24h == shownClock_) {
    return;
  }
  shownHave_ = have;
  shownVersion_ = version;
  shownState_ = state;
  shownUnit_ = tempUnit;
  shownClock_ = clock24h;

  WxData d;
  if (have && svc.snapshot(d)) {
    showData(d, tempUnit, clock24h);
    return;
  }
  switch (state) {
    case WxState::NoLocation: {
      char addr[48];
      snprintf(addr, sizeof(addr), "http://%s", ip && ip[0] ? ip : "halo");
      showMessage("Set your location in", addr);
      break;
    }
    case WxState::Failed:
      showMessage("Weather unavailable", "Trying again soon");
      break;
    case WxState::Waiting:
      showMessage("Waiting for Wi-Fi", "");
      break;
    default:
      showMessage("Loading weather...", "");
      break;
  }
}
