#include <Arduino.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <BH1750.h>
#include <Wire.h>
#include <WiFi.h>
#include "esp_wpa2.h"
#include <ArduinoJson.h>
#include <time.h>

// Modem & MQTT libraries
#define TINY_GSM_MODEM_SIM7600
#include <TinyGsmClient.h>
#include <PubSubClient.h>
#include <ESP_SSLClient.h>
#include <mbedtls/base64.h>

#include "auth.h"
#include "trust_anchors.h"

// --- Constantes Hardware ---
const int PIN_LED_1 = 14; 
const int PIN_LED_2 = 13; 
const int PIN_PIR = 32;

const int MODEM_TX = 26;
const int MODEM_RX = 27;
const int MODEM_PWRKEY = 4;
const int BOARD_POWERON = 12;

const unsigned long PUBLISH_INTERVAL_MS = 5000;

// --- Objets ---
Adafruit_BME280 bme;
BH1750 lightMeter;

HardwareSerial serialAT(1);
TinyGsm modem(serialAT);
TinyGsmClient gsmClient(modem, 0);
WiFiClient wifiClient;
ESP_SSLClient sslClient;

class WebSocketClient : public Client {
private:
  ESP_SSLClient* _sslClient;
  bool _wsConnected;
  uint8_t _rxBuffer[512];
  size_t _rxBufferLen;
  size_t _rxBufferPos;

  String generateWebSocketKey() {
    uint8_t key[16];
    for(int i = 0; i < 16; i++) key[i] = random(0, 256);
    size_t olen;
    unsigned char output[64];
    mbedtls_base64_encode(output, sizeof(output), &olen, key, 16);
    return String((char*)output);
  }

  bool readWebSocketFrame() {
    if (!_sslClient->available()) return false;
    uint8_t byte1 = _sslClient->read();
    if (!_sslClient->available()) return false;
    uint8_t byte2 = _sslClient->read();
    uint8_t opcode = byte1 & 0x0F;
    bool masked = (byte2 & 0x80) != 0;
    size_t payloadLen = byte2 & 0x7F;
    if (payloadLen == 126) {
      if (_sslClient->available() < 2) return false;
      payloadLen = (_sslClient->read() << 8) | _sslClient->read();
    }
    uint8_t mask[4] = {0};
    if (masked) {
      if (_sslClient->available() < 4) return false;
      for(int i = 0; i < 4; i++) mask[i] = _sslClient->read();
    }
    if (opcode == 0x01 || opcode == 0x02) {
      _rxBufferLen = payloadLen < sizeof(_rxBuffer) ? payloadLen : sizeof(_rxBuffer);
      for(size_t i = 0; i < _rxBufferLen; i++) {
        _rxBuffer[i] = _sslClient->read();
        if (masked) _rxBuffer[i] ^= mask[i % 4];
      }
      _rxBufferPos = 0;
      return true;
    }
    return false;
  }

public:
  WebSocketClient(ESP_SSLClient* sslClient) : _sslClient(sslClient), _wsConnected(false), _rxBufferLen(0), _rxBufferPos(0) {}
  int connect(IPAddress ip, uint16_t port) override { return 0; }
  int connect(const char *host, uint16_t port) override {
    if (!_sslClient->connect(host, port)) return 0;
    String wsKey = generateWebSocketKey();
    _sslClient->print("GET / HTTP/1.1\r\nHost: "); _sslClient->print(host);
    _sslClient->print("\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n");
    _sslClient->print("Sec-WebSocket-Key: "); _sslClient->print(wsKey);
    _sslClient->print("\r\nSec-WebSocket-Protocol: mqtt\r\nSec-WebSocket-Version: 13\r\n\r\n");
    unsigned long timeout = millis();
    while (!_sslClient->available() && millis() - timeout < 5000) delay(10);
    if (!_sslClient->available()) return 0;
    String response = "";
    while (_sslClient->available()) {
      char c = _sslClient->read();
      response += c;
      if (response.endsWith("\r\n\r\n")) break;
    }
    if (response.indexOf("101") > 0) { _wsConnected = true; return 1; }
    return 0;
  }
  size_t write(uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t *buf, size_t size) override {
    if (!_wsConnected) return 0;
    uint8_t header[14]; int headerLen = 2;
    header[0] = 0x82;
    if (size < 126) header[1] = 0x80 | size;
    else { header[1] = 0x80 | 126; header[2] = (size >> 8) & 0xFF; header[3] = size & 0xFF; headerLen = 4; }
    uint8_t mask[4];
    for(int i = 0; i < 4; i++) { mask[i] = random(0, 256); header[headerLen + i] = mask[i]; }
    headerLen += 4;
    _sslClient->write(header, headerLen);
    for(size_t i = 0; i < size; i++) { uint8_t maskedByte = buf[i] ^ mask[i % 4]; _sslClient->write(&maskedByte, 1); }
    return size;
  }
  int available() override {
    if (_rxBufferPos < _rxBufferLen) return _rxBufferLen - _rxBufferPos;
    if (_sslClient->available() && readWebSocketFrame()) return _rxBufferLen - _rxBufferPos;
    return 0;
  }
  int read() override {
    if (_rxBufferPos < _rxBufferLen) return _rxBuffer[_rxBufferPos++];
    if (_sslClient->available() && readWebSocketFrame() && _rxBufferPos < _rxBufferLen) return _rxBuffer[_rxBufferPos++];
    return -1;
  }
  int read(uint8_t *buf, size_t size) override {
    size_t count = 0;
    while (count < size) {
      int v = read();
      if (v < 0) break;
      buf[count++] = (uint8_t)v;
    }
    return count;
  }
  int peek() override {
    if (_rxBufferPos < _rxBufferLen) return _rxBuffer[_rxBufferPos];
    return -1;
  }
  void flush() override { _sslClient->flush(); }
  void stop() override { _wsConnected = false; _sslClient->stop(); }
  uint8_t connected() override { return _wsConnected && _sslClient->connected(); }
  operator bool() override { return _wsConnected; }
};

