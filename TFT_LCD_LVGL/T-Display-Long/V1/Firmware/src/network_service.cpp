#include "network_service.h"

#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "pages_ca.h"

#include "aqi.h"
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

const char kVersionUrl[] = "https://yashmulgaonkar.github.io/halo/firmware-lvgl/version.txt";
const char kFirmwareUrl[] = "https://yashmulgaonkar.github.io/halo/firmware-lvgl/firmware.bin";

bool clockReady() {
  return time(nullptr) > 1704067200;
}

void trimInPlace(char* text) {
  char* start = text;
  while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') start++;
  if (start != text) memmove(text, start, strlen(start) + 1);
  size_t n = strlen(text);
  while (n > 0 && (text[n - 1] == ' ' || text[n - 1] == '\t' || text[n - 1] == '\r' || text[n - 1] == '\n')) {
    text[--n] = 0;
  }
}

bool validVersion(const char* text) {
  if (text == nullptr || text[0] == 0) return false;
  int dots = 0;
  int digits = 0;
  for (const char* p = text; *p; p++) {
    if (p - text > 20) return false;
    if (*p == '.') {
      if (digits == 0) return false;
      dots++;
      digits = 0;
    } else if (*p >= '0' && *p <= '9') {
      if (++digits > 6) return false;
    } else {
      return false;
    }
  }
  return dots >= 1 && digits > 0;
}

int cmpVersion(const char* left, const char* right) {
  const char* a = left == nullptr ? "" : left;
  const char* b = right == nullptr ? "" : right;
  for (int i = 0; i < 4; i++) {
    char* end = nullptr;
    long av = strtol(a, &end, 10);
    a = end == nullptr ? a : end;
    long bv = strtol(b, &end, 10);
    b = end == nullptr ? b : end;
    if (av != bv) return av > bv ? 1 : -1;
    if (*a == '.') a++;
    if (*b == '.') b++;
  }
  return 0;
}

