#include <Arduino.h>
#include <WiFi.h>

#include "aqi.h"
#include "backlight.h"
#include "board_pins.h"
#include "display_port.h"
#include "event_log.h"
#include "halo_ui.h"
#include "halo_version.h"
#include "network_service.h"
#include "night_mode.h"
#include "pmu.h"
#include "ring_light.h"
#include "sensor_hub.h"
#include "settings.h"
#include "touch_in.h"
#include "weather_service.h"

namespace {
uint32_t bootDownAt = 0;
uint32_t lastLoopMs = 0;
int lastWifi = -1;
int lastMqtt = -1;

// Any loop stage that blocks this long is worth a line in the event log.
void noteStage(const char* name, uint32_t startUs) {
  uint32_t us = micros() - startUs;
  if (us >= 1000000UL) evlog("STALL %s %lums", name, (unsigned long)(us / 1000UL));
}
bool bootHoldFired = false;
}

void setup() {
  Serial.begin(115200);
  delay(250);
  evlogBegin();
  setCpuFrequencyMhz(240);
  Serial.printf("\n%s %s\n", HALO_FW_NAME, HALO_FW_VERSION);

  settings().load();
  backlight().begin();
  backlight().setLevel(settings().backlight);
  ring().begin();
  displayBegin();
  touchBegin();
  pmuBegin();
  ui().begin();
  displayPump();
  sensors().begin();
  network().begin();
  weather().begin();
  pinMode(PIN_BOOT, INPUT_PULLUP);
  displayPump();
}

void loop() {
  uint32_t loopNow = millis();
  if (lastLoopMs != 0 && loopNow - lastLoopMs >= 1500) evlog("LOOPGAP %lums", (unsigned long)(loopNow - lastLoopMs));
  lastLoopMs = loopNow;
  backlight().refresh();
  uint32_t mark = micros();
  network().loop();
  noteStage("net", mark);
  ring().markStage(1, micros() - mark);
  mark = micros();
  sensors().poll(network().mqttConnected());
  noteStage("sensor", mark);
  ring().markStage(2, micros() - mark);
  ring().loop(settings().startupBlink, network().mqttConnected(), network().wifiConnected());
  nightMode().loop();
  pmuLoop();
  const Readings& air = sensors().latest();
  uint8_t airR = 0;
  uint8_t airG = 0;
  uint8_t airB = 0;
  if (haloRingColor(air.aqiOk, air.aqi, air.aqiEvac, air.co2Ok, air.co2, airR, airG, airB)) {
    ring().showAir(airR, airG, airB);
  }

  bool bootDown = digitalRead(PIN_BOOT) == LOW;
  if (bootDown) {
    if (bootDownAt == 0) bootDownAt = millis();
    if (!bootHoldFired && millis() - bootDownAt >= 5000) {
      bootHoldFired = true;
      Serial.println("BOOT held, clearing Wi-Fi");
      network().forgetWifi();
    }
  } else {
    bootDownAt = 0;
    bootHoldFired = false;
  }

  UiStatus status;
  status.readings = &sensors().latest();
  status.wifi = network().wifiConnected();
  status.mqtt = network().mqttConnected();
  status.mqttConfigured = network().mqttConfigured();
  status.clock24h = settings().clock24h;
  status.tempUnit = settings().tempUnit;
  status.ip = network().ip();
  status.uptimeSec = millis() / 1000UL;
  mark = micros();
  ui().loop(status);
  noteStage("ui", mark);
  ring().markStage(3, micros() - mark);
  if ((int)status.wifi != lastWifi || (int)status.mqtt != lastMqtt) {
    evlog("LINK wifi=%d mqtt=%d rssi=%d", status.wifi, status.mqtt, status.wifi ? WiFi.RSSI() : 0);
    lastWifi = status.wifi;
    lastMqtt = status.mqtt;
  }
  evlogTick();

  if (network().takeReboot()) {
    evlog("RESTART requested by portal or update");
    Serial.println("Restarting");
    delay(250);
    ESP.restart();
  }
}
