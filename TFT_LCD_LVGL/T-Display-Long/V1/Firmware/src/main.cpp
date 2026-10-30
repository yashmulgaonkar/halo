#include <Arduino.h>

#include "aqi.h"
#include "backlight.h"
#include "board_pins.h"
#include "display_port.h"
#include "halo_ui.h"
#include "halo_version.h"
#include "network_service.h"
#include "ring_light.h"
#include "sensor_hub.h"
#include "settings.h"
#include "touch_in.h"

namespace {
uint32_t bootDownAt = 0;
bool bootHoldFired = false;
}

void setup() {
  Serial.begin(115200);
  delay(250);
  setCpuFrequencyMhz(240);
  Serial.printf("\n%s %s\n", HALO_FW_NAME, HALO_FW_VERSION);

  settings().load();
  backlight().begin();
  backlight().setLevel(settings().backlight);
  ring().begin();
  displayBegin();
  touchBegin();
  ui().begin();
  displayPump();
  sensors().begin();
  network().begin();
  pinMode(PIN_BOOT, INPUT_PULLUP);
  displayPump();
}

void loop() {
  backlight().refresh();
  uint32_t mark = micros();
  network().loop();
  ring().markStage(1, micros() - mark);
  mark = micros();
  sensors().poll(network().mqttConnected());
  ring().markStage(2, micros() - mark);
  ring().loop(settings().startupBlink, network().mqttConnected(), network().wifiConnected());
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
    if (bootDownAt != 0 && !bootHoldFired && millis() - bootDownAt > 40 && millis() - bootDownAt < 1200) {
      ui().nextPage();
      touchNoteActivity();
    }
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
  ring().markStage(3, micros() - mark);

  if (network().takeReboot()) {
    Serial.println("Restarting");
    delay(250);
    ESP.restart();
  }
}