void copyNote(char* dst, size_t n, const char* src) {
  if (src == nullptr) src = "";
  strncpy(dst, src, n - 1);
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

const char kPortalCss[] =
    ":root{color-scheme:dark}"
    "*{box-sizing:border-box}"
    "body{margin:0;background:#000;color:#f4f7fb;font:15px/1.45 ui-sans-serif,system-ui,sans-serif}"
    "main{max-width:920px;margin:0 auto;padding:22px 16px 56px}"
    "header{display:flex;justify-content:space-between;gap:16px;align-items:flex-end;flex-wrap:wrap;margin-bottom:18px}"
    "h1{margin:0;font-size:20px;letter-spacing:.18em;font-weight:700}"
    "h2{margin:26px 0 10px;font-size:12px;letter-spacing:.14em;color:#93a0b5;font-weight:650}"
    ".sub{margin:4px 0 0;color:#93a0b5}"
    "nav{display:flex;gap:14px;margin-top:8px}"
    "nav a{color:#93a0b5;text-decoration:none}"
    "nav a.on{color:#f4f7fb}"
    ".pills{display:flex;gap:8px;flex-wrap:wrap}"
    ".pill{border-radius:999px;padding:5px 10px;background:#141820;color:#93a0b5;font-size:12px}"
    ".pill.on{background:#3ddc97;color:#04140c}"
    ".pill.off{background:#f07167;color:#fff}"
    ".hero{display:flex;justify-content:space-between;align-items:flex-end;gap:12px;background:#141820;"
    "border-radius:22px;padding:22px;margin-bottom:12px;border-left:6px solid #93a0b5}"
    ".hero .k{color:#93a0b5;letter-spacing:.14em;font-size:12px}"
    ".hero b{display:block;font-size:68px;line-height:.85;margin:6px 0}"
    ".hero .cat{font-size:18px}"
    ".grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(148px,1fr));gap:10px}"
    ".card{background:#141820;border-radius:16px;padding:12px 14px 10px;border-left:4px solid #2c3544}"
    ".card .n{color:#93a0b5;font-size:11px;letter-spacing:.08em}"
    ".card .v{font-size:28px;font-weight:700;line-height:1.1;margin-top:4px}"
    ".card .u{color:#93a0b5;font-size:12px}"
    ".wide{grid-column:1/-1}"
    "label{display:block;color:#93a0b5;font-size:12px;margin-top:10px}"
    "input{width:100%;margin:4px 0 0;padding:11px 12px;border:0;border-radius:10px;background:#0c0e12;color:#fff}"
    "input[type=range]{height:32px;padding:0;background:transparent;accent-color:#3ddc97}"
    "button,.btn{display:inline-block;background:#3ddc97;color:#04140c;border:0;border-radius:10px;"
    "padding:10px 14px;font-weight:700;text-decoration:none;cursor:pointer}"
    "button.warn{background:#f07167}"
    ".actions{display:flex;gap:8px;flex-wrap:wrap;align-items:center;margin-top:18px}"
    "form.inline{display:inline}"
    ".panel{background:#141820;border-radius:18px;padding:16px 16px 18px}"
    ".split{display:grid;grid-template-columns:1fr 1fr;gap:10px}"
    "@media(max-width:640px){.hero b{font-size:52px}.split{grid-template-columns:1fr}}";

void portalOpen(String& page, const char* title, bool live) {
  page += "<!doctype html><html><head><meta charset=utf-8>";
  page += "<meta name=viewport content=\"width=device-width,initial-scale=1\">";
  if (live) page += "<meta http-equiv=refresh content=15>";
  page += "<title>";
  page += title;
  page += "</title><style>";
  page += kPortalCss;
  page += "</style></head><body><main>";
}

void portalClose(String& page) { page += "</main></body></html>"; }

const char* aqiCss(int aqi, bool evacuate) {
  if (evacuate || aqi > 300) return "#c43b4e";
  if (aqi <= 50) return "#3ddc97";
  if (aqi <= 100) return "#e6c85c";
  if (aqi <= 150) return "#f0a04b";
  if (aqi <= 200) return "#f07167";
  return "#c084fc";
}

const char* bandCss(float value, float green, float yellow, float orange) {
  if (value <= green) return "#3ddc97";
  if (value <= yellow) return "#e6c85c";
  if (orange >= 0.f && value <= orange) return "#f0a04b";
  return "#f07167";
}

const char* tempCss(float celsius) {
  if (celsius >= 18.f && celsius <= 24.f) return "#3ddc97";
  if ((celsius > 24.f && celsius <= 27.f) || (celsius >= 16.f && celsius < 18.f)) return "#e6c85c";
  if ((celsius > 27.f && celsius <= 30.f) || (celsius >= 10.f && celsius < 16.f)) return "#f0a04b";
  return "#f07167";
}

const char* humCss(float humidity) {
  if (humidity >= 30.f && humidity <= 50.f) return "#3ddc97";
  if ((humidity > 50.f && humidity <= 60.f) || (humidity >= 20.f && humidity < 30.f)) return "#e6c85c";
  if ((humidity > 60.f && humidity <= 70.f) || (humidity >= 10.f && humidity < 20.f)) return "#f0a04b";
  return "#f07167";
}

String readingText(bool ok, float value, int decimals) {
  if (!ok) return "--";
  String text(value, (unsigned int)decimals);
  text.trim();
  return text;
}

void metric(String& page, const char* name, const String& value, const char* unit, const char* color) {
  page += "<article class=card style=\"border-color:";
  page += color;
  page += "\"><div class=n>";
  page += name;
  page += "</div><div class=v style=\"color:";
  page += color;
  page += "\">";
  page += value;
  page += "</div><div class=u>";
  page += unit;
  page += "</div></article>";
}

void portalNav(String& page, bool setup) {
  page += "<nav><a href=/";
  if (!setup) page += " class=on";
  page += ">Live</a><a href=/setup";
  if (setup) page += " class=on";
  page += ">Settings</a></nav>";
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
  server_.on("/update", HTTP_POST, [this]() { handleUpdate(); });
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
  updateReadyMs_ = millis();
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
    doc["name"] = "Firmware";
    doc["device_class"] = "firmware";
    char uid[48];
    snprintf(uid, sizeof(uid), "%s_fw_update", deviceId_);
    doc["unique_id"] = uid;
    char state[64];
    snprintf(state, sizeof(state), "%s/update", root_);
    doc["state_topic"] = state;
    doc["value_template"] = "{{ value_json.installed_version }}";
    doc["latest_version_topic"] = state;
    doc["latest_version_template"] = "{{ value_json.latest_version }}";
    char cmd[80];
    snprintf(cmd, sizeof(cmd), "%s/cmd/update/install", root_);
    doc["command_topic"] = cmd;
    doc["payload_install"] = "INSTALL";
    doc["availability_topic"] = statusTopic_;
    addDevice(doc);
    char config[96];
    snprintf(config, sizeof(config), "homeassistant/update/%s/firmware/config", deviceId_);
    char body[1200];
    size_t n = serializeJson(doc, body, sizeof(body));
    if (n > 0 && n < sizeof(body)) mqtt_.publish(config, body, true);
    displayPump();
  }

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
  publishFirmwareUpdate();

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
  } else if (strcmp(cmd, "update/install") == 0) {
    if (strcmp(body, "INSTALL") == 0 && !updating_) installRequested_ = true;
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
  page.reserve(8000);
  portalOpen(page, "HALO settings", false);
  page += "<header><div><h1>HALO</h1><p class=sub>";
  page += htmlEscape(hostname_);
  page += "</p>";
  portalNav(page, true);
  page += "</div></header>";
  page += "<p class=sub>Join Wi-Fi, then point this device at your MQTT broker. Home Assistant discovery is automatic.</p>";
  page += "<form class=panel method=post action=/save><div class=split>";
  page += "<div><label>Wi-Fi name</label><input name=ssid value=\"" + htmlEscape(cfg.wifiSsid) + "\"></div>";
  page += "<div><label>Wi-Fi password</label><input name=pass type=password placeholder=unchanged autocomplete=new-password></div>";
  page += "<div><label>MQTT host</label><input name=mqtt_host value=\"" + htmlEscape(cfg.mqttHost) + "\"></div>";
  char port[8];
  snprintf(port, sizeof(port), "%u", cfg.mqttPort);
  page += "<div><label>MQTT port</label><input name=mqtt_port inputmode=numeric value=\"";
  page += port;
  page += "\"></div>";
  page += "<div><label>MQTT user</label><input name=mqtt_user value=\"" + htmlEscape(cfg.mqttUser) + "\"></div>";
  page += "<div><label>MQTT password</label><input name=mqtt_pass type=password placeholder=unchanged autocomplete=new-password></div>";
  page += "</div><label>Timezone</label><input name=tz value=\"" + htmlEscape(cfg.tz) + "\">";
  page += "<div class=actions><button type=submit>Save and restart</button></div></form>";
  page += "<h2>RING</h2>";
  appendRingForm(page);
  portalClose(page);
  server_.send(200, "text/html", page);
}

