#pragma once

// On-screen AQI uses the same PM2.5 piecewise map as the ESPHome UI.
// NowCast uses EPA hourly weighting, then standard PM2.5 / PM10 breakpoints,
// and keeps the higher index — the same split the ESPHome build exposes
// as "Computed AQI" versus "NowCast AQI".

bool haloDisplayAqi(float pm25, int& aqi, bool& evacuate);
const char* aqiCategory(int aqi, bool evacuate);

// newestFirst[0] is the most recent hour. Needs 12 hours.
bool haloNowcastAqi(const float* pm25NewestFirst, const float* pm10NewestFirst, int hours, int& aqi);

const char* vocQualityLabel(float vocIndex);
