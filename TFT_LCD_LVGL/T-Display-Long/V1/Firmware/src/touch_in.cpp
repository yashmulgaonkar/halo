#include "touch_in.h"

#include <Arduino.h>
#include <Wire.h>
#include <lvgl.h>

#include "board_pins.h"

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
constexpr int kSwipePx = 36;

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
  if (d[0] != 0 || d[1] == 0) return false;
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

void readCb(lv_indev_drv_t*, lv_indev_data_t* data) {
  int x = 0;
  int y = 0;
  if (readPoint(x, y)) {
    if (!g_tracking) {
      g_tracking = true;
      g_dragged = false;
      g_startX = x;
      g_startY = y;
    }
    g_lastX = x;
    g_lastY = y;
    int dx = x - g_startX;
    int dy = y - g_startY;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    if (dx > 14 || dy > 14) g_dragged = true;
    g_lastActivity = millis();
    data->state = LV_INDEV_STATE_PR;
    data->point.x = x;
    data->point.y = y;
  } else {
    if (g_tracking) noteSwipe();
    g_tracking = false;
    data->state = LV_INDEV_STATE_REL;
    data->point.x = g_lastX;
    data->point.y = g_lastY;
  }
}
}  // namespace

void touchNoteActivity() { g_lastActivity = millis(); }

int touchTakeSwipe() {
  int swipe = g_swipe;
  g_swipe = 0;
  return swipe;
}

bool touchDragged() { return g_dragged; }

uint32_t touchLastActivityMs() { return g_lastActivity; }

void touchBegin() {
  Wire1.begin(PIN_TOUCH_SDA, PIN_TOUCH_SCL);
  Wire1.setClock(100000);
  pinMode(PIN_TOUCH_IRQ, INPUT);

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
  else Serial.println("Touch controller not found; BOOT button still changes pages");

  static lv_indev_drv_t indev;
  lv_indev_drv_init(&indev);
  indev.type = LV_INDEV_TYPE_POINTER;
  indev.read_cb = readCb;
  lv_indev_drv_register(&indev);
}
