#pragma once

#include <stdint.h>

struct Readings {
  uint32_t updatedMs = 0;

  bool scdPresent = false;
  bool senPresent = false;
  bool bmePresent = false;
  bool micsPresent = false;
  bool micsReady = false;

  bool co2Ok = false;
  bool senOk = false;
  bool noxOk = false;
  bool bmeOk = false;
  bool micsOk = false;

  float co2 = 0;
  float pm1 = 0;
  float pm25 = 0;
  float pm4 = 0;
  float pm10 = 0;
  float senTempC = 0;
  float senHum = 0;
  float voc = 0;
  float nox = 0;
  char senName[16] = {};
  float bmeTempC = 0;
  float bmeHum = 0;
  float bmePressureHpa = 0;
  float co = 0;
  float no2 = 0;
  float h2 = 0;
  float ch4 = 0;
  float ethanol = 0;
  float nh3 = 0;

  int aqi = 0;
  bool aqiOk = false;
  bool aqiEvac = false;
  int nowcast = 0;
  bool nowcastOk = false;
  const char* vocQuality = "--";
};
