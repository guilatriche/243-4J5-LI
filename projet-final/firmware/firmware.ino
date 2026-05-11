#include <Arduino.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <BH1750.h>
#include <Wire.h>
#include <WiFi.h>
#include "esp_wpa2.h"
#include <ArduinoJson.h>
#include <time.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

// Modem & MQTT libraries
#define TINY_GSM_MODEM_SIM7600
#include <TinyGsmClient.h>
#include <MQTT.h>
#include <WiFiClientSecure.h>
#include <mbedtls/base64.h>

// --- Timings (Contrat Hydro-Limoilou) ---
const unsigned long INTERVAL_TELEMETRY = 10000; // 10s
const unsigned long INTERVAL_STATUS = 30000;    // 30s
const unsigned long INTERVAL_LLM = 120000;      // 2 min

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
const int PIN_BAT_ADC = 35;

const unsigned long PUBLISH_INTERVAL_MS = 5000;

// --- Objets ---
Adafruit_BME280 bme;
BH1750 lightMeter;

HardwareSerial serialAT(1);
TinyGsm modem(serialAT);
TinyGsmClient gsmClient(modem, 0);
WiFiClient wifiClient;
WiFiClientSecure sslClient;       // Utilise mbedTLS natif de l'ESP32
WiFiClientSecure IA_Client;      // Instance dédiée pour l'IA

class WebSocketClient : public Client {
private:
  WiFiClientSecure* _sslClient;
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
  WebSocketClient(WiFiClientSecure* sslClient) : _sslClient(sslClient), _wsConnected(false), _rxBufferLen(0), _rxBufferPos(0) {}
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
    
