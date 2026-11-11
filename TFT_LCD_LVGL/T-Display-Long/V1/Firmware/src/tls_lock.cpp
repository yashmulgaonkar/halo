#include "tls_lock.h"

#include <Arduino.h>
#include <freertos/semphr.h>

namespace {
SemaphoreHandle_t mutex() {
  // Function-local static: created once, safe to call from any task.
  static SemaphoreHandle_t m = xSemaphoreCreateRecursiveMutex();
  return m;
}
}  // namespace

bool tlsLock(uint32_t waitMs) {
  SemaphoreHandle_t m = mutex();
  if (m == nullptr) return true;  // could not create it; do not block TLS
  return xSemaphoreTakeRecursive(m, pdMS_TO_TICKS(waitMs)) == pdTRUE;
}

void tlsUnlock() {
  SemaphoreHandle_t m = mutex();
  if (m != nullptr) xSemaphoreGiveRecursive(m);
}
