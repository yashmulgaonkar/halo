#include "aqi.h"

#include <math.h>

namespace {

struct Breakpoint {
  float cLo;
  float cHi;
  int iLo;
  int iHi;
};

int indexFromBreakpoints(float c, const Breakpoint* table, int n) {
  if (c < 0) c = 0;
  for (int i = 0; i < n; i++) {
    if (c <= table[i].cHi || i == n - 1) {
      float span = table[i].cHi - table[i].cLo;
      if (span <= 0.01f) return table[i].iHi;
      float idx = (table[i].iHi - table[i].iLo) / span * (c - table[i].cLo) + table[i].iLo;
      if (idx < 0) idx = 0;
      if (idx > 500) idx = 500;
      return (int)(idx + 0.5f);
    }
  }
  return 500;
}

int epaPm25(float c) {
  static const Breakpoint kTable[] = {
      {0.0f, 9.0f, 0, 50},     {9.1f, 35.4f, 51, 100},  {35.5f, 55.4f, 101, 150},
      {55.5f, 125.4f, 151, 200}, {125.5f, 225.4f, 201, 300}, {225.5f, 325.4f, 301, 500},
  };
  return indexFromBreakpoints(c, kTable, 6);
}

int epaPm10(float c) {
  static const Breakpoint kTable[] = {
      {0, 54, 0, 50},     {55, 154, 51, 100},   {155, 254, 101, 150},
      {255, 354, 151, 200}, {355, 424, 201, 300}, {425, 604, 301, 500},
  };
  return indexFromBreakpoints(c, kTable, 6);
}

float nowcastConcentration(const float* newestFirst, int n) {
  float cMin = newestFirst[0];
  float cMax = newestFirst[0];
  for (int i = 1; i < n; i++) {
    if (newestFirst[i] < cMin) cMin = newestFirst[i];
    if (newestFirst[i] > cMax) cMax = newestFirst[i];
  }
  float weight = 1.0f;
  if (cMax > 0.0f) weight = 1.0f - (cMax - cMin) / cMax;
  if (weight < 0.5f) weight = 0.5f;
  float sum = 0;
  float wsum = 0;
  float w = 1.0f;
  for (int i = 0; i < n; i++) {
    sum += newestFirst[i] * w;
    wsum += w;
    w *= weight;
  }
  if (wsum <= 0) return newestFirst[0];
  return sum / wsum;
}

}  // namespace

bool haloDisplayAqi(float pm25, int& aqi, bool& evacuate) {
  evacuate = false;
  if (isnan(pm25)) return false;
  float v = pm25;
  float a = 0;
  if (v <= 9.0f) {
    a = ((50.0f - 0.0f) / (9.0f - 0.0f)) * (v - 0.0f) + 0.0f;
  } else if (v <= 35.4f) {
    a = ((100.0f - 51.0f) / (35.4f - 9.1f)) * (v - 9.1f) + 51.0f;
  } else if (v <= 55.4f) {
    a = ((150.0f - 101.0f) / (55.4f - 35.5f)) * (v - 35.5f) + 101.0f;
  } else if (v <= 125.4f) {
    a = ((200.0f - 151.0f) / (125.4f - 55.5f)) * (v - 55.5f) + 151.0f;
  } else if (v <= 225.4f) {
    a = ((300.0f - 201.0f) / (225.4f - 125.5f)) * (v - 125.5f) + 201.0f;
  } else if (v <= 500.4f) {
    a = ((500.0f - 301.0f) / (500.4f - 250.5f)) * (v - 250.5f) + 301.0f;
  } else {
    evacuate = true;
    aqi = 500;
    return true;
  }
  if (a < 0) a = 0;
  if (a > 500) a = 500;
  aqi = (int)a;
  return true;
}

const char* aqiCategory(int aqi, bool evacuate) {
  if (evacuate) return "Evacuate";
  if (aqi <= 50) return "Good";
  if (aqi <= 100) return "Moderate";
  if (aqi <= 150) return "Groups at risk";
  if (aqi <= 200) return "Unhealthy";
  if (aqi <= 300) return "Very unhealthy";
  return "Hazardous";
}

bool haloNowcastAqi(const float* pm25NewestFirst, const float* pm10NewestFirst, int hours, int& aqi) {
  if (pm25NewestFirst == nullptr || pm10NewestFirst == nullptr || hours < 12) return false;
  float pm25 = nowcastConcentration(pm25NewestFirst, 12);
  float pm10 = nowcastConcentration(pm10NewestFirst, 12);
  int a = epaPm25(pm25);
  int b = epaPm10(pm10);
  aqi = a > b ? a : b;
  return true;
}

const char* vocQualityLabel(float vocIndex) {
  if (isnan(vocIndex)) return "--";
  if (vocIndex < 80) return "Improved";
  if (vocIndex < 150) return "Normal";
  if (vocIndex < 250) return "Abnormal";
  if (vocIndex < 400) return "Very abnormal";
  return "Extremely abnormal";
}
