#include "network_service.h"

#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <WiFi.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "backlight.h"
#include "display_port.h"
#include "halo_version.h"
#include "ring_light.h"
#include "sensor_hub.h"
#include "settings.h"

namespace {

struct SensorEnt {
  const char* key;
  const char* name;
  const char* unit;
  const char* devClass;
  const char* category;
  const char* stateClass;
  int decimals;
};

const SensorEnt kSensors[] = {
    {"co2", "CO2", "ppm", "carbon_dioxide", nullptr, "measurement", 0},
    {"nitrogen_dioxide", "Nitrogen Dioxide", "ppm", nullptr, nullptr, "measurement", 2},
    {"carbon_monoxide", "Carbon Monoxide", "ppm", "carbon_monoxide", nullptr, "measurement", 2},
    {"hydrogen", "Hydrogen", "ppm", nullptr, nullptr, "measurement", 2},
    {"methane", "Methane", "ppm", nullptr, nullptr, "measurement", 2},
    {"ethanol", "Ethanol", "ppm", nullptr, nullptr, "measurement", 2},
    {"ammonia", "Ammonia", "ppm", nullptr, nullptr, "measurement", 2},
    {"bme280_temperature", "BME280 Temperature", "°C", "temperature", nullptr, "measurement", 1},
    {"bme280_pressure", "BME280 Pressure", "hPa", "pressure", nullptr, "measurement", 1},
    {"bme280_humidity", "BME280 Humidity", "%", "humidity", nullptr, "measurement", 0},
    {"pm1", "PM <1um Weight concentration", "ug/m3", nullptr, nullptr, "measurement", 1},
    {"pm25", "PM <2.5um Weight concentration", "ug/m3", "pm25", nullptr, "measurement", 1},
    {"pm4", "PM <4um Weight concentration", "ug/m3", nullptr, nullptr, "measurement", 1},
    {"pm10", "PM <10um Weight concentration", "ug/m3", "pm10", nullptr, "measurement", 1},
    {"sen55_temperature", "SEN55 Temperature", "°C", "temperature", nullptr, "measurement", 1},
    {"sen55_humidity", "SEN55 Humidity", "%", "humidity", nullptr, "measurement", 0},
    {"sen55_voc", "SEN55 VOC", nullptr, nullptr, nullptr, "measurement", 0},
    {"sen55_nox", "SEN55 NOX", nullptr, nullptr, nullptr, "measurement", 1},
    {"computed_aqi", "Computed AQI", nullptr, "aqi", nullptr, "measurement", 0},
    {"nowcast_aqi", "NowCast AQI", nullptr, "aqi", nullptr, "measurement", 0},
    {"uptime", "Uptime", "s", "duration", "diagnostic", "total_increasing", 0},
    {"rssi", "RSSI", "dBm", "signal_strength", "diagnostic", "measurement", 0},
    {"history_fill", "AQI History Buffer Fill", "%", nullptr, "diagnostic", "measurement", 0},
    {"history_count", "AQI History Buffered Entries", "entries", nullptr, "diagnostic", "measurement", 0},
};

void copyField(char* dst, size_t n, const String& src) {
  strncpy(dst, src.c_str(), n - 1);
  dst[n - 1] = 0;
}

String htmlEscape(const char* src) {
  String out;
  if (src == nullptr) return out;
  for (const char* p = src; *p; p++) {
    if (*p == '&') out += "&amp;";
    else if (*p == '<') out += "&lt;";
    else if (*p == '"') out += "&quot;";
    else out += *p;
  }
  return out;
}

bool lookup(const char* key, const Readings& r, bool wifi, float& out) {
  if (strcmp(key, "co2") == 0) { out = r.co2; return r.co2Ok; }
  if (strcmp(key, "nitrogen_dioxide") == 0) { out = r.no2; return r.micsOk; }
  if (strcmp(key, "carbon_monoxide") == 0) { out = r.co; return r.micsOk; }
  if (strcmp(key, "hydrogen") == 0) { out = r.h2; return r.micsOk; }
  if (strcmp(key, "methane") == 0) { out = r.ch4; return r.micsOk; }
  if (strcmp(key, "ethanol") == 0) { out = r.ethanol; return r.micsOk; }
  if (strcmp(key, "ammonia") == 0) { out = r.nh3; return r.micsOk; }
  if (strcmp(key, "bme280_temperature") == 0) { out = r.bmeTempC; return r.bmeOk; }
  if (strcmp(key, "bme280_pressure") == 0) { out = r.bmePressureHpa; return r.bmeOk; }
  if (strcmp(key, "bme280_humidity") == 0) { out = r.bmeHum; return r.bmeOk; }
  if (strcmp(key, "pm1") == 0) { out = r.pm1; return r.senOk; }
  if (strcmp(key, "pm25") == 0) { out = r.pm25; return r.senOk; }
  if (strcmp(key, "pm4") == 0) { out = r.pm4; return r.senOk; }
  if (strcmp(key, "pm10") == 0) { out = r.pm10; return r.senOk; }
  if (strcmp(key, "sen55_temperature") == 0) { out = r.senTempC; return r.senOk; }
  if (strcmp(key, "sen55_humidity") == 0) { out = r.senHum; return r.senOk; }
  if (strcmp(key, "sen55_voc") == 0) { out = r.voc; return r.senOk; }
  if (strcmp(key, "sen55_nox") == 0) { out = r.nox; return r.senOk && r.noxOk; }
  if (strcmp(key, "computed_aqi") == 0) { out = r.aqi; return r.aqiOk; }
  if (strcmp(key, "nowcast_aqi") == 0) { out = r.nowcast; return r.nowcastOk; }
  if (strcmp(key, "uptime") == 0) { out = millis() / 1000.0f; return true; }
  if (strcmp(key, "rssi") == 0) { out = WiFi.RSSI(); return wifi; }
  if (strcmp(key, "history_fill") == 0) { out = sensors().history().fillPercent(); return true; }
  if (strcmp(key, "history_count") == 0) { out = sensors().history().count(); return true; }
  return false;
}

bool isOn(const char* body) {
  return strcmp(body, "ON") == 0 || strcmp(body, "on") == 0 || strcmp(body, "true") == 0 || strcmp(body, "1") == 0;
}
}  // namespace

