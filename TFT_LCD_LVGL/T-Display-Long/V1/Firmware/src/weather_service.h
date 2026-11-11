#pragma once

#include <stddef.h>
#include <stdint.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Weather from Open-Meteo (https://open-meteo.com), key-less, CC BY 4.0. The
// screen must show the credit. Values are kept in metric and converted when
// drawn, so changing the temperature unit does not need a new fetch.
constexpr int kWxDays = 6;  // today plus five

struct WxDay {
  uint32_t date;  // local midnight, seconds as served (UTC epoch of the local day start)
  uint8_t code;   // WMO weather code
  float maxC;
  float minC;
  int8_t rain;    // chance of precipitation in percent, -1 when unknown
};

struct WxData {
  bool valid = false;
  uint32_t fetchedMs = 0;
  float tempC = 0;
  float feelsC = 0;
  uint8_t humidity = 0;
  float windKmh = 0;
  uint8_t code = 0;
  bool isDay = true;
  int32_t utcOffset = 0;
  uint32_t sunrise = 0;
  uint32_t sunset = 0;
  char zone[40] = "";
  int days = 0;
  WxDay day[kWxDays] = {};
};

enum class WxState : uint8_t { NoLocation, Waiting, Loading, Ok, Failed };

class WeatherService {
 public:
  void begin();
  // True once the first fetch has succeeded. Copies the data out under a lock.
  bool snapshot(WxData& out) const;
  // Changes whenever new data lands, so the UI knows when to redraw.
  uint32_t version() const { return version_; }
  WxState state() const { return state_; }
  uint32_t ageMs() const;
  // Ask for a fetch soon, for example after the location changed.
  void requestRefresh() { refreshWanted_ = true; }
  // The saved location, or false when none has been set.
  static bool location(float& lat, float& lon);
  // Looks up a city or postal code (Open-Meteo geocoding). Blocks for a few
  // seconds, so call it from a web handler and not from the UI loop.
  static bool geocode(const char* query, float& lat, float& lon, char* label, size_t labelSize);

 private:
  static void taskEntry(void* arg);
  void run();
  bool fetch(float lat, float lon);

  SemaphoreHandle_t lock_ = nullptr;
  WxData data_;
  volatile uint32_t version_ = 0;
  volatile WxState state_ = WxState::Waiting;
  volatile bool refreshWanted_ = false;
  float fetchedLat_ = 0;
  float fetchedLon_ = 0;
  uint8_t failures_ = 0;
};

WeatherService& weather();
