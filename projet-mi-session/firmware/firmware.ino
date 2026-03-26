#include <Arduino.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>
#include <WiFi.h>
#include "esp_wpa2.h"
#include <ArduinoJson.h>

// Modem & MQTT libraries
#define TINY_GSM_MODEM_SIM7600
#include <TinyGsmClient.h>
#include <PubSubClient.h>
#include <ESP_SSLClient.h>
#include <mbedtls/base64.h>

#include "auth.h"
#include "trust_anchors.h"

// --- Constantes ---
const int PIN_LED_1 = 14; 
const int PIN_LED_2 = 13; 
const int PIN_LED_3 = 2;  
const int PIN_BOUTON = 32;
const int PIN_POT_1 = 36;
const int PIN_POT_2 = 35;
const int PIN_POT_3 = 34;

const int MODEM_TX = 26;
const int MODEM_RX = 27;
const int MODEM_PWRKEY = 4;
const int BOARD_POWERON = 12;

// Fluidité : 100ms
const unsigned long PUBLISH_INTERVAL_MS = 100;
const int POT_HYSTERESIS = 15; 
const int POT_SNAP_THRESHOLD = 30;

// --- Objets ---
Adafruit_MPU6050 mpu;
bool mpuOk = false;

HardwareSerial serialAT(1);
TinyGsm modem(serialAT);
TinyGsmClient gsmClient(modem, 0);
WiFiClient wifiClient;
ESP_SSLClient sslClient;

// Wrapper WebSocket pour MQTT sur port 443
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

// --- Variables d'état ---
unsigned long lastPublishTime = 0;
bool networkModeLTE = false;
bool lastBtnState = false;

struct {
  int pot1, pot2, pot3;
  bool btn1;
  float roll, pitch;
} sensorData;

int lastSentPot1 = 0, lastSentPot2 = 0, lastSentPot3 = 0;

// --- Prototypes ---
void initSerial();
void initGPIO();
void initI2C();
void initLTE();
void connectMQTT();
void maintainMQTT();
void readSensors();
void publishData();
void mqttCallback(char* topic, byte* payload, unsigned int length);
void connectWiFi();
void connectLTE();
void checkSerial();

// --- Main ---

void setup() {
  initSerial();
  initGPIO();
  initI2C();
  initLTE();
  connectMQTT(); 
}

void loop() {
  maintainMQTT();
  readSensors();
  checkSerial();
  
  // Heartbeat Diagnostic
  static unsigned long lastHeartbeat = 0;
  if (millis() - lastHeartbeat > 10000) {
    lastHeartbeat = millis();
    Serial.print("[STATUS] LilyGO Running - Mode: ");
    if (networkModeLTE) {
      int csq = modem.getSignalQuality();
      Serial.print("LTE - Signal (CSQ): "); Serial.println(csq);
    } else {
      Serial.print("WiFi - Status: "); 
      Serial.println(WiFi.status() == WL_CONNECTED ? "CONNECTED" : "DISCONNECTED");
    }
    Serial.print("[DEBUG] MQTT: ");
    Serial.println(mqttClient.connected() ? "CONNECTED" : "DISCONNECTED");
  }
  
  bool forcePublish = (sensorData.btn1 != lastBtnState);
  unsigned long now = millis();
  if (forcePublish || (now - lastPublishTime >= PUBLISH_INTERVAL_MS)) {
    lastPublishTime = now;
    lastBtnState = sensorData.btn1;
    publishData();
  }
}

// --- Implémentations ---

void initSerial() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n[SYSTEM] V3.2 - MODE CEGEP (PEAP) ACTIF");
  Serial.println("[INFO] Version Cegep prete.");
}

void initGPIO() {
  pinMode(BOARD_POWERON, OUTPUT);
  digitalWrite(BOARD_POWERON, HIGH);
  pinMode(PIN_BOUTON, INPUT_PULLUP);
  pinMode(PIN_LED_1, OUTPUT);
  pinMode(PIN_LED_2, OUTPUT);
  pinMode(PIN_LED_3, OUTPUT);
  digitalWrite(PIN_LED_1, LOW);
  digitalWrite(PIN_LED_2, LOW);
  digitalWrite(PIN_LED_3, LOW);
}

void initI2C() {
  Wire.begin(21, 22);
  if (mpu.begin()) {
    mpuOk = true;
    mpu.setAccelerometerRange(MPU6050_RANGE_2_G);
  }
}