NetworkService* NetworkService::instance_ = nullptr;

void NetworkService::mqttTrampoline(char* topic, uint8_t* payload, unsigned int length) {
  if (instance_) instance_->onMqtt(topic, payload, length);
}

NetworkService& network() {
  static NetworkService service;
  return service;
}

bool NetworkService::wifiConnected() const { return WiFi.status() == WL_CONNECTED; }

bool NetworkService::mqttConnected() { return mqtt_.connected(); }

bool NetworkService::mqttConfigured() const { return settings().mqttHost[0] != 0; }

bool NetworkService::takeReboot() {
  bool was = reboot_;
  reboot_ = false;
  return was;
}

void NetworkService::forgetWifi() {
  settings().wifiSsid[0] = 0;
  settings().wifiPass[0] = 0;
  settings().save();
  reboot_ = true;
}

void NetworkService::addDevice(JsonDocument& doc) {
  JsonObject dev = doc["device"].to<JsonObject>();
  dev["identifiers"].add(deviceId_);
  dev["name"] = "Halo Air Quality Sensor";
  dev["manufacturer"] = "HALO";
  dev["model"] = "T-Display-S3-Long";
  dev["sw_version"] = HALO_FW_VERSION;
  if (staReady_) {
    char url[40];
    snprintf(url, sizeof(url), "http://%s/", ip_);
    dev["configuration_url"] = url;
  }
}

bool NetworkService::publish(const char* topic, const char* payload, bool retain) {
  return mqtt_.publish(topic, payload, retain);
}

void NetworkService::begin() {
  instance_ = this;
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  snprintf(deviceId_, sizeof(deviceId_), "halo_%02x%02x%02x", mac[3], mac[4], mac[5]);
  snprintf(hostname_, sizeof(hostname_), "halo-%02x%02x%02x", mac[3], mac[4], mac[5]);
  snprintf(root_, sizeof(root_), "halo/%s", deviceId_);
  snprintf(statusTopic_, sizeof(statusTopic_), "%s/status", root_);
  snprintf(cmdFilter_, sizeof(cmdFilter_), "%s/cmd/#", root_);

  WiFi.persistent(false);
  WiFi.setSleep(WIFI_PS_NONE);
  WiFi.setHostname(hostname_);
  WiFi.setAutoReconnect(true);
  WiFi.mode(WIFI_STA);
  if (settings().wifiSsid[0] == 0) {
    startAp();
  } else {
    Serial.printf("Wi-Fi connecting to %s\n", settings().wifiSsid);
    WiFi.begin(settings().wifiSsid, settings().wifiPass);
    wifiDownSince_ = millis();
    wifiRetryMs_ = wifiDownSince_;
  }

  mqtt_.setClient(wifiClient_);
  mqtt_.setBufferSize(2048);
  mqtt_.setSocketTimeout(2);
  mqtt_.setCallback(NetworkService::mqttTrampoline);

  server_.on("/", HTTP_GET, [this]() {
    if (wifiConnected() && !server_.hasArg("setup")) sendStatus();
    else sendSetup();
  });
  server_.on("/setup", HTTP_GET, [this]() { sendSetup(); });
  server_.on("/save", HTTP_POST, [this]() { handleSave(); });
  server_.on("/ring", HTTP_POST, [this]() { handleRing(); });
  server_.on("/api/state", HTTP_GET, [this]() { handleApi(); });
  server_.on("/reboot", HTTP_POST, [this]() {
    server_.send(200, "text/html", "<p>Restarting.</p>");
    reboot_ = true;
  });
  server_.on("/forget", HTTP_POST, [this]() {
    server_.send(200, "text/html", "<p>Wi-Fi cleared. Restarting into Halo Hotspot.</p>");
    forgetWifi();
  });
  server_.on("/generate_204", HTTP_GET, [this]() { sendSetup(); });
  server_.on("/gen_204", HTTP_GET, [this]() { sendSetup(); });
  server_.on("/hotspot-detect.html", HTTP_GET, [this]() { sendSetup(); });
  server_.on("/ncsi.txt", HTTP_GET, [this]() { sendSetup(); });
  server_.onNotFound([this]() {
    if (!wifiConnected()) sendSetup();
    else server_.send(404, "text/plain", "Not found");
  });
  server_.begin();
  Serial.printf("Portal ready as %s\n", hostname_);
}

void NetworkService::startAp() {
  if (apOn_) return;
  WiFi.mode(settings().wifiSsid[0] ? WIFI_AP_STA : WIFI_AP);
  IPAddress ip(192, 168, 4, 1);
  WiFi.softAPConfig(ip, ip, IPAddress(255, 255, 255, 0));
  WiFi.softAP("Halo Hotspot");
  dns_.start(53, "*", ip);
  apOn_ = true;
  Serial.println("Hotspot Halo Hotspot at 192.168.4.1");
}

void NetworkService::stopAp() {
  if (!apOn_) return;
  dns_.stop();
  WiFi.softAPdisconnect(true);
  apOn_ = false;
}

