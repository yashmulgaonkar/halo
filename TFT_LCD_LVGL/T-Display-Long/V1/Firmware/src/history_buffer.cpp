#include "history_buffer.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "aqi.h"

namespace {
float finite(float v) { return isnan(v) ? 0.0f : v; }
}

void HistoryBuffer::ensure(uint16_t cap) {
  if (cap < 60) cap = 60;
  if (cap > 4500) cap = 4500;
  if (data_ != nullptr && cap_ == cap) return;
  if (data_ != nullptr) heap_caps_free(data_);
  data_ = static_cast<Sample*>(heap_caps_malloc(sizeof(Sample) * cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (data_ == nullptr) {
    data_ = static_cast<Sample*>(heap_caps_malloc(sizeof(Sample) * cap, MALLOC_CAP_8BIT));
  }
  cap_ = data_ ? cap : 0;
  head_ = 0;
  count_ = 0;
  flushSent_ = 0;
  pendingBatch_ = 0;
  flushActive_ = false;
}

uint16_t HistoryBuffer::oldestIndex() const {
  if (cap_ == 0 || count_ == 0) return 0;
  return (head_ + cap_ - count_) % cap_;
}

int HistoryBuffer::fillPercent() const {
  if (cap_ == 0) return 0;
  return (int)((count_ * 100) / cap_);
}

void HistoryBuffer::sample(const Readings& readings, uint32_t epoch, uint16_t intervalS, float days,
                           bool enabled) {
  if (!enabled || epoch < 1700000000UL) return;
  if (!readings.aqiOk) return;
  if (intervalS < 30) intervalS = 30;
  uint32_t now = millis();
  if (lastSampleMs_ != 0 && (now - lastSampleMs_) < (uint32_t)intervalS * 1000UL) return;

  uint16_t need = (uint16_t)(days * 86400.0f / (float)intervalS);
  ensure(need);
  if (data_ == nullptr) return;

  Sample& s = data_[head_];
  s.epoch = epoch;
  s.v[0] = (float)readings.aqi;
  s.v[1] = finite(readings.co2Ok ? readings.co2 : NAN);
  s.v[2] = finite(readings.micsOk ? readings.no2 : NAN);
  s.v[3] = finite(readings.micsOk ? readings.co : NAN);
  s.v[4] = finite(readings.micsOk ? readings.h2 : NAN);
  s.v[5] = finite(readings.micsOk ? readings.ch4 : NAN);
  s.v[6] = finite(readings.micsOk ? readings.ethanol : NAN);
  s.v[7] = finite(readings.micsOk ? readings.nh3 : NAN);
  s.v[8] = finite(readings.senOk ? readings.pm1 : NAN);
  s.v[9] = finite(readings.senOk ? readings.pm25 : NAN);
  s.v[10] = finite(readings.senOk ? readings.pm4 : NAN);
  s.v[11] = finite(readings.senOk ? readings.pm10 : NAN);
  s.v[12] = finite(readings.senOk ? readings.senTempC : NAN);
  s.v[13] = finite(readings.senOk ? readings.voc : NAN);
  s.v[14] = finite(readings.senOk ? readings.senHum : NAN);

  head_ = (head_ + 1) % cap_;
  if (count_ < cap_) count_++;
  if (flushActive_ && flushSent_ > 0) {
    // A new sample arrived only while offline, so flush is not active.
  }
  lastSampleMs_ = now;
}

void HistoryBuffer::startFlush() {
  if (count_ == 0) {
    flushActive_ = false;
    flushSent_ = 0;
    return;
  }
  flushActive_ = true;
  flushSent_ = 0;
  pendingBatch_ = 0;
}

void HistoryBuffer::commitBatch() {
  if (pendingBatch_ == 0) return;
  flushSent_ = (uint16_t)(flushSent_ + pendingBatch_);
  if (flushSent_ > count_) flushSent_ = count_;
  pendingBatch_ = 0;
  if (flushSent_ >= count_) {
    head_ = 0;
    count_ = 0;
    flushSent_ = 0;
    flushActive_ = false;
  }
}

bool HistoryBuffer::nextBatch(char* out, size_t cap) {
  pendingBatch_ = 0;
  if (!flushPending() || out == nullptr || cap < 16) return false;
  uint16_t remain = count_ - flushSent_;
  uint16_t batch = remain > 6 ? 6 : remain;
  uint16_t oldest = oldestIndex();
  size_t used = 0;
  out[used++] = '[';
  out[used] = 0;
  for (uint16_t i = 0; i < batch; i++) {
    const Sample& s = data_[(oldest + flushSent_ + i) % cap_];
    char entry[220];
    int n = snprintf(entry, sizeof(entry),
                     "%s{\"ts\":%lu,\"q\":%.0f,\"c2\":%.0f,\"n2\":%.2f,\"co\":%.2f,\"h2\":%.2f,"
                     "\"c4\":%.2f,\"et\":%.2f,\"n3\":%.2f,\"p1\":%.1f,\"p25\":%.1f,\"p4\":%.1f,"
                     "\"p10\":%.1f,\"tm\":%.1f,\"vc\":%.0f,\"rh\":%.0f}",
                     (i == 0 ? "" : ","), (unsigned long)s.epoch, s.v[0], s.v[1], s.v[2], s.v[3], s.v[4],
                     s.v[5], s.v[6], s.v[7], s.v[8], s.v[9], s.v[10], s.v[11], s.v[12], s.v[13], s.v[14]);
    if (n < 0 || used + (size_t)n + 2 >= cap) break;
    memcpy(out + used, entry, (size_t)n);
    used += (size_t)n;
    pendingBatch_++;
  }
  if (pendingBatch_ == 0) return false;
  if (used + 2 >= cap) return false;
  out[used++] = ']';
  out[used] = 0;
  return true;
}