void NetworkService::appendRingForm(String& page) {
  uint8_t bright = ring().brightness();
  if (bright == 0) bright = 160;
  page += "<form class=panel method=post action=/ring>";
  page += "<p class=sub>Color follows the worse of AQI and CO2. Brightness stays where you set it.</p>";
  page += "<label>Brightness</label><input name=brightness type=range min=1 max=255 value=\"";
  page += String(bright);
  page += "\">";
  page += "<div class=actions><button type=submit>Apply brightness</button>";
  page += "<button class=warn type=submit name=mode value=off>Turn ring off</button></div></form>";
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
    if (ring().brightness() == 0) ring().setBrightness(160);
    ring().setEffect("Rainbow");
    ring().setPower(true);
  } else if (server_.hasArg("color")) {
    uint8_t r = 255, g = 255, b = 255;
    String color = server_.arg("color");
    if (color.length() == 7 && color.charAt(0) == '#') {
      r = (uint8_t)strtol(color.substring(1, 3).c_str(), nullptr, 16);
      g = (uint8_t)strtol(color.substring(3, 5).c_str(), nullptr, 16);
      b = (uint8_t)strtol(color.substring(5, 7).c_str(), nullptr, 16);
    }
    if (ring().brightness() == 0) ring().setBrightness(160);
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
  const Settings& cfg = settings();
  String page;
  page.reserve(14000);
  portalOpen(page, "HALO", true);
  page += "<header><div><h1>HALO</h1><p class=sub>";
  page += htmlEscape(hostname_);
  page += " &middot; ";
  page += htmlEscape(ip_);
  page += " &middot; ";
  page += HALO_FW_VERSION;
  page += "</p>";
  portalNav(page, false);
  page += "</div><div class=pills>";
  page += wifiConnected() ? "<span class=\"pill on\">Wi-Fi</span>" : "<span class=\"pill off\">Wi-Fi</span>";
  page += mqtt_.connected() ? "<span class=\"pill on\">MQTT</span>" : "<span class=\"pill off\">MQTT</span>";
  page += "</div></header>";

  const char* aqiColor = r.aqiOk ? aqiCss(r.aqi, r.aqiEvac) : "#93a0b5";
  page += "<section class=hero style=\"border-color:";
  page += aqiColor;
  page += "\"><div><div class=k>AIR QUALITY</div><b style=\"color:";
  page += aqiColor;
  page += "\">";
  if (r.aqiOk) page += r.aqiEvac ? "EVAC" : String(r.aqi);
  else page += "--";
  page += "</b><div class=cat style=\"color:";
  page += aqiColor;
  page += "\">";
  if (r.aqiOk) page += aqiCategory(r.aqi, r.aqiEvac);
  else page += r.senPresent ? "Warming up" : "Particle sensor missing";
  page += "</div></div><div class=sub>";
  page += r.nowcastOk ? String("NowCast ") + String(r.nowcast) : String("NowCast after 12 hours");
  page += "</div></section><section class=grid>";

  float shownTemp = r.senTempC;
  const char* tempUnit = "C";
  if (cfg.tempUnit == TEMP_F) {
    shownTemp = r.senTempC * 9.f / 5.f + 32.f;
    tempUnit = "F";
  } else if (cfg.tempUnit == TEMP_K) {
    shownTemp = r.senTempC + 273.15f;
    tempUnit = "K";
  }
  metric(page, "CO2", readingText(r.co2Ok, r.co2, 0), "ppm", r.co2Ok ? bandCss(r.co2, 600, 1000, 1500) : "#93a0b5");
  metric(page, "TEMP", readingText(r.senOk, shownTemp, 1), tempUnit, r.senOk ? tempCss(r.senTempC) : "#93a0b5");
  metric(page, "HUMIDITY", readingText(r.senOk, r.senHum, 0), "%", r.senOk ? humCss(r.senHum) : "#93a0b5");
  metric(page, "PRESSURE", readingText(r.bmeOk, r.bmePressureHpa, 0), "hPa", r.bmeOk ? "#f4f7fb" : "#93a0b5");
  metric(page, "PM1", readingText(r.senOk, r.pm1, 1), "ug/m3", r.senOk ? bandCss(r.pm1, 12, 35, 55) : "#93a0b5");
  metric(page, "PM2.5", readingText(r.senOk, r.pm25, 1), "ug/m3",
         (r.senOk && r.aqiOk) ? aqiCss(r.aqi, r.aqiEvac) : (r.senOk ? bandCss(r.pm25, 12, 35, 55) : "#93a0b5"));
  metric(page, "PM4", readingText(r.senOk, r.pm4, 1), "ug/m3", r.senOk ? bandCss(r.pm4, 25, 50, 75) : "#93a0b5");
  metric(page, "PM10", readingText(r.senOk, r.pm10, 1), "ug/m3", r.senOk ? bandCss(r.pm10, 54, 154, 254) : "#93a0b5");
  metric(page, "VOC", readingText(r.senOk, r.voc, 0), r.senOk ? r.vocQuality : "--",
         r.senOk ? bandCss(r.voc, 150, 250, 400) : "#93a0b5");
  page += "</section><h2>GASES</h2><section class=grid>";

  const char* gasNames[] = {"CO", "NO2", "H2", "CH4", "ETHANOL", "NH3", "NOX"};
  const char* gasUnits[] = {"ppm", "ppm", "ppm", "ppm", "ppm", "ppm", "idx"};
  const float gasValues[] = {r.co, r.no2, r.h2, r.ch4, r.ethanol, r.nh3, r.nox};
  const bool gasOk[] = {r.micsOk, r.micsOk, r.micsOk, r.micsOk, r.micsOk, r.micsOk, r.senOk && r.noxOk};
  const float g1[] = {15, 0.03f, 0.4f, 2, 0.5f, 0.5f, 150};
  const float g2[] = {30, 0.06f, 1, 10, 1.5f, 1, 250};
  const float g3[] = {36, 0.1f, 2, -1, 3, 2, 400};
  const int gasDecimals[] = {2, 2, 2, 2, 2, 2, 1};
  for (int i = 0; i < 7; i++) {
    metric(page, gasNames[i], readingText(gasOk[i], gasValues[i], gasDecimals[i]), gasUnits[i],
           gasOk[i] ? bandCss(gasValues[i], g1[i], g2[i], g3[i]) : "#93a0b5");
  }
  page += "</section><h2>DEVICE</h2><section class=grid>";

  unsigned long sec = millis() / 1000UL;
  unsigned long days = sec / 86400UL;
  unsigned long hours = (sec % 86400UL) / 3600UL;
  unsigned long mins = (sec % 3600UL) / 60UL;
  char uptime[40];
  if (days) snprintf(uptime, sizeof(uptime), "%lud %luh %lum", days, hours, mins);
  else snprintf(uptime, sizeof(uptime), "%luh %lum", hours, mins);

  const char* mics = "missing";
  const char* micsColor = "#f07167";
  if (r.micsPresent && r.micsReady) {
    mics = r.micsOk ? "live" : "calibrating";
    micsColor = r.micsOk ? "#3ddc97" : "#e6c85c";
  } else if (r.micsPresent) {
    mics = "warming";
    micsColor = "#e6c85c";
  }
  String particle = htmlEscape(r.senName[0] ? r.senName : "PARTICLES");
  metric(page, "UPTIME", uptime, "", "#f4f7fb");
  metric(page, "WIFI", wifiConnected() ? "connected" : "offline", htmlEscape(cfg.wifiSsid).c_str(),
         wifiConnected() ? "#3ddc97" : "#f07167");
  metric(page, "MQTT", mqtt_.connected() ? "connected" : "offline", htmlEscape(cfg.mqttHost).c_str(),
         mqtt_.connected() ? "#3ddc97" : "#f07167");
  metric(page, "BACKLIGHT", String(backlight().level()), "/ 255", backlight().level() ? "#f4f7fb" : "#93a0b5");
  metric(page, "SCD40", r.scdPresent ? "ok" : "missing", "CO2", r.scdPresent ? "#3ddc97" : "#f07167");
  metric(page, particle.c_str(), r.senPresent ? "ok" : "missing", "PM / VOC", r.senPresent ? "#3ddc97" : "#f07167");
  metric(page, "BME280", r.bmePresent ? "ok" : "missing", "pressure", r.bmePresent ? "#3ddc97" : "#f07167");
  metric(page, "MICS-4514", mics, "gases", micsColor);
  page += "</section><h2>FIRMWARE</h2><section class=panel>";
  page += "<p>Running ";
  page += HALO_FW_VERSION;
  page += ".</p>";
  if (updateKnown_) {
    page += "<p class=sub>Site version ";
    page += htmlEscape(latestVersion_);
    page += ". ";
    int cmp = cmpVersion(latestVersion_, HALO_FW_VERSION);
    if (cmp > 0) page += "A newer build is ready.";
    else if (cmp < 0) page += "This device is newer than the site.";
    else page += "This build matches the site.";
    page += "</p>";
  } else if (updateNote_[0] == 0) {
    page += "<p class=sub>The site version has not been read yet.</p>";
  }
  if (updateNote_[0]) {
    page += "<p class=sub>";
    page += htmlEscape(updateNote_);
    page += "</p>";
  }
  page += "<form class=actions method=post action=/update onsubmit=\"return confirm('Download the firmware from the site and restart HALO?')\"><button>Update firmware</button></form>";
  page += "</section><h2>RING</h2>";
  appendRingForm(page);
  page += "<div class=actions>";
  page += "<form class=inline method=post action=/reboot onsubmit=\"return confirm('Restart HALO?')\"><button>Restart</button></form>";
  page += "<form class=inline method=post action=/forget onsubmit=\"return confirm('Forget Wi-Fi and open the setup hotspot?')\"><button class=warn>Forget Wi-Fi</button></form>";
  page += "</div>";
  portalClose(page);
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
  String page;
  page.reserve(2500);
  portalOpen(page, "HALO", false);
  page += "<header><div><h1>HALO</h1></div></header>";
  page += "<section class=panel><h2>SAVED</h2><p>Settings are stored. HALO is restarting.</p></section>";
  portalClose(page);
  server_.send(200, "text/html", page);
  reboot_ = true;
}

