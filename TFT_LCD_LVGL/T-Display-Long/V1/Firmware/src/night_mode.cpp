#include "night_mode.h"

#include <Arduino.h>
#include <math.h>
#include <stdlib.h>

#include "backlight.h"
#include "ring_light.h"
#include "settings.h"

namespace {

constexpr float kDeg = 0.017453292f;
constexpr float kAssumedLat = 40.0f;  // when only the timezone is known
constexpr uint32_t kCheckMs = 30000;

bool clockSet(time_t now) { return now > 1704067200; }

// Standard-time UTC offset (hours, east positive) from a POSIX TZ such as "CST6CDT,M3.2.0,M11.1.0".
bool tzOffsetHours(const char* tz, float& hours) {
  const char* p = tz;
  if (*p == '<') {
    while (*p && *p != '>') p++;
    if (*p) p++;
  } else {
    while (*p && ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z'))) p++;
  }
  if (!*p) return false;
  int sign = 1;
  if (*p == '+' || *p == '-') sign = *p++ == '-' ? -1 : 1;
  if (*p < '0' || *p > '9') return false;
  float h = strtol(p, (char**)&p, 10);
  if (*p == ':') h += strtol(p + 1, nullptr, 10) / 60.0f;
  hours = -sign * h;  // POSIX offsets count west as positive
  return true;
}

// NOAA solar equations. Sunrise and sunset for the UTC day starting at dayStart.
// Returns 1 normally, 0 for polar night (no daylight), 2 for polar day (all daylight).
int sunTimes(time_t dayStart, float lat, float lon, time_t& rise, time_t& set) {
  struct tm t;
  gmtime_r(&dayStart, &t);
  float g = 2.0f * PI / 365.0f * t.tm_yday;
  float eqTime = 229.18f * (0.000075f + 0.001868f * cosf(g) - 0.032077f * sinf(g) -
                            0.014615f * cosf(2 * g) - 0.040849f * sinf(2 * g));
  float decl = 0.006918f - 0.399912f * cosf(g) + 0.070257f * sinf(g) - 0.006758f * cosf(2 * g) +
               0.000907f * sinf(2 * g) - 0.002697f * cosf(3 * g) + 0.00148f * sinf(3 * g);
  float cosH = cosf(90.833f * kDeg) / (cosf(lat * kDeg) * cosf(decl)) - tanf(lat * kDeg) * tanf(decl);
  if (cosH > 1) return 0;
  if (cosH < -1) return 2;
  float ha = acosf(cosH) / kDeg;
  rise = dayStart + (time_t)((720.0f - 4.0f * (lon + ha) - eqTime) * 60.0f);
  set = dayStart + (time_t)((720.0f - 4.0f * (lon - ha) - eqTime) * 60.0f);
  return 1;
}

time_t utcMidnight(time_t now) { return now - (now % 86400); }

bool isDaylight(time_t now, float lat, float lon) {
  time_t today = utcMidnight(now);
  for (int k = -1; k <= 1; k++) {
    time_t day = today + k * 86400;
    time_t rise = 0, set = 0;
    int kind = sunTimes(day, lat, lon, rise, set);
    if (kind == 2 && now >= day && now < day + 86400) return true;
    if (kind == 1 && now >= rise && now < set) return true;
  }
  return false;
}

}  // namespace

NightMode& nightMode() {
  static NightMode mode;
  return mode;
}

void NightMode::location(float& lat, float& lon, bool& estimated) const {
  const Settings& cfg = settings();
  if (!isnan(cfg.nightLat) && !isnan(cfg.nightLon)) {
    lat = cfg.nightLat;
    lon = cfg.nightLon;
    estimated = false;
    return;
  }
  float hours = 0;
  if (!tzOffsetHours(cfg.tz, hours)) hours = -8;
  lat = kAssumedLat;
  lon = hours * 15.0f;
  estimated = true;
}

bool NightMode::nextTimes(time_t& sunset, time_t& sunrise) const {
  time_t now = time(nullptr);
  if (!clockSet(now)) return false;
  float lat, lon;
  bool estimated;
  location(lat, lon, estimated);
  sunset = 0;
  sunrise = 0;
  time_t today = utcMidnight(now);
  for (int k = -1; k <= 2; k++) {
    time_t rise = 0, set = 0;
    if (sunTimes(today + k * 86400, lat, lon, rise, set) != 1) continue;
    if (set > now && (sunset == 0 || set < sunset)) sunset = set;
    if (rise > now && (sunrise == 0 || rise < sunrise)) sunrise = rise;
  }
  return sunset != 0 && sunrise != 0;
}

void NightMode::enter() {
  dayRing_ = ring().brightness();
  dayBacklight_ = backlight().level();
  // Only ever dim; a ring or screen already below the night level is left alone.
  if (dayRing_ > kRingLevel) ring().setBrightness(kRingLevel);
  if (dayBacklight_ > kBacklightLevel) backlight().setLevel(kBacklightLevel);
  active_ = true;
  Serial.println("Night mode on");
}

void NightMode::leave() {
  // Restore only what night mode set; a change made during the night wins.
  if (dayRing_ > kRingLevel && ring().brightness() == kRingLevel) ring().setBrightness(dayRing_);
  if (dayBacklight_ > kBacklightLevel && backlight().level() == kBacklightLevel) backlight().setLevel(dayBacklight_);
  active_ = false;
  Serial.println("Night mode off");
}

void NightMode::loop() {
  uint32_t ms = millis();
  if (lastCheckMs_ != 0 && ms - lastCheckMs_ < kCheckMs) return;
  lastCheckMs_ = ms;

  bool want = false;
  if (settings().nightMode) {
    time_t now = time(nullptr);
    if (!clockSet(now)) return;  // keep the current state until NTP has the time
    float lat, lon;
    bool estimated;
    location(lat, lon, estimated);
    want = !isDaylight(now, lat, lon);
  }
  if (want && !active_) enter();
  else if (!want && active_) leave();
}
