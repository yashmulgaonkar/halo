#pragma once

#include "readings.h"

#include <stddef.h>
#include <stdint.h>

// Offline ring of the same 15 fields the ESPHome history package stored.
// Samples are taken only while MQTT is down, then drained after reconnect.
class HistoryBuffer {
 public:
  void sample(const Readings& readings, uint32_t epoch, uint16_t intervalS, float days, bool enabled);
  void startFlush();
  bool flushPending() const { return flushActive_ && count_ > flushSent_; }
  bool nextBatch(char* out, size_t cap);
  void commitBatch();
  uint16_t count() const { return count_; }
  uint16_t capacity() const { return cap_; }
  int fillPercent() const;

 private:
  struct Sample {
    uint32_t epoch;
    float v[15];
  };

  void ensure(uint16_t cap);
  uint16_t oldestIndex() const;

  Sample* data_ = nullptr;
  uint16_t cap_ = 0;
  uint16_t head_ = 0;
  uint16_t count_ = 0;
  uint16_t flushSent_ = 0;
  uint16_t pendingBatch_ = 0;
  bool flushActive_ = false;
  uint32_t lastSampleMs_ = 0;
};
