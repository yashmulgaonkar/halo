#include "touch_in.h"

#include <Arduino.h>
#include <Wire.h>
#include <lvgl.h>

#include "board_pins.h"
#include "event_log.h"

namespace {
uint8_t g_addr = 0;
int g_lastX = 0;
int g_lastY = 0;
int g_startX = 0;
int g_startY = 0;
bool g_tracking = false;
int g_swipe = 0;
uint32_t g_lastActivity = 0;
bool g_dragged = false;
int g_runX = 0;
int g_runY = 0;
int g_candX = 0;
int g_candY = 0;
uint8_t g_stable = 0;
volatile bool g_irqSeen = false;
uint32_t g_irqMs = 0;
uint32_t g_contactMs = 0;
uint32_t g_samples = 0;
bool g_heldLogged = false;
TouchStats g_stats;
constexpr int kSwipePx = 36;
constexpr int kStablePx = 24;
constexpr int kJumpPx = 70;
constexpr uint8_t kNeedStable = 3;
constexpr uint32_t kIrqFreshMs = 120;
// A press that does not move for this long is not a finger. A phantom contact
// was seen sitting at one spot for the whole run and, because a held contact
// ignores any jump away from it, it also blocked every real touch.
constexpr uint32_t kStuckMs = 3000;
constexpr int kDeadPx = 30;
constexpr uint32_t kDeadClearMs = 2000;
bool g_dead = false;
bool g_stuckPending = false;
int g_deadX = 0;
int g_deadY = 0;
uint32_t g_deadSeenMs = 0;

bool inDeadZone(int x, int y) {
  int dx = x - g_deadX;
  int dy = y - g_deadY;
  if (dx < 0) dx = -dx;
  if (dy < 0) dy = -dy;
  return dx <= kDeadPx && dy <= kDeadPx;
}

void IRAM_ATTR onTouchIrq() { g_irqSeen = true; }

// The IRQ line pulses low when a new point is ready. It does not stay low for
// the whole press, so a polled level check misses it. A recent pulse, or the
// line being low right now, means the following I2C read is a real contact.
bool contactHint() {
  if (g_irqSeen) {
    g_irqSeen = false;
    g_irqMs = millis();
  }
  if (digitalRead(PIN_TOUCH_IRQ) == LOW) g_irqMs = millis();
  return g_irqMs != 0 && (millis() - g_irqMs) < kIrqFreshMs;
}

bool axsPresent(uint8_t addr) {
  static const uint8_t cmd[11] = {0xB5, 0xAB, 0xA5, 0x5A, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00};
  Wire1.beginTransmission(addr);
  Wire1.write(cmd, sizeof(cmd));
  if (Wire1.endTransmission(false) != 0) return false;
  return Wire1.requestFrom(addr, (uint8_t)8) == 8;
}

bool readPoint(int& x, int& y) {
  if (g_addr == 0) return false;
  static const uint8_t cmd[11] = {0xB5, 0xAB, 0xA5, 0x5A, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00};
  Wire1.beginTransmission(g_addr);
  Wire1.write(cmd, sizeof(cmd));
  if (Wire1.endTransmission(false) != 0) return false;
  if (Wire1.requestFrom(g_addr, (uint8_t)8) != 8) return false;
  uint8_t d[8];
  for (int i = 0; i < 8; i++) d[i] = Wire1.read();
  if (d[0] != 0 || d[1] == 0 || d[1] > 2) return false;
  // The controller's X runs along the 640px glass and starts at the bottom.
  // Y runs across the 180px width. Swapping only when X is past 180 left the
  // lower part of the glass reporting a point near the top of the screen.
  int rawX = ((d[2] & 0x0F) << 8) | d[3];
  int rawY = ((d[4] & 0x0F) << 8) | d[5];
  x = rawY;
  y = (HALO_LCD_H - 1) - rawX;
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  if (x >= HALO_LCD_W) x = HALO_LCD_W - 1;
  if (y >= HALO_LCD_H) y = HALO_LCD_H - 1;
  return true;
}

void noteSwipe() {
  int dx = g_lastX - g_startX;
  int dy = g_lastY - g_startY;
  int adx = dx < 0 ? -dx : dx;
  int ady = dy < 0 ? -dy : dy;
  if (adx < kSwipePx || adx < ady) return;
  g_swipe = dx < 0 ? 1 : -1;
}

void holdPoint(lv_indev_data_t* data, bool pressed) {
  data->state = pressed ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
  data->point.x = g_lastX;
  data->point.y = g_lastY;
}

void releaseContact() {
  if (g_tracking) {
    int before = g_swipe;
    noteSwipe();
    g_stats.contacts++;
    if (g_swipe != 0 && g_swipe != before) g_stats.swipes++;
    evlog("TOUCH end %lums n=%lu dx=%d dy=%d swipe=%d", (unsigned long)(millis() - g_contactMs), (unsigned long)g_samples,
          g_lastX - g_startX, g_lastY - g_startY, g_swipe);
  } else if (g_stable > 0) {
    g_stats.ghosts++;  // some reads, but never a steady contact
  }
  g_tracking = false;
  g_stable = 0;
  g_heldLogged = false;
}

void readCb(lv_indev_drv_t*, lv_indev_data_t* data) {
  // The ignored spot is forgotten once nothing has been seen inside it for a
  // while, so a real finger there works again after the phantom goes away.
  if (g_dead && millis() - g_deadSeenMs > kDeadClearMs) {
    g_dead = false;
    evlog("TOUCH dead spot %d,%d cleared", g_deadX, g_deadY);
  }
  // Keep reading through a press. The chip only pulses IRQ, and a gap between
  // pulses used to look like a lift before three samples could agree.
  if (!g_tracking && !contactHint()) {
    g_stable = 0;
    holdPoint(data, false);
    return;
  }
  int x = 0;
  int y = 0;
  if (!readPoint(x, y)) {
    releaseContact();
    holdPoint(data, false);
    return;
  }
  if (g_dead && inDeadZone(x, y)) {
    g_deadSeenMs = millis();
    g_tracking = false;
    g_stable = 0;
    holdPoint(data, false);
    return;
  }
  if (!g_tracking) {
    int dx = x - g_candX;
    int dy = y - g_candY;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    // Compare with the previous sample, not the first one. A quick swipe moves
    // well over kStablePx per sample along X, which used to restart the count
    // so the swipe was never tracked. Stray reads still jump in Y or reverse
    // by more than a finger can travel in one sample.
    if (g_stable == 0 || dx > kJumpPx || dy > kStablePx) {
      g_runX = x;
      g_runY = y;
      g_stable = 1;
    } else {
      g_stable++;
    }
    g_candX = x;
    g_candY = y;
    if (g_stable < kNeedStable) {
      holdPoint(data, false);
      return;
    }
    g_tracking = true;
    g_contactMs = millis();
    g_samples = 0;
    g_dragged = false;
    // Measure the swipe from where the finger landed.
    g_startX = g_runX;
    g_startY = g_runY;
  } else {
    int jx = x - g_lastX;
    int jy = y - g_lastY;
    if (jx < 0) jx = -jx;
    if (jy < 0) jy = -jy;
    if (jx > kJumpPx || jy > kJumpPx) {
      holdPoint(data, true);
      return;
    }
  }
  g_lastX = x;
  g_lastY = y;
  g_samples++;
  if (!g_heldLogged && millis() - g_contactMs > 4000) {
    g_heldLogged = true;
    evlog("TOUCH held %lums x=%d y=%d start=%d,%d", (unsigned long)(millis() - g_contactMs), x, y, g_startX, g_startY);
  }
  int dx = x - g_startX;
  int dy = y - g_startY;
  if (dx < 0) dx = -dx;
  if (dy < 0) dy = -dy;
  if (dx > 14 || dy > 14) g_dragged = true;
  if (!g_dragged && millis() - g_contactMs > kStuckMs) {
    g_dead = true;
    g_deadX = x;
    g_deadY = y;
    g_deadSeenMs = millis();
    g_stats.stuck++;
    g_stuckPending = true;
    evlog("TOUCH stuck %lums at %d,%d, ignoring that spot", (unsigned long)(millis() - g_contactMs), x, y);
    g_tracking = false;
    g_stable = 0;
    g_heldLogged = false;
    holdPoint(data, false);
    return;
  }
  g_lastActivity = millis();
  data->state = LV_INDEV_STATE_PR;
  data->point.x = x;
  data->point.y = y;
}
}  // namespace

