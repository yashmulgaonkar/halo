#include "weather_service.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <math.h>
#include <string.h>
#include <time.h>

#include "event_log.h"
#include "pages_ca.h"
#include "settings.h"
#include "tls_lock.h"

namespace {
constexpr uint32_t kRefreshMs = 30UL * 60UL * 1000UL;  // Open-Meteo updates about every 15 min
constexpr uint32_t kBootGraceMs = 25000;               // let Wi-Fi, MQTT and the update check settle
// Never two forecast requests less than a minute apart, whatever the reason. Open-Meteo's free
// tier allows under 10,000 calls a day, and a request with more than 10 variables counts as
// more than one, so this caps the worst case at 1,440 requests (about 1,900 weighted) a day.
constexpr uint32_t kMinGapMs = 60000;
constexpr uint32_t kMaxBody = 6000;
constexpr const char* kForecastUrl = "https://api.open-meteo.com/v1/forecast";
constexpr const char* kGeocodeUrl = "https://geocoding-api.open-meteo.com/v1/search";

bool clockReady() { return time(nullptr) > 1704067200; }

void urlEncode(const char* in, char* out, size_t outSize) {
  static const char hex[] = "0123456789ABCDEF";
  size_t n = 0;
  for (; *in && n + 4 < outSize; in++) {
    unsigned char c = (unsigned char)*in;
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out[n++] = (char)c;
    } else {
      out[n++] = '%';
      out[n++] = hex[c >> 4];
      out[n++] = hex[c & 15];
    }
  }
  out[n] = 0;
}

// GET a small JSON document over verified TLS. Returns the body or an empty
// string. Holds the shared TLS lock for the whole request.
bool httpGetJson(const String& url, String& body, int& status) {
  TlsGuard tls(20000);
  if (!tls.ok()) {
    status = -100;
    return false;
  }
  WiFiClientSecure client;
  client.setCACert(kPagesCa);
  client.setHandshakeTimeout(15000);
  client.setTimeout(15);
  HTTPClient http;
  http.setTimeout(15000);
  http.useHTTP10(true);  // no chunked encoding to unwrap
  if (!http.begin(client, url)) {
    status = -101;
    return false;
  }
  status = http.GET();
  int len = http.getSize();
  if (status != 200 || len > (int)kMaxBody) {
    http.end();
    return false;
  }
  body = http.getString();
  http.end();
  return body.length() > 0 && body.length() <= kMaxBody;
}
}  // namespace

WeatherService& weather() {
  static WeatherService service;
  return service;
}

void WeatherService::begin() {
  lock_ = xSemaphoreCreateMutex();
  // Core 0 runs the Wi-Fi stack. The Arduino loop, display and touch stay on
  // core 1, so a slow handshake never stalls them.
  xTaskCreatePinnedToCore(taskEntry, "weather", 12288, this, 1, nullptr, 0);
}

void WeatherService::taskEntry(void* arg) { static_cast<WeatherService*>(arg)->run(); }

bool WeatherService::location(float& lat, float& lon) {
  const Settings& cfg = settings();
  if (isnan(cfg.nightLat) || isnan(cfg.nightLon)) return false;
  lat = cfg.nightLat;
  lon = cfg.nightLon;
  return true;
}

bool WeatherService::snapshot(WxData& out) const {
  if (lock_ == nullptr) return false;
  bool ok = false;
  if (xSemaphoreTake(lock_, pdMS_TO_TICKS(50)) == pdTRUE) {
    if (data_.valid) {
      out = data_;
      ok = true;
    }
    xSemaphoreGive(lock_);
  }
  return ok;
}

uint32_t WeatherService::ageMs() const {
  if (!data_.valid) return UINT32_MAX;
  return millis() - data_.fetchedMs;
}

void WeatherService::run() {
  uint32_t lastTry = 0;
  bool tried = false;
  float triedLat = 0, triedLon = 0;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(2000));
    float lat = 0, lon = 0;
    if (!location(lat, lon)) {
      state_ = WxState::NoLocation;
      continue;
    }
    if (WiFi.status() != WL_CONNECTED || !clockReady() || millis() < kBootGraceMs) {
      if (!data_.valid) state_ = WxState::Waiting;
      continue;
    }
    uint32_t now = millis();
    bool moved = data_.valid && (fabsf(lat - fetchedLat_) > 0.01f || fabsf(lon - fetchedLon_) > 0.01f);
    bool stale = !data_.valid || now - data_.fetchedMs >= kRefreshMs;
    if (!(stale || moved || refreshWanted_)) continue;
    // After a failure wait longer each time, up to ten minutes.
    uint32_t backoff = failures_ == 0 ? 0 : (30000UL << (failures_ > 5 ? 5 : failures_ - 1));
    if (backoff > 600000UL) backoff = 600000UL;
    // A place we have not tried yet, or an explicit request, skips the failure backoff. It never
    // skips the minimum gap. "moved" alone must not skip the backoff: if the fetch for a new
    // place keeps failing it stays true, and the task would retry every few seconds.
    bool newPlace = !tried || fabsf(lat - triedLat) > 0.01f || fabsf(lon - triedLon) > 0.01f;
    if (tried && now - lastTry < kMinGapMs) continue;
    if (tried && !newPlace && !refreshWanted_ && now - lastTry < backoff) continue;
    lastTry = now;
    triedLat = lat;
    triedLon = lon;
    tried = true;
    refreshWanted_ = false;
    if (!data_.valid) state_ = WxState::Loading;
    if (fetch(lat, lon)) {
      failures_ = 0;
    } else {
      if (failures_ < 250) failures_++;
      if (!data_.valid) state_ = WxState::Failed;
    }
  }
}