    // Optimisation : Envoi par bloc pour stabilité SSL
    uint8_t* maskedBuf = (uint8_t*)malloc(size);
    if (maskedBuf) {
      for(size_t i = 0; i < size; i++) maskedBuf[i] = buf[i] ^ mask[i % 4];
      _sslClient->write(maskedBuf, size);
      free(maskedBuf);
    } else {
      for(size_t i = 0; i < size; i++) { uint8_t b = buf[i] ^ mask[i % 4]; _sslClient->write(&b, 1); }
    }
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
MQTTClient mqttClient(1024);

unsigned long lastPublishTime = 0;
unsigned long lastStatusTime = 0;
unsigned long lastLLMTime = 0;
unsigned long lastAlarmPublishTime = 0; // Pour limiter la répétition des alarmes
bool networkModeLTE = false;
bool lastPirState = false;
bool doorAlarmActive = false;

// --- Task LLM ---
TaskHandle_t TaskLLMHandle = NULL;
struct LLMData {
  float temp;
  float hum;
  float pres;
  float lux;
  bool pir;
  bool pending = false;
} llmParams;

void taskLLM(void * pvParameters) {
  for(;;) {
    if (llmParams.pending) {
      String summary = callLLM(llmParams.temp, llmParams.hum, llmParams.pres, llmParams.lux, llmParams.pir);
      
      if (mqttClient.connected()) {
        unsigned long ts = getTimestamp();
        StaticJsonDocument<512> doc;
        doc["summary"] = summary;
        doc["model"] = MODEL_NAME;
        doc["ts"] = ts;
        
        char buffer[512];
        serializeJson(doc, buffer);
        char topic[100];
        snprintf(topic, sizeof(topic), "%sstatus/llm", TOPIC_PREFIX);
        mqttClient.publish(topic, buffer, true, 0); // Retain = true, QoS 0
        Serial.println("[LLM] Nouveau resume publie via Task");
      }
      llmParams.pending = false;
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

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

void messageReceived(String &topic, String &payload) {
  Serial.printf("[MQTT] Message reçu sur %s : %s\n", topic.c_str(), payload.c_str());
  StaticJsonDocument<128> doc;
  if (deserializeJson(doc, payload)) return;
  if (doc.containsKey("state")) {
    String state = doc["state"].as<String>();
    bool isOn = (state == "on" || state == "ON");
    if (topic.indexOf("/led_1") != -1 || topic.indexOf("/led_01") != -1) digitalWrite(PIN_LED_1, isOn ? HIGH : LOW);
    if (topic.indexOf("/led_2") != -1 || topic.indexOf("/led_02") != -1) digitalWrite(PIN_LED_2, isOn ? HIGH : LOW);
    Serial.printf("[ACTUATOR] LED change: %s\n", state.c_str());
  }
}

void connectMQTT() {
  if (!networkModeLTE) {
    connectWiFi();
    if (WiFi.status() != WL_CONNECTED) return;
    sslClient.setInsecure();
  } else {
    connectLTE();
    if (!networkModeLTE) return;
  }
  
  mqttClient.begin(wsClient); 
  mqttClient.onMessage(messageReceived);
  
  Serial.println("[MQTT] Tentative WebSocket...");
  if (wsClient.connect(MQTT_BROKER, MQTT_PORT)) {
    Serial.println("[MQTT] WebSocket OK. Connexion Broker...");
    // Le 4ème paramètre 'true' est crucial : il skip le connect TCP/SSL interne de la lib
    if (mqttClient.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS, true)) {
      Serial.println("[INFO] MQTT Connecté (Native SSL)");
      char topic[100];
      snprintf(topic, sizeof(topic), "%sactuators/+", TOPIC_PREFIX);
      mqttClient.subscribe(topic);
    } else {
      Serial.printf("[ERROR] Echec MQTT Code: %d\n", mqttClient.returnCode());
    }
  } else {
    Serial.println("[ERROR] Echec WebSocket (Handshake)");
  }
}

unsigned long getTimestamp() {
  time_t now;
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return 0;
  time(&now);
  return (unsigned long)now;
}

String callLLM(float temp, float hum, float pres, float lux, bool pir) {
  HTTPClient http;
  IA_Client.setInsecure();

  Serial.println("[LLM] Connexion a Groq via HTTPClient...");
  
  if (!http.begin(IA_Client, OPENWEBUI_URL)) {
    return "Erreur initialisation HTTP";
  }

  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", String("Bearer ") + API_KEY);
  http.setTimeout(30000); // 30s timeout pour le LLM

  // Preparation du body
  JsonDocument req;
  req["model"] = MODEL_NAME;
  
  const char* SCHEMA = R"({"type":"json_schema","json_schema":{"name":"poste07_diag","strict":true,"schema":{"type":"object","additionalProperties":false,"required":["summary"],"properties":{"summary":{"type":"string"}}}}})";
  JsonDocument schemaDoc; 
  deserializeJson(schemaDoc, SCHEMA);
  req["response_format"] = schemaDoc;

  auto m = req["messages"].to<JsonArray>();
  JsonObject sys = m.add<JsonObject>(); 
  sys["role"] = "system"; 
  sys["content"] = SYSTEM_PROMPT;
  
  JsonObject usr = m.add<JsonObject>(); 
  usr["role"] = "user"; 
  char input[256];
  snprintf(input, sizeof(input), "Temperature=%.1fC, Humidite=%.1f%%, Pression=%.1fhPa, Luminosite=%.1f lux, Mouvement=%d", 
           temp, hum, pres, lux, pir ? 1 : 0);
  usr["content"] = input;
  
  String body;
  serializeJson(req, body);

  // Envoi
  int httpCode = http.POST(body);
  String result = "Erreur parsing";

  if (httpCode == HTTP_CODE_OK) {
    String response = http.getString();
    JsonDocument r;
    DeserializationError error = deserializeJson(r, response);
    if (error == DeserializationError::Ok) {
      String raw_content = r["choices"][0]["message"]["content"].as<String>();
      JsonDocument summaryDoc;
      if (deserializeJson(summaryDoc, raw_content) == DeserializationError::Ok) {
        result = summaryDoc["summary"].as<String>();
      } else {
        result = raw_content;
      }
    } else {
      Serial.printf("[LLM] Erreur JSON: %s\n", error.c_str());
      Serial.println("[LLM] Réponse brute pour debug:");
      Serial.println(response);
    }
  } else {
    Serial.printf("[LLM] Erreur HTTP: %d\n", httpCode);
    result = "Erreur HTTP: " + String(httpCode);
  }

  http.end();
  return result;
}

void readAndPublishTelemetry() {
  float temp = bme.readTemperature();
  float hum = bme.readHumidity();
  float pres = bme.readPressure() / 100.0F;
  float lux = lightMeter.readLightLevel();

  Serial.printf("\n--- [TELEMETRIE] ---\nT: %.1fC | H: %.1f%% | P: %.1fhPa | L: %.1flux\n", temp, hum, pres, lux);

  if (!mqttClient.connected()) return;
  
  unsigned long ts = getTimestamp();
  char buffer[256]; char topic[100]; StaticJsonDocument<256> doc;

  auto pub = [&](const char* sub, float val, const char* unit) {
    doc.clear(); doc["value"] = val; doc["unit"] = unit; doc["ts"] = ts;
    serializeJson(doc, buffer);
    snprintf(topic, sizeof(topic), "%stelemetry/%s", TOPIC_PREFIX, sub);
    mqttClient.publish(topic, buffer, false, 0);
  };

  pub("temperature", temp, "C");
  pub("humidity", hum, "%");
  pub("pressure", pres, "hPa");
  pub("light", lux, "lux");
}

float readBattery() {
  // Utilise la calibration d'usine de l'ESP32 pour lire les millivolts au pin
  float pin_mv = analogReadMilliVolts(PIN_BAT_ADC);
  // Le pont diviseur par 2 sur GPIO 35 ramène la tension batterie à la moitié.
  // On multiplie par 2 pour retrouver le voltage réel de la pile.
  return (pin_mv * 2.0) / 1000.0;
}

void publishStatus() {
  if (!mqttClient.connected()) return;
  
  unsigned long ts = getTimestamp();
  int rssi = modem.getSignalQuality();
  float bat = readBattery();
  StaticJsonDocument<256> doc;
  char buffer[256]; char topic[100];

  doc["link"] = networkModeLTE ? "wan privé" : "wifi";
  doc["uptime"] = millis() / 1000;
  doc["rssi"] = rssi;
  doc["battery_v"] = bat;
  doc["ts"] = ts;
  serializeJson(doc, buffer);
  snprintf(topic, sizeof(topic), "%sstatus", TOPIC_PREFIX);
  mqttClient.publish(topic, buffer, true, 0);
  Serial.printf("[STATUS] Maj 30s: RSSI=%d, BAT=%.2fV\n", rssi, bat);
}

void setup() {
  initSerial();
  initGPIO();
  initI2C();
  initLTE();
  connectMQTT();
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  
  xTaskCreatePinnedToCore(taskLLM, "TaskLLM", 8192, NULL, 1, &TaskLLMHandle, 0);
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

  // Détection Mouvement (PIR) - Instantané
  bool currentPirState = digitalRead(PIN_PIR);
  if (currentPirState != lastPirState) {
    if (mqttClient.connected()) {
      unsigned long ts = getTimestamp();
      StaticJsonDocument<128> doc;
      doc["level"] = (currentPirState == HIGH) ? "warning" : "ok";
      doc["value"] = (currentPirState == HIGH) ? 1 : 0;
      doc["unit"] = "detect";
      doc["ts"] = ts;
      char buffer[128];
      serializeJson(doc, buffer);
      char topic[100];
      snprintf(topic, sizeof(topic), "%salarm/motion", TOPIC_PREFIX);
      mqttClient.publish(topic, buffer, false, 1); // QoS 1
    }
    lastPirState = currentPirState;
  }

  // Surveillance Lumière Temps Réel (Alarmes immédiates et répétées)
  static unsigned long lastFastLightCheck = 0;
  if (now - lastFastLightCheck >= 500) { // Check toutes les 500ms
    lastFastLightCheck = now;
    float currentLux = lightMeter.readLightLevel();
    
    if (currentLux > 2000.0) {
      // Alarme active : Envoyer immédiatement si c'est le début ou toutes les 2s
      if (!doorAlarmActive || (now - lastAlarmPublishTime >= 2000)) {
        doorAlarmActive = true;
        lastAlarmPublishTime = now;
        if (mqttClient.connected()) {
          unsigned long ts = getTimestamp();
          StaticJsonDocument<128> doc;
          doc["level"] = "critical";
          doc["value"] = currentLux;
          doc["unit"] = "lux";
          doc["ts"] = ts;
          char buffer[128];
          serializeJson(doc, buffer);
          char topic[100];
          snprintf(topic, sizeof(topic), "%salarm/door", TOPIC_PREFIX);
          mqttClient.publish(topic, buffer, false, 1);
          Serial.printf("[ALERTE] PORTE (Lumière critique): %.1f lux\n", currentLux);
        }
      }
    } else if (doorAlarmActive) {
      // Retour à la normale
      doorAlarmActive = false;
      if (mqttClient.connected()) {
        unsigned long ts = getTimestamp();
        StaticJsonDocument<128> doc;
        doc["level"] = "ok";
        doc["value"] = currentLux;
        doc["ts"] = ts;
        char buffer[128];
        serializeJson(doc, buffer);
        char topic[100];
        snprintf(topic, sizeof(topic), "%salarm/door", TOPIC_PREFIX);
        mqttClient.publish(topic, buffer, false, 1);
        Serial.println("[ALERTE] Retour porte normale");
      }
    }
  }
  
  // Télémétrie (10s)
  if (now - lastPublishTime >= INTERVAL_TELEMETRY) {
    lastPublishTime = now;
    readAndPublishTelemetry();
  }

  // Statut (30s)
  if (now - lastStatusTime >= INTERVAL_STATUS) {
    lastStatusTime = now;
    publishStatus();
  }

  // Pipeline LLM (2 min)
  if (now - lastLLMTime >= INTERVAL_LLM) {
    lastLLMTime = now;
    if (!llmParams.pending) {
      llmParams.temp = bme.readTemperature();
      llmParams.hum = bme.readHumidity();
      llmParams.pres = bme.readPressure() / 100.0F;
      llmParams.lux = lightMeter.readLightLevel();
      llmParams.pir = digitalRead(PIN_PIR);
      llmParams.pending = true;
    }
  }
}
