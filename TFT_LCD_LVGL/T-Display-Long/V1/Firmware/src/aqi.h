#pragma once

#include <stdint.h>

// On-screen AQI uses the same PM2.5 piecewise map as the ESPHome UI.
// NowCast uses EPA hourly weighting, then standard PM2.5 / PM10 breakpoints,
// and keeps the higher index — the same split the ESPHome build exposes
// as "Computed AQI" versus "NowCast AQI".

bool haloDisplayAqi(float pm25, int& aqi, bool& evacuate);
const char* aqiCategory(int aqi, bool evacuate);

// Screen colors for the worse of the on-screen AQI and the CO2 band.
bool haloRingColor(bool aqiOk, int aqi, bool evacuate, bool co2Ok, float co2, uint8_t& r, uint8_t& g, uint8_t& b);

// newestFirst[0] is the most recent hour. Needs 12 hours.
bool haloNowcastAqi(const float* pm25NewestFirst, const float* pm10NewestFirst, int hours, int& aqi);

const char* vocQualityLabel(float vocIndex);