bool WeatherService::fetch(float lat, float lon) {
  uint32_t start = millis();
  char head[420];
  snprintf(head, sizeof(head),
           "%s?latitude=%.4f&longitude=%.4f"
           "&current=temperature_2m,apparent_temperature,relative_humidity_2m,is_day,weather_code,wind_speed_10m"
           "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,sunrise,sunset"
           "&timezone=auto&forecast_days=%d&timeformat=unixtime",
           kForecastUrl, lat, lon, kWxDays);
  String body;
  int status = 0;
  if (!httpGetJson(String(head), body, status)) {
    evlog("WEATHER fail http=%d after %lums", status, (unsigned long)(millis() - start));
    return false;
  }

  JsonDocument filter;
  filter["utc_offset_seconds"] = true;
  filter["timezone"] = true;
  JsonObject fc = filter["current"].to<JsonObject>();
  fc["temperature_2m"] = true;
  fc["apparent_temperature"] = true;
  fc["relative_humidity_2m"] = true;
  fc["is_day"] = true;
  fc["weather_code"] = true;
  fc["wind_speed_10m"] = true;
  JsonObject fd = filter["daily"].to<JsonObject>();
  fd["time"] = true;
  fd["weather_code"] = true;
  fd["temperature_2m_max"] = true;
  fd["temperature_2m_min"] = true;
  fd["precipitation_probability_max"] = true;
  fd["sunrise"] = true;
  fd["sunset"] = true;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body, DeserializationOption::Filter(filter));
  if (err) {
    evlog("WEATHER parse error %s", err.c_str());
    return false;
  }
  JsonObjectConst cur = doc["current"];
  if (cur.isNull() || !cur["temperature_2m"].is<float>()) {
    evlog("WEATHER no current data");
    return false;
  }

  WxData next;
  next.valid = true;
  next.fetchedMs = millis();
  next.tempC = cur["temperature_2m"].as<float>();
  next.feelsC = cur["apparent_temperature"] | next.tempC;
  next.humidity = (uint8_t)(cur["relative_humidity_2m"] | 0);
  next.windKmh = cur["wind_speed_10m"] | 0.0f;
  next.code = (uint8_t)(cur["weather_code"] | 0);
  next.isDay = (cur["is_day"] | 1) != 0;
  next.utcOffset = doc["utc_offset_seconds"] | 0;
  strlcpy(next.zone, doc["timezone"] | "", sizeof(next.zone));

  JsonObjectConst daily = doc["daily"];
  JsonArrayConst time = daily["time"];
  JsonArrayConst code = daily["weather_code"];
  JsonArrayConst tmax = daily["temperature_2m_max"];
  JsonArrayConst tmin = daily["temperature_2m_min"];
  JsonArrayConst rain = daily["precipitation_probability_max"];
  JsonArrayConst rise = daily["sunrise"];
  JsonArrayConst set = daily["sunset"];
  if (!rise.isNull() && rise.size() > 0) next.sunrise = rise[0] | 0u;
  if (!set.isNull() && set.size() > 0) next.sunset = set[0] | 0u;
  int n = time.isNull() ? 0 : (int)time.size();
  if (n > kWxDays) n = kWxDays;
  for (int i = 0; i < n; i++) {
    WxDay& d = next.day[i];
    d.date = time[i] | 0u;
    d.code = (uint8_t)(code[i] | 0);
    d.maxC = tmax[i] | 0.0f;
    d.minC = tmin[i] | 0.0f;
    d.rain = rain[i].is<int>() ? (int8_t)rain[i].as<int>() : (int8_t)-1;
  }
  next.days = n;

  if (xSemaphoreTake(lock_, pdMS_TO_TICKS(500)) == pdTRUE) {
    data_ = next;
    fetchedLat_ = lat;
    fetchedLon_ = lon;
    version_++;
    xSemaphoreGive(lock_);
  }
  state_ = WxState::Ok;
  evlog("WEATHER ok %lums %.1fC code=%d days=%d", (unsigned long)(millis() - start), next.tempC, next.code, n);
  return true;
}

bool WeatherService::geocode(const char* query, float& lat, float& lon, char* label, size_t labelSize) {
  if (query == nullptr || query[0] == 0) return false;
  char enc[96];
  urlEncode(query, enc, sizeof(enc));
  // The text goes through as typed and the first match is used. "City, State" ("Foster City, CA"
  // or "Foster City, California") makes the geocoder rank the right place first. A bare name is
  // ambiguous ("Foster City" finds Michigan first), so the portal asks for the state.
  String url = String(kGeocodeUrl) + "?name=" + enc + "&count=1&language=en&format=json";
  String body;
  int status = 0;
  if (!httpGetJson(url, body, status)) {
    evlog("GEOCODE fail http=%d", status);
    return false;
  }
  JsonDocument filter;
  filter["results"][0]["name"] = true;
  filter["results"][0]["admin1"] = true;
  filter["results"][0]["latitude"] = true;
  filter["results"][0]["longitude"] = true;
  JsonDocument doc;
  if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) return false;
  JsonObjectConst hit = doc["results"][0];
  if (hit.isNull() || !hit["latitude"].is<float>() || !hit["longitude"].is<float>()) {
    evlog("GEOCODE no match for the query");
    return false;
  }
  lat = hit["latitude"].as<float>();
  lon = hit["longitude"].as<float>();
  if (label != nullptr && labelSize > 0) {
    const char* name = hit["name"] | "";
    const char* region = hit["admin1"] | "";
    if (region[0]) snprintf(label, labelSize, "%s, %s", name, region);
    else strlcpy(label, name, labelSize);
  }
  evlog("GEOCODE ok %.3f,%.3f", lat, lon);
  return true;
}