void NetworkService::onStaReady() {
  staReady_ = true;
  IPAddress addr = WiFi.localIP();
  snprintf(ip_, sizeof(ip_), "%u.%u.%u.%u", addr[0], addr[1], addr[2], addr[3]);
  Serial.printf("Wi-Fi %s as %s\n", ip_, hostname_);
  configTzTime(settings().tz, "pool.ntp.org", "time.nist.gov");
  if (MDNS.begin(hostname_)) {
    MDNS.addService("http", "tcp", 80);
  }
  ArduinoOTA.setHostname(hostname_);
  ArduinoOTA.onStart([]() { ring().suspend(true); });
  ArduinoOTA.begin();
  stopAp();
}

void NetworkService::connectMqtt() {
  if (!mqttConfigured() || !wifiConnected()) return;
  const Settings& cfg = settings();
  mqtt_.setServer(cfg.mqttHost, cfg.mqttPort);
  Serial.printf("MQTT connecting to %s:%u\n", cfg.mqttHost, cfg.mqttPort);
  bool ok;
  if (cfg.mqttUser[0]) {
    ok = mqtt_.connect(deviceId_, cfg.mqttUser, cfg.mqttPass, statusTopic_, 1, true, "offline");
  } else {
    ok = mqtt_.connect(deviceId_, statusTopic_, 1, true, "offline");
  }
  if (!ok) {
    Serial.printf("MQTT failed rc=%d\n", mqtt_.state());
    return;
  }
  mqtt_.subscribe(cmdFilter_);
  mqtt_.publish(statusTopic_, "online", true);
  publishDiscovery();
  publishStates();
  publishRing();
  publishBacklight();
  sensors().history().startFlush();
  lastDiscoveryMs_ = millis();
  lastStateMs_ = millis();
  discoverySent_ = true;
  Serial.println("MQTT online, Home Assistant discovery sent");
}