void NetworkService::sendUpdatePage(const char* heading, const char* message) {
  String page;
  page.reserve(2500);
  portalOpen(page, "HALO", false);
  page += "<header><div><h1>HALO</h1></div></header><section class=panel><h2>";
  page += heading;
  page += "</h2><p>";
  page += htmlEscape(message);
  page += "</p><p class=sub><a href=/>Back to live readings</a></p></section>";
  portalClose(page);
  server_.send(200, "text/html", page);
}

void NetworkService::refreshLatest() {
  if (!clockReady()) {
    if (!updateKnown_) copyNote(updateNote_, sizeof(updateNote_), "Waiting for the clock before checking the site.");
    return;
  }
  char url[128];
  snprintf(url, sizeof(url), "%s?t=%lu", kVersionUrl, (unsigned long)millis());
  WiFiClientSecure client;
  client.setCACert(kPagesCa);
  client.setHandshakeTimeout(20000);
  client.setTimeout(20);
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(20000);
  if (!http.begin(client, url)) {
    if (!updateKnown_) copyNote(updateNote_, sizeof(updateNote_), "Could not start the version check.");
    return;
  }
  int code = http.GET();
  int len = http.getSize();
  if (code == 404) {
    http.end();
    if (!updateKnown_) copyNote(updateNote_, sizeof(updateNote_), "The site does not have a Wi-Fi firmware yet.");
    return;
  }
  if (code != 200 || len <= 0 || len > 32) {
    http.end();
    if (!updateKnown_) copyNote(updateNote_, sizeof(updateNote_), "Could not read the firmware version on the site.");
    return;
  }
  char body[40];
  int got = http.getStreamPtr()->readBytes(body, len);
  http.end();
  if (got != len) {
    if (!updateKnown_) copyNote(updateNote_, sizeof(updateNote_), "Could not read the firmware version on the site.");
    return;
  }
  body[got] = 0;
  trimInPlace(body);
  if (!validVersion(body)) {
    if (!updateKnown_) copyNote(updateNote_, sizeof(updateNote_), "The site version was not usable.");
    return;
  }
  strncpy(latestVersion_, body, sizeof(latestVersion_) - 1);
  latestVersion_[sizeof(latestVersion_) - 1] = 0;
  updateKnown_ = true;
  updateNote_[0] = 0;
  Serial.printf("Firmware site version %s\n", latestVersion_);
  if (mqtt_.connected()) publishFirmwareUpdate();
}