void connectWiFi() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[MQTT] Tentative connexion WiFi Cegep (PEAP)...");
    WiFi.mode(WIFI_STA);
    esp_wifi_sta_wpa2_ent_set_identity((uint8_t *)WIFI_IDENTITY, strlen(WIFI_IDENTITY));
    esp_wifi_sta_wpa2_ent_set_username((uint8_t *)WIFI_IDENTITY, strlen(WIFI_IDENTITY));
    esp_wifi_sta_wpa2_ent_set_password((uint8_t *)WIFI_PASSWORD, strlen(WIFI_PASSWORD));
    esp_wifi_sta_wpa2_ent_enable();
    WiFi.begin(WIFI_SSID);
  }
}

void powerOnModem() {
  Serial.println("[MODEM] Allumage materiel du modem...");
  pinMode(MODEM_PWRKEY, OUTPUT);
  digitalWrite(MODEM_PWRKEY, HIGH); delay(100);
  digitalWrite(MODEM_PWRKEY, LOW); delay(1000); // Impulsion pour allumer
  digitalWrite(MODEM_PWRKEY, HIGH);
  delay(3000); // Attente de stabilisation
}

void initLTE() {
  // On prepare seulement les broches, on n'allume pas encore pour economiser l'energie au boot
  pinMode(BOARD_POWERON, OUTPUT);
  digitalWrite(BOARD_POWERON, HIGH);
  serialAT.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);
}

void connectLTE() {
  Serial.println("\n[MQTT] --- DIAGNOSTIC LTE PRO ---");
  
  if (!modem.testAT()) {
    powerOnModem();
  }

  if (!modem.testAT()) {
    Serial.println("[ERREUR] Le modem ne repond pas.");
    return;
  }

  // Preuves materielles (IMEI / CCID)
  String imei = modem.getIMEI();
  String ccid = modem.getSimCCID();
  Serial.print("[MODEM] IMEI : "); Serial.println(imei);
  Serial.print("[SIM]   CCID : "); Serial.println(ccid);
  
  // Optimisation Robustesse (Exigence Prof)
  Serial.println("[NETWORK] Configuration Robustesse LTE...");
  modem.sendAT("+CNMP=2"); // Mode Automatique (2G/3G/4G)
  modem.waitResponse();
  modem.sendAT("+CMNB=3"); // Prefer LTE bands
  modem.waitResponse();

  // Injection APN (Force config Keepgo)
  Serial.println("[NETWORK] Configuration APN Keepgo...");
  modem.sendAT("+CGDCONT=1,\"IP\",\"", APN, "\"");
  modem.waitResponse();

  Serial.println("[MODEM] Recherche reseau (max 60s)...");
  if (!modem.waitForNetwork(60000L)) {
    Serial.println("[ERREUR] Reseau mobile introuvable.");
    return;
  }

  // Preuves Reseau
  String oper = modem.getOperator();
  int csq = modem.getSignalQuality();
  Serial.print("[RESEAU] Connecte sur : "); Serial.println(oper);
  Serial.print("[RESEAU] Signal (CSQ) : "); Serial.print(csq); Serial.println("/31");

  Serial.println("[MODEM] Tentative de connexion Data...");
  if (modem.gprsConnect(APN, APN_USER, APN_PASS)) {
    Serial.println("[INFO] LTE Connecte (Session Data OK).");
    sslClient.setClient(&gsmClient);
    networkModeLTE = true;
  } else {
    Serial.println("[AVERTISSEMENT] LTE Enregistre, mais Data refusee (Probablement plus de credit).");
    // On bascule quand meme pour le projet (preuve radio faite)
    sslClient.setClient(&gsmClient);
    networkModeLTE = true;
  }
}

void connectMQTT() {
  if (!networkModeLTE) {
    connectWiFi();
    if (WiFi.status() != WL_CONNECTED) return; // On attend que le WiFi soit prêt
    sslClient.setClient(&wifiClient);
  } else {
    connectLTE();
    if (!networkModeLTE) return;
    sslClient.setClient(&gsmClient);
  }

  sslClient.setInsecure();
  sslClient.setBufferSizes(2048, 1024);
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setKeepAlive(15);
  mqttClient.setCallback(mqttCallback);

  if (wsClient.connect(MQTT_BROKER, MQTT_PORT)) {
    if (mqttClient.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS)) {
      Serial.println("[INFO] MQTT Connecte.");
      char topic[100];
      snprintf(topic, sizeof(topic), "%s/actuators/+", TOPIC_ROOT);
      mqttClient.subscribe(topic);
      snprintf(topic, sizeof(topic), "%s/config", TOPIC_ROOT);
      mqttClient.subscribe(topic);
    }
  }
}