void NetworkService::publishDiscovery() {
  for (const SensorEnt& ent : kSensors) {
    JsonDocument doc;
    doc["name"] = ent.name;
    char uid[48];
    snprintf(uid, sizeof(uid), "%s_%s", deviceId_, ent.key);
    doc["unique_id"] = uid;
    char state[72];
    snprintf(state, sizeof(state), "%s/sensor/%s", root_, ent.key);
    doc["state_topic"] = state;
    if (ent.unit) doc["unit_of_measurement"] = ent.unit;
    if (ent.devClass) doc["device_class"] = ent.devClass;
    if (ent.stateClass) doc["state_class"] = ent.stateClass;
    if (ent.category) doc["entity_category"] = ent.category;
    doc["suggested_display_precision"] = ent.decimals;
    doc["availability_topic"] = statusTopic_;
    doc["payload_available"] = "online";
    doc["payload_not_available"] = "offline";
    addDevice(doc);
    char config[96];
    snprintf(config, sizeof(config), "homeassistant/sensor/%s/%s/config", deviceId_, ent.key);
    char body[1500];
    size_t n = serializeJson(doc, body, sizeof(body));
    if (n > 0 && n < sizeof(body)) mqtt_.publish(config, body, true);
    displayPump();
    yield();
  }

  auto textEnt = [&](const char* key, const char* name, const char* category) {
    JsonDocument doc;
    doc["name"] = name;
    char uid[48];
    snprintf(uid, sizeof(uid), "%s_%s", deviceId_, key);
    doc["unique_id"] = uid;
    char state[72];
    snprintf(state, sizeof(state), "%s/text/%s", root_, key);
    doc["state_topic"] = state;
    if (category) doc["entity_category"] = category;
    doc["availability_topic"] = statusTopic_;
    doc["payload_available"] = "online";
    doc["payload_not_available"] = "offline";
    addDevice(doc);
    char config[96];
    snprintf(config, sizeof(config), "homeassistant/sensor/%s/%s/config", deviceId_, key);
    char body[1200];
    size_t n = serializeJson(doc, body, sizeof(body));
    if (n > 0 && n < sizeof(body)) mqtt_.publish(config, body, true);
    displayPump();
  };
  textEnt("voc_quality", "VOC Quality", nullptr);
  textEnt("firmware_version", "HALO Firmware Version", "diagnostic");

  auto sw = [&](const char* key, const char* name) {
    JsonDocument doc;
    doc["name"] = name;
    char uid[48];
    snprintf(uid, sizeof(uid), "%s_%s", deviceId_, key);
    doc["unique_id"] = uid;
    char state[80];
    char cmd[80];
    snprintf(state, sizeof(state), "%s/switch/%s", root_, key);
    snprintf(cmd, sizeof(cmd), "%s/cmd/switch/%s", root_, key);
    doc["state_topic"] = state;
    doc["command_topic"] = cmd;
    doc["payload_on"] = "ON";
    doc["payload_off"] = "OFF";
    doc["state_on"] = "ON";
    doc["state_off"] = "OFF";
    doc["entity_category"] = "config";
    doc["availability_topic"] = statusTopic_;
    addDevice(doc);
    char config[96];
    snprintf(config, sizeof(config), "homeassistant/switch/%s/%s/config", deviceId_, key);
    char body[1200];
    size_t n = serializeJson(doc, body, sizeof(body));
    if (n > 0 && n < sizeof(body)) mqtt_.publish(config, body, true);
    displayPump();
  };
  sw("startup_blink", "Startup Light Blink");
  sw("clock_24h", "24h Clock Format");
  sw("co2_asc", "CO2 Auto Calibration");
  sw("history", "AQI History Enable Buffering");
  sw("page_rotation", "Auto Page Rotation");

  auto num = [&](const char* key, const char* name, float minV, float maxV, float step, const char* unit) {
    JsonDocument doc;
    doc["name"] = name;
    char uid[48];
    snprintf(uid, sizeof(uid), "%s_%s", deviceId_, key);
    doc["unique_id"] = uid;
    char state[80];
    char cmd[80];
    snprintf(state, sizeof(state), "%s/number/%s", root_, key);
    snprintf(cmd, sizeof(cmd), "%s/cmd/number/%s", root_, key);
    doc["state_topic"] = state;
    doc["command_topic"] = cmd;
    doc["min"] = minV;
    doc["max"] = maxV;
    doc["step"] = step;
    doc["mode"] = "box";
    doc["entity_category"] = "config";
    if (unit) doc["unit_of_measurement"] = unit;
    doc["availability_topic"] = statusTopic_;
    addDevice(doc);
    char config[96];
    snprintf(config, sizeof(config), "homeassistant/number/%s/%s/config", deviceId_, key);
    char body[1200];
    size_t n = serializeJson(doc, body, sizeof(body));
    if (n > 0 && n < sizeof(body)) mqtt_.publish(config, body, true);
    displayPump();
  };
  num("sen55_temp_offset", "SEN55 Temperature Offset", -70, 70, 0.1f, "°C");
  num("sen55_hum_offset", "SEN55 Humidity Offset", -70, 70, 0.1f, "%");
  num("no2_offset", "MICS4514 NO2 Offset", -1, 1, 0.01f, "ppm");
  num("history_interval", "AQI History Sample Interval", 30, 1800, 30, "s");
  num("history_days", "AQI History Max Coverage", 0.5f, 7, 0.5f, "days");
  num("page_interval", "Screen Cycle Timeout", 0, 300, 1, "s");
  num("temp_bias", "Temperature Bias", -20, 20, 0.1f, "°C");
  num("hum_bias", "Humidity Bias", -30, 30, 1, "%");
  num("co_bias", "CO Bias", -50, 50, 0.1f, "ppm");
  num("no2_bias", "NO2 Bias", -50, 50, 0.01f, "ppm");
  num("h2_bias", "Hydrogen Bias", -50, 50, 0.1f, "ppm");
  num("ch4_bias", "Methane Bias", -50, 50, 0.1f, "ppm");
  num("ethanol_bias", "Ethanol Bias", -50, 50, 0.01f, "ppm");
  num("nh3_bias", "Ammonia Bias", -50, 50, 0.1f, "ppm");
  num("nox_bias", "NOx Bias", -50, 50, 0.1f, "idx");

  {
    JsonDocument doc;
    doc["name"] = "Display Temperature Unit";
    char uid[48];
    snprintf(uid, sizeof(uid), "%s_temp_unit", deviceId_);
    doc["unique_id"] = uid;
    char state[80];
    char cmd[80];
    snprintf(state, sizeof(state), "%s/select/temp_unit", root_);
    snprintf(cmd, sizeof(cmd), "%s/cmd/select/temp_unit", root_);
    doc["state_topic"] = state;
    doc["command_topic"] = cmd;
    JsonArray opts = doc["options"].to<JsonArray>();
    opts.add("Fahrenheit");
    opts.add("Celsius");
    opts.add("Kelvin");
    doc["entity_category"] = "config";
    doc["availability_topic"] = statusTopic_;
    addDevice(doc);
    char body[1200];
    size_t n = serializeJson(doc, body, sizeof(body));
    char config[96];
    snprintf(config, sizeof(config), "homeassistant/select/%s/temp_unit/config", deviceId_);
    if (n > 0 && n < sizeof(body)) mqtt_.publish(config, body, true);
    displayPump();
  }

  auto button = [&](const char* key, const char* name) {
    JsonDocument doc;
    doc["name"] = name;
    char uid[48];
    snprintf(uid, sizeof(uid), "%s_%s", deviceId_, key);
    doc["unique_id"] = uid;
    char cmd[80];
    snprintf(cmd, sizeof(cmd), "%s/cmd/button/%s", root_, key);
    doc["command_topic"] = cmd;
    doc["payload_press"] = "PRESS";
    doc["availability_topic"] = statusTopic_;
    addDevice(doc);
    char config[96];
    snprintf(config, sizeof(config), "homeassistant/button/%s/%s/config", deviceId_, key);
    char body[1000];
    size_t n = serializeJson(doc, body, sizeof(body));
    if (n > 0 && n < sizeof(body)) mqtt_.publish(config, body, true);
    displayPump();
  };
  button("reboot", "ESP Reboot");
  button("calibrate_co2", "Calibrate SCD40 To 420ppm");
  button("sen55_clean", "Clean SEN55");

  {
    JsonDocument doc;
    doc["name"] = "Halo";
    char uid[48];
    snprintf(uid, sizeof(uid), "%s_ring", deviceId_);
    doc["unique_id"] = uid;
    doc["schema"] = "json";
    doc["brightness"] = true;
    doc["effect"] = true;
    JsonArray modes = doc["supported_color_modes"].to<JsonArray>();
    modes.add("rgb");
    JsonArray effects = doc["effect_list"].to<JsonArray>();
    effects.add("None");
    effects.add("Pulse");
    effects.add("Rainbow");
    effects.add("Scan");
    effects.add("Twinkle");
    effects.add("Color Wipe");
    char state[64];
    char cmd[64];
    snprintf(state, sizeof(state), "%s/ring", root_);
    snprintf(cmd, sizeof(cmd), "%s/cmd/ring", root_);
    doc["state_topic"] = state;
    doc["command_topic"] = cmd;
    doc["availability_topic"] = statusTopic_;
    addDevice(doc);
    char config[96];
    snprintf(config, sizeof(config), "homeassistant/light/%s/ring/config", deviceId_);
    char body[1400];
    size_t n = serializeJson(doc, body, sizeof(body));
    if (n > 0 && n < sizeof(body)) mqtt_.publish(config, body, true);
    displayPump();
  }
  {
    JsonDocument doc;
    doc["name"] = "Display Backlight";
    char uid[48];
    snprintf(uid, sizeof(uid), "%s_backlight", deviceId_);
    doc["unique_id"] = uid;
    doc["schema"] = "json";
    doc["brightness"] = true;
    JsonArray modes = doc["supported_color_modes"].to<JsonArray>();
    modes.add("brightness");
    char state[64];
    char cmd[72];
    snprintf(state, sizeof(state), "%s/backlight", root_);
    snprintf(cmd, sizeof(cmd), "%s/cmd/backlight", root_);
    doc["state_topic"] = state;
    doc["command_topic"] = cmd;
    doc["availability_topic"] = statusTopic_;
    addDevice(doc);
    char config[96];
    snprintf(config, sizeof(config), "homeassistant/light/%s/backlight/config", deviceId_);
    char body[1200];
    size_t n = serializeJson(doc, body, sizeof(body));
    if (n > 0 && n < sizeof(body)) mqtt_.publish(config, body, true);
    displayPump();
  }
}