WebSocketClient wsClient(&sslClient);
PubSubClient mqttClient(wsClient);

unsigned long lastPublishTime = 0;
bool networkModeLTE = false;

void initSerial() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n[SYSTEM] Restauration Stable - Poste 07");
}

void initGPIO() {
  pinMode(BOARD_POWERON, OUTPUT);
  digitalWrite(BOARD_POWERON, HIGH);
  pinMode(PIN_PIR, INPUT);
  pinMode(PIN_LED_1, OUTPUT);
  pinMode(PIN_LED_2, OUTPUT);
  digitalWrite(PIN_LED_1, LOW);
  digitalWrite(PIN_LED_2, LOW);
}

void initI2C() {
  Wire.begin(21, 22);
  bme.begin(0x76, &Wire);
  lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE);
}

void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  static unsigned long lastAttempt = 0;
  if (millis() - lastAttempt < 10000) return; 
  lastAttempt = millis();
  Serial.println("[WIFI] Tentative connexion...");
  WiFi.mode(WIFI_STA);
  if (strlen(WIFI_IDENTITY) > 0 && strcmp(WIFI_IDENTITY, WIFI_SSID) != 0) {
    esp_wifi_sta_wpa2_ent_set_identity((uint8_t *)WIFI_IDENTITY, strlen(WIFI_IDENTITY));
    esp_wifi_sta_wpa2_ent_set_username((uint8_t *)WIFI_IDENTITY, strlen(WIFI_IDENTITY));
    esp_wifi_sta_wpa2_ent_set_password((uint8_t *)WIFI_PASSWORD, strlen(WIFI_PASSWORD));
    esp_wifi_sta_wpa2_ent_enable();
    WiFi.begin(WIFI_SSID);
  } else {
    esp_wifi_sta_wpa2_ent_disable(); 
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }
}

