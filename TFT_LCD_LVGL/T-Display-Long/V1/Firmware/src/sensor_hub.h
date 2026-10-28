#pragma once

#include "history_buffer.h"
#include "readings.h"
#include "settings.h"

class SensorHub {
 public:
  void begin();
  void poll(bool mqttConnected);
  const Readings& latest() const { return readings_; }
  HistoryBuffer& history() { return history_; }

  void requestAsc(bool enable);
  void requestCalibration(uint16_t ppm);
  void requestFanClean();

 private:
  void applyPending();
  void readClimate();
  void readParticles();
  void readGases();
  void updateNowcast();
  bool probe(uint8_t address);
  bool micsWrite(uint8_t reg, uint8_t value);
  bool micsRead(uint8_t reg, uint8_t* data, size_t len);

  Readings readings_;
  HistoryBuffer history_;
  bool started_ = false;
  uint32_t lastPollMs_ = 0;
  uint32_t micsReadyAtMs_ = 0;
  bool micsCalibrated_ = false;
  float oxCal_ = 0;
  float redCal_ = 0;

  bool ascPending_ = false;
  bool ascValue_ = false;
  bool calPending_ = false;
  uint16_t calPpm_ = 420;
  bool cleanPending_ = false;

  static constexpr int kHours = 12;
  float hourPm25_[kHours] = {};
  float hourPm10_[kHours] = {};
  int hourCount_ = 0;
  uint32_t hourKey_ = 0;
  bool hourOpen_ = false;
  float hourSum25_ = 0;
  float hourSum10_ = 0;
  int hourSamples_ = 0;
};

SensorHub& sensors();