void NetworkService::publishStates() {
  const Readings& r = sensors().latest();
  bool wifi = wifiConnected();
  char topic[80];
  char value[24];
  for (const SensorEnt& ent : kSensors) {
    snprintf(topic, sizeof(topic), "%s/sensor/%s", root_, ent.key);
    float number = 0;
    if (!lookup(ent.key, r, wifi, number)) {
      mqtt_.publish(topic, "unavailable", true);
    } else {
      snprintf(value, sizeof(value), "%.*f", ent.decimals, number);
      mqtt_.publish(topic, value, true);
    }
    displayPump();
  }
  snprintf(topic, sizeof(topic), "%s/text/voc_quality", root_);
  mqtt_.publish(topic, r.senOk ? r.vocQuality : "unavailable", true);
  snprintf(topic, sizeof(topic), "%s/text/firmware_version", root_);
  mqtt_.publish(topic, HALO_FW_VERSION, true);

  const Settings& cfg = settings();
  auto pubSwitch = [&](const char* key, bool on) {
    snprintf(topic, sizeof(topic), "%s/switch/%s", root_, key);
    mqtt_.publish(topic, on ? "ON" : "OFF", true);
    displayPump();
  };
  pubSwitch("startup_blink", cfg.startupBlink);
  pubSwitch("clock_24h", cfg.clock24h);
  pubSwitch("co2_asc", cfg.co2Asc);
  pubSwitch("history", cfg.historyEnabled);
  pubSwitch("page_rotation", cfg.pageRotation);

  auto pubNum = [&](const char* key, float number, int decimals) {
    snprintf(topic, sizeof(topic), "%s/number/%s", root_, key);
    snprintf(value, sizeof(value), "%.*f", decimals, number);
    mqtt_.publish(topic, value, true);
    displayPump();
  };
  pubNum("sen55_temp_offset", cfg.sen55TempOffset, 1);
  pubNum("sen55_hum_offset", cfg.sen55HumOffset, 1);
  pubNum("no2_offset", cfg.no2Offset, 2);
  pubNum("history_interval", cfg.historyIntervalS, 0);
  pubNum("history_days", cfg.historyDays, 1);
  pubNum("page_interval", cfg.pageIntervalS, 0);
  pubNum("temp_bias", cfg.tempBias, 1);
  pubNum("hum_bias", cfg.humBias, 0);
  pubNum("co_bias", cfg.gasBias[0], 1);
  pubNum("no2_bias", cfg.gasBias[1], 2);
  pubNum("h2_bias", cfg.gasBias[2], 1);
  pubNum("ch4_bias", cfg.gasBias[3], 1);
  pubNum("ethanol_bias", cfg.gasBias[4], 2);
  pubNum("nh3_bias", cfg.gasBias[5], 1);
  pubNum("nox_bias", cfg.gasBias[6], 1);

  snprintf(topic, sizeof(topic), "%s/select/temp_unit", root_);
  mqtt_.publish(topic, cfg.tempUnitName(), true);
}

void NetworkService::publishRing() {
  JsonDocument doc;
  doc["state"] = ring().power() ? "ON" : "OFF";
  doc["brightness"] = ring().brightness();
  doc["color_mode"] = "rgb";
  doc["color"]["r"] = ring().red();
  doc["color"]["g"] = ring().green();
  doc["color"]["b"] = ring().blue();
  doc["effect"] = ring().effectName();
  char topic[64];
  snprintf(topic, sizeof(topic), "%s/ring", root_);
  char body[256];
  size_t n = serializeJson(doc, body, sizeof(body));
  if (n > 0 && n < sizeof(body)) mqtt_.publish(topic, body, true);
}

void NetworkService::publishBacklight() {
  JsonDocument doc;
  uint8_t level = backlight().level();
  doc["state"] = level ? "ON" : "OFF";
  doc["brightness"] = level;
  char topic[64];
  snprintf(topic, sizeof(topic), "%s/backlight", root_);
  char body[96];
  size_t n = serializeJson(doc, body, sizeof(body));
  if (n > 0 && n < sizeof(body)) mqtt_.publish(topic, body, true);
  publishedBacklight_ = level;
}

void NetworkService::onMqtt(char* topic, uint8_t* payload, unsigned int length) {
  char body[384];
  unsigned int n = length < sizeof(body) - 1 ? length : sizeof(body) - 1;
  memcpy(body, payload, n);
  body[n] = 0;
  const char* marker = strstr(topic, "/cmd/");
  if (marker == nullptr) return;
  handleCmd(marker + 5, body);
}

