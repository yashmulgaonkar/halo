#include "pmu.h"

#include <Arduino.h>
#include <Wire.h>

#include "event_log.h"
#include "settings.h"

namespace {
constexpr uint8_t kAddr = 0x6A;

// Same values as LilyGO's T-Display-S3-Long example:
// REG00: EN_HIZ=0, EN_ILIM=0 (ignore ILIM pin), IINLIM=max (3.25 A).
constexpr uint8_t kReg00 = 0b00111111;
// REG09 bit 5 BATFET_DIS. LilyGO's example sets it (0x64) because the board usually
// runs without a battery. With a battery fitted it must stay clear, or the battery is
// cut off and never charges. Only that bit is touched.
constexpr uint8_t kBatfetDis = 0b00100000;
// REG07 bits 5:4 WATCHDOG. 00 disables the I2C watchdog, which would otherwise
// return the chip to default settings 40 s after the last write.
constexpr uint8_t kWdMask = 0b00110000;

bool g_online = false;
uint32_t g_lastCheckMs = 0;

bool readReg(uint8_t reg, uint8_t& value) {
  Wire1.beginTransmission(kAddr);
  Wire1.write(reg);
  if (Wire1.endTransmission(false) != 0) return false;
  if (Wire1.requestFrom(kAddr, (uint8_t)1) != 1) return false;
  value = Wire1.read();
  return true;
}

bool writeReg(uint8_t reg, uint8_t value) {
  Wire1.beginTransmission(kAddr);
  Wire1.write(reg);
  Wire1.write(value);
  return Wire1.endTransmission() == 0;
}

// Returns true when every register already held the wanted value.
bool apply(bool& ok) {
  ok = true;
  bool clean = true;
  uint8_t v = 0;

  if (!readReg(0x07, v)) ok = false;
  else if (v & kWdMask) {
    clean = false;
    ok &= writeReg(0x07, v & ~kWdMask);
  }
  if (!readReg(0x00, v)) ok = false;
  else if (v != kReg00) {
    clean = false;
    ok &= writeReg(0x00, kReg00);
  }
  if (!readReg(0x09, v)) ok = false;
  else {
    uint8_t want = settings().batteryFitted ? (v & ~kBatfetDis) : (v | kBatfetDis);
    if (v != want) {
      clean = false;
      ok &= writeReg(0x09, want);
    }
  }
  return clean;
}

void logStatus(const char* tag) {
  uint8_t r00 = 0, r07 = 0, r09 = 0, r0b = 0, r0c = 0;
  readReg(0x00, r00);
  readReg(0x07, r07);
  readReg(0x09, r09);
  readReg(0x0B, r0b);
  readReg(0x0C, r0c);
  // REG0C is latched: read it twice so the second value is the current fault state.
  readReg(0x0C, r0c);
  evlog("PMU %s r00=%02X r07=%02X r09=%02X stat=%02X fault=%02X", tag, r00, r07, r09, r0b, r0c);
  Serial.printf("PMU %s r00=%02X r07=%02X r09=%02X stat=%02X fault=%02X\n", tag, r00, r07, r09, r0b,
                r0c);
}
}  // namespace

void pmuBegin() {
  uint8_t v = 0;
  if (!readReg(0x00, v)) {
    evlog("PMU absent");
    Serial.println("PMU SY6970 not found");
    return;
  }
  g_online = true;
  uint8_t before00 = v, before09 = 0, after09 = 0;
  readReg(0x09, before09);
  bool ok = false;
  apply(ok);
  readReg(0x09, after09);
  evlog("PMU set battery=%s r00 %02X>%02X r09 %02X>%02X %s", settings().batteryFitted ? "yes" : "no", before00,
        kReg00, before09, after09, ok ? "ok" : "FAIL");
  logStatus("boot");
  g_lastCheckMs = millis();
}

void pmuLoop() {
  if (!g_online) return;
  uint32_t now = millis();
  if (now - g_lastCheckMs < 30000) return;
  g_lastCheckMs = now;
  bool ok = false;
  if (!apply(ok)) logStatus(ok ? "reapplied" : "reapply-FAIL");
}
