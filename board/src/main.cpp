#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include "secrets.h"

#define WIFI_SSID "Wokwi-GUEST"
#define WIFI_PASSWORD ""
#define WIFI_CHANNEL 6

#define DHTPIN 15
#define DHTTYPE DHT22
#define FAN_LED_PIN 2

const char *MQTT_NAME = "tammy";
const char *BROKER_HOST = "135.232.203.144";
const uint16_t BROKER_PORT = 1883;

const char *API_HOST = "iot-shaquira.northcentralus.cloudapp.azure.com";
const uint16_t API_PORT = 80;

DHT dht(DHTPIN, DHTTYPE);
WiFiClient net;
PubSubClient mqtt(net);

String settingsTopic, stateTopic, statusTopic;
unsigned long lastReadMs = 0;
const unsigned long READ_EVERY_MS = 10000;

String mode = "off";
float maxHumidity = 100.0;
bool hasSettings = false;
bool fanOn = false;
bool hasPublishedState = false;
bool lastPublishedState = false;

void applyFan(bool on) {
  fanOn = on;
  digitalWrite(FAN_LED_PIN, on ? HIGH : LOW);
  if (!hasPublishedState || on != lastPublishedState) {
    mqtt.publish(stateTopic.c_str(), on ? "on" : "off", true);
    lastPublishedState = on;
    hasPublishedState = true;
  }
}

void evaluateFan(float humidity) {
  if (!hasSettings) { applyFan(false); return; }
  if (mode == "on") applyFan(true);
  else if (mode == "off") applyFan(false);
  else applyFan(humidity > maxHumidity);   // auto
}

void onMessage(char *topic, byte *payload, unsigned int length) {
  String body;
  for (unsigned int i = 0; i < length; i++) body += (char)payload[i];

  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, body)) return;                                    // not JSON
  if (!doc.containsKey("mode") || !doc.containsKey("max_humidity")) return;  // missing field

  String newMode = doc["mode"].as<String>();
  if (newMode != "auto" && newMode != "on" && newMode != "off") return;      // bad mode

  mode = newMode;
  maxHumidity = doc["max_humidity"].as<float>();
  hasSettings = true;

  float h = dht.readHumidity();
  if (!isnan(h)) evaluateFan(h);   // react immediately, not next reading
}

void connectMqtt() {
  String clientId = "test2-" + String(MQTT_NAME) + "-" + String((uint32_t)esp_random(), HEX);
  if (mqtt.connect(clientId.c_str(), nullptr, nullptr, statusTopic.c_str(), 1, true, "offline")) {
    mqtt.subscribe(settingsTopic.c_str());
    mqtt.publish(statusTopic.c_str(), "online", true);
    Serial.println("MQTT connected");
  } else {
    Serial.printf("MQTT failed, state %d\n", mqtt.state());
  }
}

void postReading(float humidity, float temp) {
  HTTPClient http;
  http.begin("http://" + String(API_HOST) + ":" + String(API_PORT) + "/readings");
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-API-Key", DEVICE_KEY);
  String body = "{\"mac\":\"" + WiFi.macAddress() + "\",\"humidity\":" + String(humidity, 1) +
                ",\"temp\":" + String(temp, 1) + "}";
  int code = http.POST(body);
  Serial.printf("POST /readings -> %d\n", code);
  Serial.println(http.getString());
  Serial.println(body);
  http.end();
}

void setup() {
  Serial.begin(115200);
  pinMode(FAN_LED_PIN, OUTPUT);
  dht.begin();

  settingsTopic = "ecse3038/" + String(MQTT_NAME) + "/fan/settings";
  stateTopic    = "ecse3038/" + String(MQTT_NAME) + "/fan/state";
  statusTopic   = "ecse3038/" + String(MQTT_NAME) + "/status";

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD, WIFI_CHANNEL);
  Serial.print("WiFi");
  while (WiFi.status() != WL_CONNECTED) { delay(250); Serial.print("."); }
  Serial.println(" connected");
  Serial.println(WiFi.macAddress());

  mqtt.setServer(BROKER_HOST, BROKER_PORT);
  mqtt.setCallback(onMessage);
  connectMqtt();
}

void loop() {
  if (!mqtt.connected()) connectMqtt();
  mqtt.loop();

  unsigned long now = millis();
  if (now - lastReadMs >= READ_EVERY_MS) {
    lastReadMs = now;
    float h = dht.readHumidity();
    float t = dht.readTemperature();
    if (!isnan(h) && !isnan(t)) {
      postReading(h, t);
      evaluateFan(h);
    }
  }
}