void NetworkService::handleCmd(const char* cmd, const char* body) {
  Settings& cfg = settings();
  bool save = false;
  if (strcmp(cmd, "switch/startup_blink") == 0) {
    cfg.startupBlink = isOn(body);
    save = true;
  } else if (strcmp(cmd, "switch/clock_24h") == 0) {
    cfg.clock24h = isOn(body);
    save = true;
  } else if (strcmp(cmd, "switch/co2_asc") == 0) {
    cfg.co2Asc = isOn(body);
    sensors().requestAsc(cfg.co2Asc);
    save = true;
  } else if (strcmp(cmd, "switch/history") == 0) {
    cfg.historyEnabled = isOn(body);
    save = true;
  } else if (strcmp(cmd, "switch/page_rotation") == 0) {
    cfg.pageRotation = isOn(body);
    if (cfg.pageRotation && cfg.pageIntervalS == 0) cfg.pageIntervalS = 30;
    save = true;
  } else if (strcmp(cmd, "number/sen55_temp_offset") == 0) {
    float v = atof(body);
    if (v >= -70 && v <= 70) { cfg.sen55TempOffset = v; save = true; }
  } else if (strcmp(cmd, "number/sen55_hum_offset") == 0) {
    float v = atof(body);
    if (v >= -70 && v <= 70) { cfg.sen55HumOffset = v; save = true; }
  } else if (strcmp(cmd, "number/no2_offset") == 0) {
    float v = atof(body);
    if (v >= -1 && v <= 1) { cfg.no2Offset = v; save = true; }
  } else if (strcmp(cmd, "number/history_interval") == 0) {
    int v = atoi(body);
    if (v >= 30 && v <= 1800) { cfg.historyIntervalS = (uint16_t)v; save = true; }
  } else if (strcmp(cmd, "number/history_days") == 0) {
    float v = atof(body);
    if (v >= 0.5f && v <= 7) { cfg.historyDays = v; save = true; }
  } else if (strcmp(cmd, "number/page_interval") == 0) {
    int v = atoi(body);
    if (v >= 0 && v <= 300) {
      cfg.pageIntervalS = (uint16_t)v;
      cfg.pageRotation = v > 0;
      save = true;
    }
  } else if (strcmp(cmd, "number/temp_bias") == 0) {
    float v = atof(body);
    if (v >= -20 && v <= 20) { cfg.tempBias = v; save = true; }
  } else if (strcmp(cmd, "number/hum_bias") == 0) {
    float v = atof(body);
    if (v >= -30 && v <= 30) { cfg.humBias = v; save = true; }
  } else if (strcmp(cmd, "number/co_bias") == 0) {
    float v = atof(body);
    if (v >= -50 && v <= 50) { cfg.gasBias[0] = v; save = true; }
  } else if (strcmp(cmd, "number/no2_bias") == 0) {
    float v = atof(body);
    if (v >= -50 && v <= 50) { cfg.gasBias[1] = v; save = true; }
  } else if (strcmp(cmd, "number/h2_bias") == 0) {
    float v = atof(body);
    if (v >= -50 && v <= 50) { cfg.gasBias[2] = v; save = true; }
  } else if (strcmp(cmd, "number/ch4_bias") == 0) {
    float v = atof(body);
    if (v >= -50 && v <= 50) { cfg.gasBias[3] = v; save = true; }
  } else if (strcmp(cmd, "number/ethanol_bias") == 0) {
    float v = atof(body);
    if (v >= -50 && v <= 50) { cfg.gasBias[4] = v; save = true; }
  } else if (strcmp(cmd, "number/nh3_bias") == 0) {
    float v = atof(body);
    if (v >= -50 && v <= 50) { cfg.gasBias[5] = v; save = true; }
  } else if (strcmp(cmd, "number/nox_bias") == 0) {
    float v = atof(body);
    if (v >= -50 && v <= 50) { cfg.gasBias[6] = v; save = true; }
  } else if (strcmp(cmd, "select/temp_unit") == 0) {
    cfg.setTempUnitName(body);
    save = true;
  } else if (strcmp(cmd, "button/reboot") == 0) {
    reboot_ = true;
  } else if (strcmp(cmd, "button/calibrate_co2") == 0) {
    uint16_t ppm = 420;
    if (body[0] >= '0' && body[0] <= '9') ppm = (uint16_t)atoi(body);
    sensors().requestCalibration(ppm);
  } else if (strcmp(cmd, "button/sen55_clean") == 0) {
    sensors().requestFanClean();
  } else if (strcmp(cmd, "ring") == 0) {
    if (body[0] != '{') {
      ring().setPower(isOn(body));
    } else {
      JsonDocument doc;
      if (deserializeJson(doc, body) == DeserializationError::Ok) {
        if (doc["state"].is<const char*>()) ring().setPower(strcmp(doc["state"], "ON") == 0);
        if (doc["brightness"].is<int>()) ring().setBrightness((uint8_t)doc["brightness"].as<int>());
        if (doc["color"].is<JsonObject>()) {
          ring().setColor(doc["color"]["r"] | 0, doc["color"]["g"] | 0, doc["color"]["b"] | 0);
        }
        if (doc["effect"].is<const char*>()) ring().setEffect(doc["effect"]);
        if (ring().power()) ring().consumePureBlueAsWhite();
      }
    }
    publishRing();
  } else if (strcmp(cmd, "backlight") == 0) {
    uint8_t level = backlight().level();
    bool on = level > 0;
    if (body[0] != '{') {
      if (body[0] >= '0' && body[0] <= '9') level = (uint8_t)atoi(body);
      else on = isOn(body);
    } else {
      JsonDocument doc;
      if (deserializeJson(doc, body) == DeserializationError::Ok) {
        if (doc["state"].is<const char*>()) on = strcmp(doc["state"], "ON") == 0;
        if (doc["brightness"].is<int>()) level = (uint8_t)doc["brightness"].as<int>();
      }
    }
    if (!on) level = 0;
    else if (level == 0) level = 255;
    backlight().setLevel(level);
    cfg.backlight = level;
    save = true;
    publishBacklight();
  }
  if (save) {
    cfg.save();
    sensors().syncBias();
    if (mqtt_.connected()) publishStates();
  }
}

