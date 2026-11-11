#pragma once

#include <Arduino.h>
#include <stdarg.h>

// Small text event log kept in RTC memory, so it survives a crash, a watchdog
// reset or a software restart. Notable lines are also copied to flash (NVS) so
// the tail survives a power cycle. Read it back with GET /api/log, or watch the
// serial port: every line is echoed there as it is written.

void evlogBegin();
void evlog(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
// Echo to serial only. For periodic lines that would flood the ring.
void evlogTrace(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
// Periodic housekeeping: heap line, deferred flash save. Call from loop().
void evlogTick();
void evlogDump(String& out);

// Logs when a scope took longer than the threshold, for finding what blocks.
class EvScope {
 public:
  EvScope(const char* name, uint32_t limitMs) : name_(name), limitMs_(limitMs), start_(millis()) {}
  ~EvScope() {
    uint32_t took = millis() - start_;
    if (took >= limitMs_) evlog("SLOW %s %lums", name_, (unsigned long)took);
  }

 private:
  const char* name_;
  uint32_t limitMs_;
  uint32_t start_;
};