void powerOnModem() {
  pinMode(MODEM_PWRKEY, OUTPUT);
  digitalWrite(MODEM_PWRKEY, HIGH); delay(100);
  digitalWrite(MODEM_PWRKEY, LOW); delay(1000);
  digitalWrite(MODEM_PWRKEY, HIGH); delay(3000);
}

void initLTE() {
  serialAT.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);
}

void connectLTE() {
  if (!modem.testAT()) powerOnModem();
  if (!modem.testAT()) return;
  modem.sendAT("+CNMP=2"); modem.waitResponse();
  modem.sendAT("+CGDCONT=1,\"IP\",\"", APN, "\""); modem.waitResponse();
  if (modem.waitForNetwork(60000L)) {
    if (modem.gprsConnect(APN, APN_USER, APN_PASS)) networkModeLTE = true;
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String t = String(topic);
  StaticJsonDocument<128> doc;
  if (deserializeJson(doc, payload, length)) return;
  if (doc.containsKey("state")) {
    String state = doc["state"].as<String>();
    bool isOn = (state == "on" || state == "ON");
    if (t.indexOf("/led_1") != -1) digitalWrite(PIN_LED_1, isOn ? HIGH : LOW);
    if (t.indexOf("/led_2") != -1) digitalWrite(PIN_LED_2, isOn ? HIGH : LOW);
    Serial.printf("[ACTUATOR] LED change: %s\n", state.c_str());
  }
}

void connectMQTT() {
  if (!networkModeLTE) {
    connectWiFi();
    if (WiFi.status() != WL_CONNECTED) return;
    sslClient.setClient(&wifiClient);
  } else {
    connectLTE();
    if (!networkModeLTE) return;
    sslClient.setClient(&gsmClient);
  }
  sslClient.setInsecure();
  sslClient.setBufferSizes(2048, 1024);
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
  if (wsClient.connect(MQTT_BROKER, MQTT_PORT)) {
    if (mqttClient.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS)) {
      Serial.println("[INFO] MQTT Connecté");
      char topic[100];
      snprintf(topic, sizeof(topic), "%sactuators/+", TOPIC_PREFIX);
      mqttClient.subscribe(topic);
    }
  }
}

unsigned long getTimestamp() {
  time_t now;
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return 0;
  time(&now);
  return (unsigned long)now;
}

void readAndPublishSensors() {
  float temp = bme.readTemperature();
  float hum = bme.readHumidity();
  float pres = bme.readPressure() / 100.0F;
  float lux = lightMeter.readLightLevel();

  Serial.printf("\n--- [MESURES] ---\nTemp: %.2fC | Hum: %.2f%% | Pres: %.1fhPa | Lux: %.1flux\n", temp, hum, pres, lux);

  if (!mqttClient.connected()) return;
  
  unsigned long ts = getTimestamp();
  char buffer[256]; char topic[100]; StaticJsonDocument<256> doc;

  auto pub = [&](const char* sub, float val, const char* unit) {
    doc.clear(); doc["value"] = val; doc["unit"] = unit; doc["ts"] = ts;
    serializeJson(doc, buffer);
    snprintf(topic, sizeof(topic), "%stelemetry/%s", TOPIC_PREFIX, sub);
    mqttClient.publish(topic, buffer);
  };

  pub("temperature", temp, "C");
  pub("humidity", hum, "%");
  pub("pressure", pres, "hPa");
  pub("light", lux, "lux");
}

void setup() {
  initSerial();
  initGPIO();
  initI2C();
  initLTE();
  connectMQTT();
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
}

void loop() {
  if (!mqttClient.connected()) {
    static unsigned long lastMqttAttempt = 0;
    if (millis() - lastMqttAttempt > 10000) {
      lastMqttAttempt = millis();
      connectMQTT();
    }
  }
  mqttClient.loop();

  unsigned long now = millis();
  if (now - lastPublishTime >= PUBLISH_INTERVAL_MS) {
    lastPublishTime = now;
    readAndPublishSensors();
  }
}