void maintainMQTT() {
  if (!mqttClient.connected()) {
    static unsigned long lastReconnect = 0;
    if (millis() - lastReconnect > 2000) { // Plus rapide pour la bascule (2s au lieu de 10s)
      lastReconnect = millis();
      connectMQTT();
    }
  }
  mqttClient.loop();
}

void readSensors() {
  const int SAMPLES = 64;
  long sum1 = 0, sum2 = 0, sum3 = 0;
  int min1 = 4096, max1 = -1;
  int min2 = 4096, max2 = -1;
  int min3 = 4096, max3 = -1;

  for (int i = 0; i < SAMPLES; i++) {
    int r1 = analogRead(PIN_POT_1);
    int r2 = analogRead(PIN_POT_2);
    int r3 = analogRead(PIN_POT_3);

    sum1 += r1; if (r1 < min1) min1 = r1; if (r1 > max1) max1 = r1;
    sum2 += r2; if (r2 < min2) min2 = r2; if (r2 > max2) max2 = r2;
    sum3 += r3; if (r3 < min3) min3 = r3; if (r3 > max3) max3 = r3;
  }

  // Moyenne assainie (Rejet des 2 aberrants : Min et Max)
  int cur1 = (sum1 - min1 - max1) / (SAMPLES - 2);
  int cur2 = (sum2 - min2 - max2) / (SAMPLES - 2);
  int cur3 = (sum3 - min3 - max3) / (SAMPLES - 2);

  if (cur1 < POT_SNAP_THRESHOLD) cur1 = 0;
  else if (cur1 > 4095 - POT_SNAP_THRESHOLD) cur1 = 4095;
  if (cur2 < POT_SNAP_THRESHOLD) cur2 = 0;
  else if (cur2 > 4095 - POT_SNAP_THRESHOLD) cur2 = 4095;
  if (cur3 < POT_SNAP_THRESHOLD) cur3 = 0;
  else if (cur3 > 4095 - POT_SNAP_THRESHOLD) cur3 = 4095;

  if (abs(cur1 - lastSentPot1) >= POT_HYSTERESIS || cur1 == 0 || cur1 == 4095) { sensorData.pot1 = cur1; lastSentPot1 = cur1; }
  if (abs(cur2 - lastSentPot2) >= POT_HYSTERESIS || cur2 == 0 || cur2 == 4095) { sensorData.pot2 = cur2; lastSentPot2 = cur2; }
  if (abs(cur3 - lastSentPot3) >= POT_HYSTERESIS || cur3 == 0 || cur3 == 4095) { sensorData.pot3 = cur3; lastSentPot3 = cur3; }

  sensorData.btn1 = (digitalRead(PIN_BOUTON) == LOW);
  if (mpuOk) {
    sensors_event_t a, g, t;
    mpu.getEvent(&a, &g, &t);
    sensorData.roll = atan2(a.acceleration.y, a.acceleration.z) * 180 / PI;
    sensorData.pitch = atan2(-a.acceleration.x, sqrt(a.acceleration.y*a.acceleration.y + a.acceleration.z*a.acceleration.z))*180/PI;
  }
}

void checkSerial() {
  if (Serial.available() > 0) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    cmd.toLowerCase();
    char topic[100];
    if (cmd == "led1 on") { digitalWrite(PIN_LED_1, HIGH); snprintf(topic, sizeof(topic), "%s/actuators/led1", TOPIC_ROOT); mqttClient.publish(topic, "{\"state\":\"on\"}"); }
    else if (cmd == "led1 off") { digitalWrite(PIN_LED_1, LOW); snprintf(topic, sizeof(topic), "%s/actuators/led1", TOPIC_ROOT); mqttClient.publish(topic, "{\"state\":\"off\"}"); }
    else if (cmd == "led2 on") { digitalWrite(PIN_LED_2, HIGH); snprintf(topic, sizeof(topic), "%s/actuators/led2", TOPIC_ROOT); mqttClient.publish(topic, "{\"state\":\"on\"}"); }
    else if (cmd == "led2 off") { digitalWrite(PIN_LED_2, LOW); snprintf(topic, sizeof(topic), "%s/actuators/led2", TOPIC_ROOT); mqttClient.publish(topic, "{\"state\":\"off\"}"); }
    else if (cmd == "led3 on") { digitalWrite(PIN_LED_3, HIGH); snprintf(topic, sizeof(topic), "%s/actuators/led3", TOPIC_ROOT); mqttClient.publish(topic, "{\"state\":\"on\"}"); }
    else if (cmd == "led3 off") { digitalWrite(PIN_LED_3, LOW); snprintf(topic, sizeof(topic), "%s/actuators/led3", TOPIC_ROOT); mqttClient.publish(topic, "{\"state\":\"off\"}"); }
  }
}