void NetworkService::publishTelemetry() {
  if (mqtt_.connected()) publishStates();
}

void NetworkService::sendSetup() {
  const Settings& cfg = settings();
  String page;
  page.reserve(3600);
  page += "<!doctype html><meta name=viewport content=\"width=device-width,initial-scale=1\">";
  page += "<title>HALO</title><style>body{font:16px sans-serif;background:#000;color:#f4f7fb;margin:0;padding:20px}";
  page += "input{width:100%;box-sizing:border-box;margin:4px 0 12px;padding:10px;border-radius:8px;border:0;background:#141820;color:#fff}";
  page += "button{background:#3ddc97;color:#04140c;border:0;border-radius:8px;padding:12px 16px;font-weight:700}</style>";
  page += "<h1>HALO</h1><p>Join Wi-Fi, then point this device at your MQTT broker. Home Assistant discovery is automatic.</p>";
  page += "<form method=post action=/save>";
  page += "<label>Wi-Fi name</label><input name=ssid value=\"" + htmlEscape(cfg.wifiSsid) + "\">";
  page += "<label>Wi-Fi password</label><input name=pass type=password placeholder=unchanged>";
  page += "<label>MQTT host</label><input name=mqtt_host value=\"" + htmlEscape(cfg.mqttHost) + "\">";
  char port[8];
  snprintf(port, sizeof(port), "%u", cfg.mqttPort);
  page += "<label>MQTT port</label><input name=mqtt_port value=\"";
  page += port;
  page += "\">";
  page += "<label>MQTT user</label><input name=mqtt_user value=\"" + htmlEscape(cfg.mqttUser) + "\">";
  page += "<label>MQTT password</label><input name=mqtt_pass type=password placeholder=unchanged>";
  page += "<label>Timezone</label><input name=tz value=\"" + htmlEscape(cfg.tz) + "\">";
  page += "<button type=submit>Save and restart</button></form>";
  appendRingForm(page);
  server_.send(200, "text/html", page);
}

void NetworkService::appendRingForm(String& page) {
  char color[8];
  snprintf(color, sizeof(color), "#%02x%02x%02x", ring().red(), ring().green(), ring().blue());
  bool rainbow = strcmp(ring().effectName(), "Rainbow") == 0 && ring().power();
  uint8_t bright = ring().brightness();
  if (bright == 0) bright = 180;
  page += "<h2>Ring</h2><form method=post action=/ring>";
  page += "<label>Color</label><input type=color name=color value=\"";
  page += color;
  page += "\" style=\"height:48px;padding:4px\">";
  page += "<label>Brightness</label><input name=brightness type=range min=1 max=255 value=\"";
  page += String(bright);
  page += "\" style=\"width:100%;height:32px;padding:0;margin:4px 0 12px\">";
  page += "<label style=\"display:flex;align-items:center;gap:8px;margin:0 0 12px\">";
  page += "<input type=checkbox name=rainbow value=1 style=\"width:auto;margin:0\"";
  if (rainbow) page += " checked";
  page += "> Rainbow</label>";
  page += "<button type=submit>Apply</button> ";
  page += "<button type=submit name=mode value=off style=\"background:#f07167\">Off</button></form>";
}

void NetworkService::handleRing() {
  if (server_.hasArg("brightness")) {
    int bright = server_.arg("brightness").toInt();
    if (bright < 1) bright = 1;
    if (bright > 255) bright = 255;
    ring().setBrightness((uint8_t)bright);
  }
  if (server_.hasArg("mode") && server_.arg("mode") == "off") {
    ring().setPower(false);
  } else if (server_.hasArg("rainbow")) {
    if (ring().brightness() == 0) ring().setBrightness(180);
    ring().setEffect("Rainbow");
    ring().setPower(true);
  } else {
    uint8_t r = 255, g = 255, b = 255;
    String color = server_.arg("color");
    if (color.length() == 7 && color.charAt(0) == '#') {
      r = (uint8_t)strtol(color.substring(1, 3).c_str(), nullptr, 16);
      g = (uint8_t)strtol(color.substring(3, 5).c_str(), nullptr, 16);
      b = (uint8_t)strtol(color.substring(5, 7).c_str(), nullptr, 16);
    }
    if (ring().brightness() == 0) ring().setBrightness(180);
    ring().setEffect("None");
    ring().setColor(r, g, b);
    ring().setPower(true);
  }
  if (mqtt_.connected()) publishRing();
  server_.sendHeader("Location", String(wifiConnected() ? "/" : "/setup"), true);
  server_.send(303, "text/plain", "Updated");
}

void NetworkService::sendStatus() {
  const Readings& r = sensors().latest();
  String page;
  page.reserve(3400);
  page += "<!doctype html><meta name=viewport content=\"width=device-width,initial-scale=1\">";
  page += "<title>HALO</title><style>body{font:16px sans-serif;background:#000;color:#f4f7fb;margin:0;padding:20px}";
  page += "a,button{color:#04140c;background:#3ddc97;border:0;border-radius:8px;padding:8px 12px;text-decoration:none}";
  page += "input[type=color]{width:100%;box-sizing:border-box;margin:4px 0 12px;border:0;border-radius:8px;background:#141820}";
  page += "form.tools{display:inline}</style>";
  page += "<h1>HALO</h1><p>";
  page += hostname_;
  page += " · ";
  page += ip_;
  page += "</p><ul>";
  page += "<li>AQI ";
  page += r.aqiOk ? String(r.aqi) : String("--");
  page += "<li>CO2 ";
  page += r.co2Ok ? String(r.co2, 0) : String("--");
  page += "<li>PM2.5 ";
  page += r.senOk ? String(r.pm25, 1) : String("--");
  page += "<li>Temp C ";
  page += r.senOk ? String(r.senTempC, 1) : String("--");
  page += "<li>MQTT ";
  page += mqtt_.connected() ? "connected" : "offline";
  page += "</ul>";
  appendRingForm(page);
  page += "<p><a href=/setup>Settings</a> ";
  page += "<form class=tools method=post action=/reboot><button>Restart</button></form> ";
  page += "<form class=tools method=post action=/forget><button>Forget Wi-Fi</button></form></p>";
  server_.send(200, "text/html", page);
}