void NetworkService::installFirmware() {
  if (updating_ || !wifiConnected()) return;
  updating_ = true;
  refreshLatest();
  if (!updateKnown_ || cmpVersion(latestVersion_, HALO_FW_VERSION) <= 0) {
    updating_ = false;
    if (mqtt_.connected()) publishFirmwareUpdate();
    if (updateKnown_) {
      copyNote(updateNote_, sizeof(updateNote_), cmpVersion(latestVersion_, HALO_FW_VERSION) < 0
                                                         ? "This device is newer than the site."
                                                         : "This firmware is current.");
    }
    return;
  }
  char url[160];
  snprintf(url, sizeof(url), "%s?v=%s", kFirmwareUrl, latestVersion_);
  Serial.printf("Firmware download %s\n", url);
  if (mqtt_.connected()) mqtt_.disconnect();
  ring().suspend(true);
  WiFiClientSecure client;
  client.setCACert(kPagesCa);
  client.setHandshakeTimeout(20000);
  client.setTimeout(180);
  HTTPUpdate updater(180000);
  updater.rebootOnUpdate(true);
  updater.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  HTTPUpdateResult result = updater.update(client, url);
  ring().suspend(false);
  updating_ = false;
  if (result != HTTP_UPDATE_OK) {
    String err = updater.getLastErrorString();
    if (err.length() == 0) err = "The download did not finish.";
    copyNote(updateNote_, sizeof(updateNote_), err.c_str());
    Serial.printf("Firmware update failed: %s\n", updateNote_);
  }
}

