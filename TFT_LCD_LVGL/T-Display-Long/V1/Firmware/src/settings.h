#pragma once

#include <stdint.h>

// Persisted setup. Temperature unit values match the ESPHome select options.
enum TempUnit : uint8_t { TEMP_F = 0, TEMP_C = 1, TEMP_K = 2 };

struct Settings {
  char wifiSsid[33];
  char wifiPass[65];
  char mqttHost[64];
  uint16_t mqttPort;
  char mqttUser[33];
  char mqttPass[65];
  char tz[48];
  uint8_t tempUnit;
  bool clock24h;
  float sen55TempOffset;
  float sen55HumOffset;
  float no2Offset;
  float tempBias;
  float humBias;
  float gasBias[7];
  bool co2Asc;
  bool startupBlink;
  uint8_t backlight;
  bool historyEnabled;
  uint16_t historyIntervalS;
  float historyDays;
  bool pageRotation;
  uint16_t pageIntervalS;

  void defaults();
  void load();
  void save() const;

  const char* tempUnitName() const;
  void setTempUnitName(const char* name);
};

Settings& settings();
