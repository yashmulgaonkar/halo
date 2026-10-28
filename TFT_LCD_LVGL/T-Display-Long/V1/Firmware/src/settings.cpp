#include "settings.h"

#include <Preferences.h>
#include <string.h>

namespace {
Settings g_settings;
Preferences g_prefs;

void copyStr(char* dst, size_t n, const char* src) {
  if (n == 0) return;
  if (src == nullptr) {
    dst[0] = 0;
    return;
  }
  strncpy(dst, src, n - 1);
  dst[n - 1] = 0;
}
}  // namespace

Settings& settings() { return g_settings; }

void Settings::defaults() {
  memset(this, 0, sizeof(*this));
  mqttPort = 1883;
  copyStr(tz, sizeof(tz), "PST8PDT,M3.2.0,M11.1.0");
  tempUnit = TEMP_F;
  clock24h = false;
  sen55TempOffset = 6.0f;
  sen55HumOffset = 0.0f;
  no2Offset = 0.0f;
  co2Asc = false;
  startupBlink = true;
  backlight = 255;
  historyEnabled = true;
  historyIntervalS = 60;
  historyDays = 3.0f;
  pageRotation = false;
  pageIntervalS = 30;
}

const char* Settings::tempUnitName() const {
  if (tempUnit == TEMP_C) return "Celsius";
  if (tempUnit == TEMP_K) return "Kelvin";
  return "Fahrenheit";
}

void Settings::setTempUnitName(const char* name) {
  if (name == nullptr) return;
  if (strcmp(name, "Celsius") == 0) tempUnit = TEMP_C;
  else if (strcmp(name, "Kelvin") == 0) tempUnit = TEMP_K;
  else if (strcmp(name, "Fahrenheit") == 0) tempUnit = TEMP_F;
}

void Settings::load() {
  defaults();
  if (!g_prefs.begin("halo", true)) return;
  g_prefs.getString("ssid", wifiSsid, sizeof(wifiSsid));
  g_prefs.getString("pass", wifiPass, sizeof(wifiPass));
  g_prefs.getString("mhost", mqttHost, sizeof(mqttHost));
  mqttPort = g_prefs.getUShort("mport", mqttPort);
  g_prefs.getString("muser", mqttUser, sizeof(mqttUser));
  g_prefs.getString("mpass", mqttPass, sizeof(mqttPass));
  g_prefs.getString("tz", tz, sizeof(tz));
  tempUnit = g_prefs.getUChar("unit", tempUnit);
  clock24h = g_prefs.getBool("clk24", clock24h);
  sen55TempOffset = g_prefs.getFloat("tOff", sen55TempOffset);
  sen55HumOffset = g_prefs.getFloat("hOff", sen55HumOffset);
  no2Offset = g_prefs.getFloat("nOff", no2Offset);
  co2Asc = g_prefs.getBool("asc", co2Asc);
  startupBlink = g_prefs.getBool("blink", startupBlink);
  backlight = g_prefs.getUChar("bl", backlight);
  historyEnabled = g_prefs.getBool("hist", historyEnabled);
  historyIntervalS = g_prefs.getUShort("hint", historyIntervalS);
  historyDays = g_prefs.getFloat("hdays", historyDays);
  pageRotation = g_prefs.getBool("prot", pageRotation);
  pageIntervalS = g_prefs.getUShort("pint", pageIntervalS);
  g_prefs.end();
  if (tz[0] == 0) copyStr(tz, sizeof(tz), "PST8PDT,M3.2.0,M11.1.0");
  if (mqttPort == 0) mqttPort = 1883;
}

void Settings::save() const {
  if (!g_prefs.begin("halo", false)) return;
  g_prefs.putString("ssid", wifiSsid);
  g_prefs.putString("pass", wifiPass);
  g_prefs.putString("mhost", mqttHost);
  g_prefs.putUShort("mport", mqttPort);
  g_prefs.putString("muser", mqttUser);
  g_prefs.putString("mpass", mqttPass);
  g_prefs.putString("tz", tz);
  g_prefs.putUChar("unit", tempUnit);
  g_prefs.putBool("clk24", clock24h);
  g_prefs.putFloat("tOff", sen55TempOffset);
  g_prefs.putFloat("hOff", sen55HumOffset);
  g_prefs.putFloat("nOff", no2Offset);
  g_prefs.putBool("asc", co2Asc);
  g_prefs.putBool("blink", startupBlink);
  g_prefs.putUChar("bl", backlight);
  g_prefs.putBool("hist", historyEnabled);
  g_prefs.putUShort("hint", historyIntervalS);
  g_prefs.putFloat("hdays", historyDays);
  g_prefs.putBool("prot", pageRotation);
  g_prefs.putUShort("pint", pageIntervalS);
  g_prefs.end();
}
