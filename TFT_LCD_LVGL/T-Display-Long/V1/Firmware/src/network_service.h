#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <PubSubClient.h>
#include <WebServer.h>
#include <WiFi.h>

class NetworkService {
 public:
  void begin();
  void loop();
  bool wifiConnected() const;
  bool mqttConnected();
  bool mqttConfigured() const;
  const char* ip() const { return ip_; }
  bool takeReboot();
  void publishTelemetry();
  void forgetWifi();

 private:
  void startAp();
  void stopAp();
  void onStaReady();
  void connectMqtt();
  void publishDiscovery();
  void publishStates();
  void publishRing();
  void publishBacklight();
  void onMqtt(char* topic, uint8_t* payload, unsigned int length);
  void handleCmd(const char* cmd, const char* body);
  void sendSetup();
  void sendStatus();
  void appendRingForm(String& page);
  void handleSave();
  void handleRing();
  void handleApi();
  bool publish(const char* topic, const char* payload, bool retain);
  void addDevice(JsonDocument& doc);
  static void mqttTrampoline(char* topic, uint8_t* payload, unsigned int length);

  static NetworkService* instance_;

  WiFiClient wifiClient_;
  PubSubClient mqtt_;
  WebServer server_{80};
  DNSServer dns_;

  char deviceId_[20] = {};
  char hostname_[20] = {};
  char root_[40] = {};
  char statusTopic_[48] = {};
  char cmdFilter_[48] = {};
  char ip_[16] = "--";

  bool staReady_ = false;
  bool apOn_ = false;
  bool reboot_ = false;
  bool discoverySent_ = false;
  uint32_t lastMqttTryMs_ = 0;
  uint32_t lastStateMs_ = 0;
  uint32_t lastDiscoveryMs_ = 0;
  uint32_t wifiDownSince_ = 0;
  uint32_t wifiRetryMs_ = 0;
  uint8_t publishedBacklight_ = 255;
};

NetworkService& network();