void touchNoteActivity() { g_lastActivity = millis(); }

int touchTakeSwipe() {
  int swipe = g_swipe;
  g_swipe = 0;
  return swipe;
}

bool touchDragged() { return g_dragged; }

bool touchTakeStuck() {
  bool pending = g_stuckPending;
  g_stuckPending = false;
  return pending;
}

TouchStats touchStats() { return g_stats; }

uint32_t touchLastActivityMs() { return g_lastActivity; }

void touchBegin() {
  Wire1.begin(PIN_TOUCH_SDA, PIN_TOUCH_SCL);
  Wire1.setClock(100000);
  pinMode(PIN_TOUCH_IRQ, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_TOUCH_IRQ), onTouchIrq, FALLING);

  Serial.println("Touch bus scan:");
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire1.beginTransmission(addr);
    if (Wire1.endTransmission() == 0) Serial.printf("  0x%02X\n", addr);
  }

  const uint8_t prefer[] = {0x3B, 0x3C, 0x5A, 0x15};
  for (uint8_t addr : prefer) {
    if (axsPresent(addr)) {
      g_addr = addr;
      break;
    }
  }
  if (g_addr == 0) {
    for (uint8_t addr = 1; addr < 127; addr++) {
      if (addr == 0x6A) continue;  // SY6970 PMU shares this bus.
      if (axsPresent(addr)) {
        g_addr = addr;
        break;
      }
    }
  }
  if (g_addr) Serial.printf("Touch AXS15231 at 0x%02X\n", g_addr);
  else Serial.println("Touch controller not found");

  static lv_indev_drv_t indev;
  lv_indev_drv_init(&indev);
  indev.type = LV_INDEV_TYPE_POINTER;
  indev.read_cb = readCb;
  indev.long_press_time = 600;
  lv_indev_drv_register(&indev);
}