void NetworkService::publishFirmwareUpdate() {
  if (!mqtt_.connected()) return;
  JsonDocument doc;
  doc["installed_version"] = HALO_FW_VERSION;
  const char* latest = HALO_FW_VERSION;
  if (updateKnown_ && cmpVersion(latestVersion_, HALO_FW_VERSION) > 0) latest = latestVersion_;
  doc["latest_version"] = latest;
  doc["title"] = "HALO LVGL firmware (new UI)";
  if (strcmp(latest, HALO_FW_VERSION) != 0) {
    char rel[96];
    snprintf(rel, sizeof(rel), "https://github.com/yashmulgaonkar/halo/releases/tag/lvgl-%s", latest);
    doc["release_url"] = rel;
  }
  char topic[64];
  snprintf(topic, sizeof(topic), "%s/update", root_);
  char body[384];
  size_t n = serializeJson(doc, body, sizeof(body));
  if (n > 0 && n < sizeof(body)) mqtt_.publish(topic, body, true);
}

void NetworkService::handleUpdate() {
  if (updating_ || installRequested_) {
    sendUpdatePage("FIRMWARE", "An update is already running.");
    return;
  }
  refreshLatest();
  if (!updateKnown_) {
    sendUpdatePage("FIRMWARE", updateNote_[0] ? updateNote_ : "Could not read the firmware version on the site.");
    return;
  }
  if (cmpVersion(latestVersion_, HALO_FW_VERSION) <= 0) {
    sendUpdatePage("FIRMWARE", cmpVersion(latestVersion_, HALO_FW_VERSION) < 0
                                   ? "This device is newer than the site."
                                   : "This firmware is current.");
    return;
  }
  sendUpdatePage("FIRMWARE", "Downloading the site firmware. Leave HALO powered. It restarts when the install finishes, then reopen this page.");
  delay(200);
  server_.client().stop();
  installRequested_ = true;
}