void NetworkService::handleSave() {
  Settings& cfg = settings();
  if (server_.hasArg("ssid") && server_.arg("ssid").length() > 0) copyField(cfg.wifiSsid, sizeof(cfg.wifiSsid), server_.arg("ssid"));
  if (server_.hasArg("pass") && server_.arg("pass").length() > 0) copyField(cfg.wifiPass, sizeof(cfg.wifiPass), server_.arg("pass"));
  if (server_.hasArg("mqtt_host")) copyField(cfg.mqttHost, sizeof(cfg.mqttHost), server_.arg("mqtt_host"));
  if (server_.hasArg("mqtt_port")) {
    int port = server_.arg("mqtt_port").toInt();
    if (port > 0 && port < 65536) cfg.mqttPort = (uint16_t)port;
  }
  if (server_.hasArg("mqtt_user")) copyField(cfg.mqttUser, sizeof(cfg.mqttUser), server_.arg("mqtt_user"));
  if (server_.hasArg("mqtt_pass") && server_.arg("mqtt_pass").length() > 0) {
    copyField(cfg.mqttPass, sizeof(cfg.mqttPass), server_.arg("mqtt_pass"));
  }
  if (server_.hasArg("tz") && server_.arg("tz").length() > 0) copyField(cfg.tz, sizeof(cfg.tz), server_.arg("tz"));
  cfg.save();
  server_.send(200, "text/html", "<p>Saved. Restarting.</p>");
  reboot_ = true;
}

void NetworkService::handleApi() {
  const Readings& r = sensors().latest();
  JsonDocument doc;
  doc["firmware"] = HALO_FW_VERSION;
  doc["ip"] = ip_;
  doc["wifi"] = wifiConnected();
  doc["mqtt"] = mqtt_.connected();
  doc["ring_frames"] = ring().frames();
  doc["ring_late"] = ring().lateFrames();
  doc["ring_max_gap_ms"] = ring().maxGapMs();
  doc["ring_last_gap_ms"] = ring().lastGapMs();
  doc["ring_max_freeze_us"] = ring().maxFreezeUs();
  doc["ring_freeze_fail"] = ring().freezeFails();
  doc["ring_late_stage"] = ring().lateStage();
  doc["ring_core"] = ring().core();
  doc["ring_rmt_hz"] = ring().rmtHz();
  doc["stage_net_us"] = ring().stageMaxUs(1);
  doc["stage_sensor_us"] = ring().stageMaxUs(2);
  doc["stage_ui_us"] = ring().stageMaxUs(3);
  if (r.aqiOk) doc["aqi"] = r.aqi;
  if (r.co2Ok) doc["co2"] = r.co2;
  if (r.senOk) {
    doc["pm25"] = r.pm25;
    doc["temperature_c"] = r.senTempC;
    doc["humidity"] = r.senHum;
    doc["voc"] = r.voc;
    doc["voc_quality"] = r.vocQuality;
  }
  String body;
  serializeJson(doc, body);
  server_.send(200, "application/json", body);
}

void NetworkService::loop() {
  if (apOn_) dns_.processNextRequest();
  server_.handleClient();

  if (wifiConnected()) {
    if (!staReady_) onStaReady();
    wifiDownSince_ = 0;
    wifiRetryMs_ = millis();
    ArduinoOTA.handle();
  } else {
    staReady_ = false;
    wl_status_t st = WiFi.status();
    bool joining = st == WL_IDLE_STATUS || st == WL_SCAN_COMPLETED;
    if (settings().wifiSsid[0] && wifiDownSince_ == 0) wifiDownSince_ = millis();
    if (settings().wifiSsid[0] == 0) {
      startAp();
    } else if (!joining && millis() - wifiRetryMs_ > 15000) {
      wifiRetryMs_ = millis();
      WiFi.begin(settings().wifiSsid, settings().wifiPass);
    }
    if (settings().wifiSsid[0] && wifiDownSince_ && millis() - wifiDownSince_ > 60000) startAp();
  }

  if (wifiConnected() && mqttConfigured()) {
    if (!mqtt_.connected()) {
      if (millis() - lastMqttTryMs_ > 5000) {
        lastMqttTryMs_ = millis();
        connectMqtt();
      }
    } else {
      mqtt_.loop();
      if (sensors().history().flushPending()) {
        char batch[960];
        if (sensors().history().nextBatch(batch, sizeof(batch))) {
          char topic[64];
          snprintf(topic, sizeof(topic), "%s/history", root_);
          if (mqtt_.publish(topic, batch)) sensors().history().commitBatch();
        }
      }
      if (ring().takeDirty()) publishRing();
      if (backlight().level() != publishedBacklight_) publishBacklight();
      uint32_t now = millis();
      if (now - lastStateMs_ > 10000) {
        publishStates();
        lastStateMs_ = now;
      }
      if (now - lastDiscoveryMs_ > 600000) {
        publishDiscovery();
        lastDiscoveryMs_ = now;
      }
    }
  }
}
