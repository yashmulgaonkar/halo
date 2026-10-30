#include "sensor_hub.h"

#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include <string.h>

#include <Adafruit_BME280.h>
#include <SensirionI2CSen5x.h>
#include <SensirionI2cScd4x.h>

#include "aqi.h"
#include "board_pins.h"

namespace {
SensorHub g_sensors;
SensirionI2cScd4x g_scd;
SensirionI2CSen5x g_sen;
Adafruit_BME280 g_bme;
constexpr uint8_t kMicsAddr = 0x75;

float clampHum(float h) {
  if (h < 0) return 0;
  if (h > 100) return 100;
  return h;
}
}  // namespace

SensorHub& sensors() { return g_sensors; }

bool SensorHub::probe(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

bool SensorHub::micsWrite(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(kMicsAddr);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool SensorHub::micsRead(uint8_t reg, uint8_t* data, size_t len) {
  Wire.beginTransmission(kMicsAddr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(kMicsAddr, (uint8_t)len) != (int)len) return false;
  for (size_t i = 0; i < len; i++) data[i] = Wire.read();
  return true;
}

void SensorHub::begin() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(50000);
  delay(50);

  Serial.println("Sensor bus scan:");
  for (uint8_t addr = 1; addr < 127; addr++) {
    if (probe(addr)) Serial.printf("  0x%02X\n", addr);
  }

  readings_.scdPresent = probe(0x62);
  readings_.senPresent = probe(0x69);
  readings_.bmePresent = probe(0x76) || probe(0x77);
  readings_.micsPresent = probe(kMicsAddr);

  if (readings_.senPresent) {
    g_sen.begin(Wire);
    uint16_t err = g_sen.deviceReset();
    delay(200);
    unsigned char rawName[32] = {};
    err = g_sen.getProductName(rawName, sizeof(rawName) - 1);
    if (!err && rawName[0]) {
      strncpy(readings_.senName, (const char*)rawName, sizeof(readings_.senName) - 1);
      Serial.printf("Particle sensor %s\n", readings_.senName);
    } else {
      Serial.printf("Particle sensor name failed: %u\n", err);
    }
    // Sensirion SEN5x engineering-guide defaults used by the ESPHome profile.
    err = g_sen.setVocAlgorithmTuningParameters(100, 72, 72, 180, 50, 230);
    if (err) Serial.printf("SEN55 VOC tuning failed: %u\n", err);
    err = g_sen.startMeasurement();
    if (err) {
      Serial.printf("SEN55 start failed: %u\n", err);
      readings_.senPresent = false;
    } else {
      Serial.println("SEN55 measuring");
    }
  } else {
    Serial.println("SEN55 not found");
  }

  if (readings_.scdPresent) {
    g_scd.begin(Wire, 0x62);
    g_scd.stopPeriodicMeasurement();
    delay(500);
    uint16_t asc = settings().co2Asc ? 1 : 0;
    int16_t err = g_scd.setAutomaticSelfCalibrationEnabled(asc);
    if (err) Serial.printf("SCD4x ASC write failed: %d\n", err);
    err = g_scd.startPeriodicMeasurement();
    if (err) {
      Serial.printf("SCD4x start failed: %d\n", err);
      readings_.scdPresent = false;
    } else {
      Serial.printf("SCD4x measuring, ASC %s\n", asc ? "on" : "off");
    }
  } else {
    Serial.println("SCD4x not found");
  }

  if (readings_.bmePresent) {
    uint8_t addr = probe(0x76) ? 0x76 : 0x77;
    if (!g_bme.begin(addr, &Wire)) {
      readings_.bmePresent = false;
      Serial.println("BME280 begin failed");
    } else {
      Serial.printf("BME280 at 0x%02X\n", addr);
    }
  } else {
    Serial.println("BME280 not found");
  }

  if (readings_.micsPresent) {
    uint8_t mode = 0;
    if (!micsRead(0x0A, &mode, 1)) {
      readings_.micsPresent = false;
      Serial.println("MiCS-4514 read failed");
    } else if (mode == 0x00) {
      micsWrite(0x0A, 0x01);
      micsReadyAtMs_ = millis() + 180000;
      Serial.println("MiCS-4514 waking, 3 min warmup");
    } else {
      micsReadyAtMs_ = millis();
      Serial.println("MiCS-4514 already awake");
    }
  } else {
    Serial.println("MiCS-4514 not found");
  }

  started_ = true;
}

void SensorHub::requestAsc(bool enable) {
  ascValue_ = enable;
  ascPending_ = true;
}

void SensorHub::requestCalibration(uint16_t ppm) {
  if (ppm < 400) ppm = 400;
  if (ppm > 2000) ppm = 2000;
  calPpm_ = ppm;
  calPending_ = true;
}

void SensorHub::requestFanClean() { cleanPending_ = true; }

void SensorHub::applyPending() {
  if (ascPending_ && readings_.scdPresent) {
    ascPending_ = false;
    g_scd.stopPeriodicMeasurement();
    delay(500);
    int16_t err = g_scd.setAutomaticSelfCalibrationEnabled(ascValue_ ? 1 : 0);
    delay(10);
    g_scd.startPeriodicMeasurement();
    Serial.printf("SCD4x ASC %s (%d)\n", ascValue_ ? "on" : "off", err);
  } else if (ascPending_) {
    ascPending_ = false;
  }

  if (calPending_ && readings_.scdPresent) {
    calPending_ = false;
    g_scd.stopPeriodicMeasurement();
    delay(500);
    uint16_t correction = 0;
    int16_t err = g_scd.performForcedRecalibration(calPpm_, correction);
    delay(400);
    g_scd.startPeriodicMeasurement();
    Serial.printf("SCD4x forced cal to %u ppm, correction %u, err %d\n", calPpm_, correction, err);
  } else if (calPending_) {
    calPending_ = false;
  }

  if (cleanPending_ && readings_.senPresent) {
    cleanPending_ = false;
    uint16_t err = g_sen.startFanCleaning();
    Serial.printf("SEN55 fan clean (%u)\n", err);
  } else if (cleanPending_) {
    cleanPending_ = false;
  }
}

void SensorHub::readClimate() {
  readings_.bmeOk = false;
  if (!readings_.bmePresent) return;
  float t = g_bme.readTemperature();
  float h = g_bme.readHumidity();
  float pPa = g_bme.readPressure();
  if (isnan(t) || isnan(h) || isnan(pPa)) return;
  readings_.bmeTempC = t;
  readings_.bmeHum = h;
  readings_.bmePressureHpa = pPa / 100.0f;
  readings_.bmeOk = true;
  if (readings_.scdPresent && pPa >= 70000.0f && pPa <= 120000.0f) {
    g_scd.setAmbientPressure((uint32_t)(pPa + 0.5f));
  }
}

void SensorHub::readParticles() {
  readings_.co2Ok = false;
  readings_.senOk = false;

  if (readings_.scdPresent) {
    bool ready = false;
    int16_t err = g_scd.getDataReadyStatus(ready);
    if (!err && ready) {
      uint16_t co2 = 0;
      float t = 0;
      float h = 0;
      err = g_scd.readMeasurement(co2, t, h);
      if (!err && co2 > 0) {
        readings_.co2 = co2;
        readings_.co2Ok = true;
      }
    }
  }

  readings_.aqiOk = false;
  readings_.aqiEvac = false;
  if (readings_.senPresent) {
    float pm1, pm25, pm4, pm10, hum, temp, voc, nox;
    uint16_t err = g_sen.readMeasuredValues(pm1, pm25, pm4, pm10, hum, temp, voc, nox);
    if (!err && !isnan(pm25) && !isnan(temp)) {
      const Settings& cfg = settings();
      readings_.pm1 = pm1;
      readings_.pm25 = pm25;
      readings_.pm4 = pm4;
      readings_.pm10 = pm10;
      readings_.senTempC = temp - cfg.sen55TempOffset + cfg.tempBias;
      readings_.senHum = clampHum(hum - cfg.sen55HumOffset + cfg.humBias);
      readings_.voc = voc;
      appliedTemp_ = cfg.tempBias;
      appliedHum_ = cfg.humBias;
      // 0x7FFF from the sensor is "unknown". A SEN54 has no NOx channel, and a
      // SEN55 also returns that for the first 10 seconds after reset.
      if (!isnan(nox)) {
        readings_.nox = nox + cfg.gasBias[6];
        appliedGas_[6] = cfg.gasBias[6];
        readings_.noxOk = true;
      }
      readings_.vocQuality = vocQualityLabel(voc);
      readings_.senOk = true;
      readings_.aqiOk = haloDisplayAqi(pm25, readings_.aqi, readings_.aqiEvac);
    }
  }
}

void SensorHub::readGases() {
  readings_.micsOk = false;
  readings_.micsReady = false;
  if (!readings_.micsPresent) return;
  if ((int32_t)(millis() - micsReadyAtMs_) < 0) return;
  readings_.micsReady = true;

  uint8_t data[6];
  if (!micsRead(0x04, data, 6)) return;
  uint16_t ox = ((uint16_t)data[0] << 8) | data[1];
  uint16_t red = ((uint16_t)data[2] << 8) | data[3];
  uint16_t power = ((uint16_t)data[4] << 8) | data[5];

  if (!micsCalibrated_) {
    oxCal_ = (float)((int)power - (int)ox);
    redCal_ = (float)((int)power - (int)red);
    micsCalibrated_ = (oxCal_ != 0.0f && redCal_ != 0.0f);
    Serial.printf("MiCS cal ox=%.0f red=%.0f\n", oxCal_, redCal_);
    return;
  }

  float redF = (float)((int)power - (int)red) / redCal_;
  float oxF = (float)((int)power - (int)ox) / oxCal_;

  float co = 0;
  if (redF > 3.4f) co = 0;
  else if (redF < 0.01f) co = 1000;
  else co = 4.2f / powf(redF, 1.2f);

  float no2 = 0;
  if (oxF >= 0.3f) no2 = 0.164f * powf(oxF, 0.975f);
  no2 -= settings().no2Offset;

  float ch4 = 0;
  if (redF <= 0.9f && redF >= 0.5f) ch4 = 630.0f / powf(redF, 4.4f);

  float ethanol = 0;
  if (redF <= 1.0f && redF >= 0.02f) ethanol = 1.52f / powf(redF, 1.55f);

  float h2 = 0;
  if (redF <= 0.9f && redF >= 0.02f) h2 = 0.85f / powf(redF, 1.75f);

  float nh3 = 0;
  if (redF <= 0.98f && redF >= 0.2532f) nh3 = 0.9f / powf(redF, 4.6f);

  readings_.co = co + settings().gasBias[0];
  readings_.no2 = no2 + settings().gasBias[1];
  readings_.h2 = h2 + settings().gasBias[2];
  readings_.ch4 = ch4 + settings().gasBias[3];
  readings_.ethanol = ethanol + settings().gasBias[4];
  readings_.nh3 = nh3 + settings().gasBias[5];
  for (int i = 0; i < 6; i++) appliedGas_[i] = settings().gasBias[i];
  readings_.micsOk = true;
}

void SensorHub::syncBias() {
  const Settings& cfg = settings();
  if (readings_.senOk) {
    readings_.senTempC += cfg.tempBias - appliedTemp_;
    readings_.senHum = clampHum(readings_.senHum + (cfg.humBias - appliedHum_));
    if (readings_.noxOk) readings_.nox += cfg.gasBias[6] - appliedGas_[6];
  }
  if (readings_.micsOk) {
    float* vals[] = {&readings_.co, &readings_.no2, &readings_.h2, &readings_.ch4, &readings_.ethanol, &readings_.nh3};
    for (int i = 0; i < 6; i++) *vals[i] += cfg.gasBias[i] - appliedGas_[i];
  }
  appliedTemp_ = cfg.tempBias;
  appliedHum_ = cfg.humBias;
  for (int i = 0; i < 7; i++) appliedGas_[i] = cfg.gasBias[i];
  if (readings_.senOk || readings_.micsOk) readings_.updatedMs = millis();
}

void SensorHub::updateNowcast() {
  if (!readings_.senOk) return;
  uint32_t key;
  time_t now = time(nullptr);
  if (now > 1700000000) key = (uint32_t)(now / 3600);
  else key = millis() / 3600000UL;

  if (!hourOpen_) {
    hourKey_ = key;
    hourOpen_ = true;
  }
  if (key != hourKey_) {
    if (hourSamples_ > 0) {
      float a25 = hourSum25_ / hourSamples_;
      float a10 = hourSum10_ / hourSamples_;
      if (hourCount_ < kHours) {
        hourPm25_[hourCount_] = a25;
        hourPm10_[hourCount_] = a10;
        hourCount_++;
      } else {
        for (int i = 1; i < kHours; i++) {
          hourPm25_[i - 1] = hourPm25_[i];
          hourPm10_[i - 1] = hourPm10_[i];
        }
        hourPm25_[kHours - 1] = a25;
        hourPm10_[kHours - 1] = a10;
      }
    }
    hourKey_ = key;
    hourSum25_ = 0;
    hourSum10_ = 0;
    hourSamples_ = 0;
  }
  hourSum25_ += readings_.pm25;
  hourSum10_ += readings_.pm10;
  hourSamples_++;

  if (hourCount_ >= kHours) {
    float newest25[kHours];
    float newest10[kHours];
    for (int i = 0; i < kHours; i++) {
      newest25[i] = hourPm25_[kHours - 1 - i];
      newest10[i] = hourPm10_[kHours - 1 - i];
    }
    readings_.nowcastOk = haloNowcastAqi(newest25, newest10, kHours, readings_.nowcast);
  }
}

void SensorHub::poll(bool mqttConnected) {
  if (!started_) return;
  bool command = ascPending_ || calPending_ || cleanPending_;
  if (command) applyPending();
  uint32_t now = millis();
  if (lastPollMs_ != 0 && (now - lastPollMs_) < 10000) return;
  lastPollMs_ = now;

  readClimate();
  readParticles();
  readGases();
  updateNowcast();
  readings_.updatedMs = now;

  if (!mqttConnected) {
    time_t epoch = time(nullptr);
    const Settings& cfg = settings();
    history_.sample(readings_, (uint32_t)epoch, cfg.historyIntervalS, cfg.historyDays, cfg.historyEnabled);
  }
}