void publishData() {
  if (!mqttClient.connected()) return;
  StaticJsonDocument<256> doc;
  char buffer[256];
  char topic[100];

  doc.clear(); doc["btn1"] = sensorData.btn1;
  serializeJson(doc, buffer);
  snprintf(topic, sizeof(topic), "%s/sensors/buttons", TOPIC_ROOT);
  mqttClient.publish(topic, buffer);

  static int lp1 = -1, lp2 = -1, lp3 = -1;
  if (sensorData.pot1 != lp1 || sensorData.pot2 != lp2 || sensorData.pot3 != lp3) {
    doc.clear(); doc["pot1"] = sensorData.pot1; doc["pot2"] = sensorData.pot2; doc["pot3"] = sensorData.pot3;
    serializeJson(doc, buffer);
    snprintf(topic, sizeof(topic), "%s/sensors/pots", TOPIC_ROOT);
    mqttClient.publish(topic, buffer);
    lp1 = sensorData.pot1; lp2 = sensorData.pot2; lp3 = sensorData.pot3;
  }

  if (mpuOk) {
    doc.clear(); doc["roll"] = sensorData.roll; doc["pitch"] = sensorData.pitch;
    serializeJson(doc, buffer);
    snprintf(topic, sizeof(topic), "%s/sensors/accel", TOPIC_ROOT);
    mqttClient.publish(topic, buffer);
  }

  static unsigned long lastStatus = 0;
  if (millis() - lastStatus > 5000) {
    lastStatus = millis();
    long rssi;
    if (networkModeLTE) {
      int csq = modem.getSignalQuality();
      rssi = (csq == 99) ? 0 : (2 * csq - 113); // Conversion CSQ en dBm
    } else {
      rssi = WiFi.RSSI();
    }
    doc.clear(); doc["uptime"] = millis() / 1000; doc["network"] = networkModeLTE ? "lte" : "wifi"; doc["rssi"] = rssi;
    serializeJson(doc, buffer);
    snprintf(topic, sizeof(topic), "%s/status", TOPIC_ROOT);
    mqttClient.publish(topic, buffer);
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("[MQTT] Message recu sur topic : "); Serial.println(topic);
  
  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, payload, length);
  if (error) {
    Serial.print("[MQTT] Erreur JSON : "); Serial.println(error.c_str());
    return;
  }

  String t = String(topic);

  // Gestion des Actuateurs (LEDs)
  if (t.indexOf("/actuators/led") != -1) {
    if (doc.containsKey("state")) {
      String state = String((const char*)doc["state"]);
      state.toLowerCase();
      bool isOn = (state == "on");
      if (t.endsWith("/led1")) digitalWrite(PIN_LED_1, isOn ? HIGH : LOW);
      else if (t.endsWith("/led2")) digitalWrite(PIN_LED_2, isOn ? HIGH : LOW);
      else if (t.endsWith("/led3")) digitalWrite(PIN_LED_3, isOn ? HIGH : LOW);
      Serial.print("[DEBUG] Ordre LED recu : "); Serial.println(state);
    }
  } 
  // Gestion de la Configuration (Réseau)
  else if (t.endsWith("/config")) {
    if (doc.containsKey("network")) {
      String net = String((const char*)doc["network"]);
      if (net == "lte" && !networkModeLTE) {
        Serial.println("[CONFIG] Bascule vers LTE demandee...");
        Serial.println("[WIFI] Desactivation du WiFi...");
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        networkModeLTE = true;
        mqttClient.disconnect();
        wsClient.stop();
      } else if (net == "wifi" && networkModeLTE) {
        Serial.println("[CONFIG] Bascule vers WIFI demandee...");
        networkModeLTE = false;
        mqttClient.disconnect();
        wsClient.stop();
      }
    }
  }
}
