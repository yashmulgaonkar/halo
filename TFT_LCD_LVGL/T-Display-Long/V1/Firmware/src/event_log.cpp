#include "event_log.h"

#include <Preferences.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <stdio.h>
#include <string.h>

#include "halo_version.h"
#include "touch_in.h"

namespace {
constexpr size_t kRingBytes = 3584;
constexpr uint32_t kMagic = 0x4C564548;  // "HEVL"
constexpr uint32_t kHeapEveryMs = 60000;
constexpr uint32_t kSaveMinGapMs = 60000;
constexpr size_t kSaveBytes = 2048;

struct Ring {
  uint32_t magic;
  uint32_t boots;
  uint32_t head;   // next write position
  uint32_t total;  // bytes written since the ring was created, caps the dump
  char data[kRingBytes];
};

RTC_NOINIT_ATTR Ring g_ring;
portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;
bool g_ready = false;
bool g_dirty = false;      // notable lines not yet saved to flash
bool g_saveNow = false;
uint32_t g_lastSaveMs = 0;
uint32_t g_lastHeapMs = 0;
uint32_t g_heapLines = 0;

const char* resetName(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON: return "power-on";
    case ESP_RST_EXT: return "external";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "PANIC";
    case ESP_RST_INT_WDT: return "INT-WDT";
    case ESP_RST_TASK_WDT: return "TASK-WDT";
    case ESP_RST_WDT: return "WDT";
    case ESP_RST_DEEPSLEEP: return "deep-sleep";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_SDIO: return "sdio";
    default: return "unknown";
  }
}

void ringWrite(const char* line, size_t n) {
  portENTER_CRITICAL(&g_mux);
  for (size_t i = 0; i < n; i++) {
    g_ring.data[g_ring.head] = line[i];
    g_ring.head = (g_ring.head + 1) % kRingBytes;
  }
  g_ring.total += n;
  portEXIT_CRITICAL(&g_mux);
}

// Copies the ring oldest-first into out (at most max bytes, newest kept).
size_t ringRead(char* out, size_t max) {
  portENTER_CRITICAL(&g_mux);
  size_t have = g_ring.total < kRingBytes ? g_ring.total : kRingBytes;
  size_t take = have < max ? have : max;
  size_t start = (g_ring.head + kRingBytes - take) % kRingBytes;
  for (size_t i = 0; i < take; i++) out[i] = g_ring.data[(start + i) % kRingBytes];
  portEXIT_CRITICAL(&g_mux);
  return take;
}

void emit(bool store, bool notable, const char* fmt, va_list ap) {
  char body[120];
  vsnprintf(body, sizeof(body), fmt, ap);
  char line[140];
  int n = snprintf(line, sizeof(line), "%lu %s\n", (unsigned long)millis(), body);
  if (n <= 0) return;
  if ((size_t)n >= sizeof(line)) n = sizeof(line) - 1;
  Serial.write((const uint8_t*)"L ", 2);
  Serial.write((const uint8_t*)line, n);
  if (!g_ready || !store) return;
  ringWrite(line, n);
  if (notable) g_dirty = true;
}

void saveToFlash() {
  static char tail[kSaveBytes];
  size_t n = ringRead(tail, sizeof(tail) - 1);
  // Start at a line boundary so the saved text is not cut mid-line.
  size_t skip = 0;
  if (n == sizeof(tail) - 1) {
    while (skip < n && tail[skip] != '\n') skip++;
    if (skip < n) skip++;
  }
  Preferences p;
  if (p.begin("evlog", false)) {
    p.putBytes("tail", tail + skip, n - skip);
    p.end();
  }
  g_dirty = false;
  g_saveNow = false;
  g_lastSaveMs = millis();
}
}  // namespace

void evlogBegin() {
  esp_reset_reason_t reason = esp_reset_reason();
  bool valid = g_ring.magic == kMagic && g_ring.head < kRingBytes;
  // RTC memory is cleared on power-on, so only trust it after other resets.
  if (reason == ESP_RST_POWERON || reason == ESP_RST_BROWNOUT) valid = false;
  if (!valid) {
    memset(&g_ring, 0, sizeof(g_ring));
    g_ring.magic = kMagic;
  }
  g_ring.boots++;
  g_ready = true;
  evlog("BOOT #%lu %s reset=%s heap=%u", (unsigned long)g_ring.boots, HALO_FW_VERSION, resetName(reason),
        (unsigned)ESP.getFreeHeap());
  // A reset with the loop in a bad state is the interesting case. Save it at
  // once, because the next crash would otherwise overwrite the evidence.
  if (reason != ESP_RST_POWERON && reason != ESP_RST_SW && reason != ESP_RST_EXT) g_saveNow = true;
}

void evlog(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  emit(true, true, fmt, ap);
  va_end(ap);
}

void evlogTrace(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  emit(false, false, fmt, ap);
  va_end(ap);
}

void evlogTick() {
  uint32_t now = millis();
  if (now - g_lastHeapMs >= kHeapEveryMs) {
    g_lastHeapMs = now;
    g_heapLines++;
    TouchStats t = touchStats();
    // Every fifth line goes into the ring; the rest only echo to serial.
    bool store = (g_heapLines % 5) == 0;
    char line[120];
    snprintf(line, sizeof(line), "HEAP free=%u min=%u big=%u stack=%u touch c=%lu s=%lu g=%lu k=%lu", (unsigned)ESP.getFreeHeap(),
             (unsigned)ESP.getMinFreeHeap(), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
             (unsigned)uxTaskGetStackHighWaterMark(nullptr), (unsigned long)t.contacts, (unsigned long)t.swipes,
             (unsigned long)t.ghosts, (unsigned long)t.stuck);
    if (store) evlog("%s", line);
    else evlogTrace("%s", line);
  }
  if (g_saveNow || (g_dirty && now - g_lastSaveMs >= kSaveMinGapMs)) saveToFlash();
}

void evlogDump(String& out) {
  static char buf[kRingBytes + 1];
  size_t n = ringRead(buf, kRingBytes);
  buf[n] = 0;
  out += "## ring (RTC memory, survives resets, oldest first)\n";
  out += buf;
  out += "\n## flash copy (survives power loss, written at most once a minute)\n";
  Preferences p;
  if (p.begin("evlog", true)) {
    size_t len = p.getBytesLength("tail");
    if (len > 0 && len <= kSaveBytes) {
      static char saved[kSaveBytes + 1];
      p.getBytes("tail", saved, len);
      saved[len] = 0;
      out += saved;
    } else {
      out += "(empty)\n";
    }
    p.end();
  }
}