void NetworkService::handleApi() {
  const Readings& r = sensors().latest();
  JsonDocument doc;
  doc["firmware"] = HALO_FW_VERSION;
  if (updateKnown_) doc["firmware_latest"] = latestVersion_;
  const char* updateState = "unknown";
  if (updateKnown_) {
    int cmp = cmpVersion(latestVersion_, HALO_FW_VERSION);
    if (cmp > 0) updateState = "available";
    else if (cmp < 0) updateState = "ahead";
    else updateState = "current";
  }
  doc["firmware_update"] = updateState;
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
  if (installRequested_ && !updating_) {
    installRequested_ = false;
    installFirmware();
  }

  if (wifiConnected()) {
    if (!staReady_) onStaReady();
    wifiDownSince_ = 0;
    wifiRetryMs_ = millis();
    ArduinoOTA.handle();
    uint32_t nowCheck = millis();
    uint32_t period = updateKnown_ ? 21600000UL : 60000UL;
    if (!updating_ && updateReadyMs_ != 0 && nowCheck - updateReadyMs_ > 15000 &&
        (lastUpdateCheckMs_ == 0 || nowCheck - lastUpdateCheckMs_ >= period)) {
      lastUpdateCheckMs_ = nowCheck;
      refreshLatest();
    }
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
