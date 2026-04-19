#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <PubSubClient.h>
#include <ESP_SSLClient.h>
#include <mbedtls/base64.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <RadioLib.h>
#include <U8g2lib.h>
#include "esp_eap_client.h"
#include "utilities.h"
#include "LoRaBoards.h"
#include "config.h"

// Schéma JSON pour Marvin
const char* SCHEMA = R"({"type":"json_schema","json_schema":{"name":"marvin_strict","strict":true,"schema":{"type":"object","additionalProperties":false,"required":["status","action"],"properties":{"status":{"type":"string"},"action":{"enum":["on","off"]}}}}})";


// --- CLASSE WEBSOCKET MQTT ---
class WebSocketClient : public Client {
private:
  ESP_SSLClient* _sslClient;
  bool _wsConnected;
  uint8_t _rxBuffer[512];
  size_t _rxBufferLen;
  size_t _rxBufferPos;
  String generateWebSocketKey() {
    uint8_t key[16]; for(int i = 0; i < 16; i++) key[i] = random(0, 256);
    size_t olen; unsigned char output[64];
    mbedtls_base64_encode(output, sizeof(output), &olen, key, 16);
    return String((char*)output);
  }
  bool readWebSocketFrame() {
    if (!_sslClient->available()) return false;
    uint8_t byte1 = _sslClient->read(); uint8_t byte2 = _sslClient->read();
    uint8_t opcode = byte1 & 0x0F; bool masked = (byte2 & 0x80) != 0;
    size_t payloadLen = byte2 & 0x7F;
    if (payloadLen == 126) payloadLen = (_sslClient->read() << 8) | _sslClient->read();
    uint8_t mask[4] = {0}; if (masked) for(int i = 0; i < 4; i++) mask[i] = _sslClient->read();
    if (opcode == 0x01 || opcode == 0x02) {
      _rxBufferLen = payloadLen < sizeof(_rxBuffer) ? payloadLen : sizeof(_rxBuffer);
      for(size_t i = 0; i < _rxBufferLen; i++) {
        _rxBuffer[i] = _sslClient->read(); if (masked) _rxBuffer[i] ^= mask[i % 4];
      }
      _rxBufferPos = 0; return true;
    }
    return false;
  }
public:
  WebSocketClient(ESP_SSLClient* sslClient) : _sslClient(sslClient), _wsConnected(false), _rxBufferLen(0), _rxBufferPos(0) {}
  int peek() override { return (_rxBufferPos < _rxBufferLen) ? _rxBuffer[_rxBufferPos] : -1; }
  int connect(IPAddress ip, uint16_t port) override { return 0; }
  int connect(const char *host, uint16_t port) override {
    if (!_sslClient->connect(host, port)) return 0;
    String wsKey = generateWebSocketKey();
    _sslClient->print("GET / HTTP/1.1\r\nHost: "); _sslClient->print(host);
    _sslClient->print("\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: ");
    _sslClient->print(wsKey); _sslClient->print("\r\nSec-WebSocket-Protocol: mqtt\r\nSec-WebSocket-Version: 13\r\n\r\n");
    unsigned long timeout = millis(); while (!_sslClient->available() && millis() - timeout < 5000) delay(10);
    String response = ""; while (_sslClient->available()) { char c = _sslClient->read(); response += c; if (response.endsWith("\r\n\r\n")) break; }
    if (response.indexOf("101") > 0) { _wsConnected = true; return 1; }
    return 0;
  }
  size_t write(uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t *buf, size_t size) override {
    if (!_wsConnected) return 0;
    uint8_t header[14]; int headerLen = 2; header[0] = 0x82;
    if (size < 126) header[1] = 0x80 | size;
    else { header[1] = 0x80 | 126; header[2] = (size >> 8) & 0xFF; header[3] = size & 0xFF; headerLen = 4; }
    uint8_t mask[4]; for(int i = 0; i < 4; i++) { mask[i] = random(0, 256); header[headerLen + i] = mask[i]; }
    headerLen += 4; _sslClient->write(header, headerLen);
    for(size_t i = 0; i < size; i++) { uint8_t maskedByte = buf[i] ^ mask[i % 4]; _sslClient->write(&maskedByte, 1); }
    return size;
  }
  int available() override { return (_rxBufferPos < _rxBufferLen) ? (_rxBufferLen - _rxBufferPos) : (_sslClient->available() && readWebSocketFrame() ? _rxBufferLen - _rxBufferPos : 0); }
  int read() override { return (_rxBufferPos < _rxBufferLen) ? _rxBuffer[_rxBufferPos++] : (_sslClient->available() && readWebSocketFrame() && _rxBufferPos < _rxBufferLen ? _rxBuffer[_rxBufferPos++] : -1); }
  int read(uint8_t *buf, size_t size) override { size_t count = 0; while (count < size) { int v = read(); if (v < 0) break; buf[count++] = (uint8_t)v; } return count; }
  void flush() override { _sslClient->flush(); }
  void stop() override { _wsConnected = false; _sslClient->stop(); }
  uint8_t connected() override { return _wsConnected && _sslClient->connected(); }
  operator bool() override { return _wsConnected; }
};

// --- INSTANCES ---
WiFiClient wifiClient;
ESP_SSLClient sslClient;
WebSocketClient wsClient(&sslClient);
PubSubClient mqttClient(wsClient);
SX1262 radio = new Module(RADIO_CS_PIN, RADIO_DIO1_PIN, RADIO_RST_PIN, RADIO_BUSY_PIN);

volatile bool receivedFlag = false;
#if defined(ESP8266) || defined(ESP32)
  IRAM_ATTR
#endif
void setFlag(void) {
  receivedFlag = true;
}

void drawWrappedText(String text, int x, int y_start) {
  int line_height = 9;
  int max_chars_per_line = 25; 
  int current_y = y_start;
  while (text.length() > 0 && current_y < 64) {
    String line = text.substring(0, max_chars_per_line);
    if (text.length() > max_chars_per_line) {
      int last_space = line.lastIndexOf(' ');
      if (last_space > 0) { line = text.substring(0, last_space); text = text.substring(last_space + 1); }
      else { text = text.substring(max_chars_per_line); }
    } else { text = ""; }
    if (disp) {
      disp->setCursor(x, current_y);
      disp->print(line);
    }
    current_y += line_height;
  }
}

String callLLM(int val) {
  WiFiClientSecure httpsClient;
  httpsClient.setInsecure(); 
  HTTPClient http;
  http.begin(httpsClient, OPENWEBUI_URL);
  http.setTimeout(15000);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + String(API_KEY));
  
  JsonDocument req;
  req["model"] = MODEL_NAME;
  JsonDocument schemaDoc; deserializeJson(schemaDoc, SCHEMA);
  req["response_format"] = schemaDoc;
  auto m = req["messages"].to<JsonArray>();
  JsonObject sys = m.add<JsonObject>(); sys["role"] = "system"; sys["content"] = SYSTEM_PROMPT;
  JsonObject usr = m.add<JsonObject>(); usr["role"] = "user"; usr["content"] = "Valeur: " + String(val);
  
  String body; serializeJson(req, body);
  
  int code = 0;
  int tentatives = 0;
  while (code != 200 && tentatives < 3) {
    // 1. Rafale rapide indiquant l'envoi HTTP
    for(int i=0; i<6; i++) { 
        digitalWrite(LED_STATUS, !digitalRead(LED_STATUS)); 
        delay(50); 
    }
    
    // 2. Extinction complète : l'ESP32 attend la réponse du serveur dans le noir
    digitalWrite(LED_STATUS, LOW); 
    
    // 3. Exécution de la requête bloquante
    code = http.POST(body);
    tentatives++;
  }
  
  String response = "{\"status\":\"Erreur LLM\",\"action\":\"off\"}";
  if (code == 200) {
    JsonDocument r; deserializeJson(r, http.getString());
    response = r["choices"][0]["message"]["content"].as<String>();
  }
  http.end(); 
  return response;
}

void connectNetwork() {
  if (WiFi.status() != WL_CONNECTED) {
    if (disp) {
      disp->clearBuffer();
      disp->setCursor(0, 15); disp->print("WiFi: Connexion...");
      disp->sendBuffer();
    }
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
    if (disp) {
      disp->clearBuffer();
      disp->setCursor(0, 15); disp->print("WiFi OK"); 
      disp->setCursor(0, 30); disp->print("IP: "); 
      disp->print(WiFi.localIP().toString().c_str());
      disp->sendBuffer();
      delay(2000);
    }
  }
  if (!mqttClient.connected()) {
    sslClient.setClient(&wifiClient);
    sslClient.setInsecure();
    if (wsClient.connect(MQTT_BROKER, MQTT_PORT)) {
      mqttClient.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS);
    }
  }
}

void setup() {
  Serial.begin(115200);
  setupBoards(); 
  if (disp) {
    disp->begin();
    disp->enableUTF8Print();
    disp->setFont(u8g2_font_5x8_tf);
  }
  pinMode(LED_STATUS, OUTPUT);
  connectNetwork();
  
  int state = radio.begin(LORA_FREQUENCY, LORA_BANDWIDTH, LORA_SPREADING_FACTOR, 7, LORA_SYNC_WORD, 22, 8);
  if (state == RADIOLIB_ERR_NONE) {
    radio.setPacketReceivedAction(setFlag);
    radio.startReceive();
  }
}

void loop() {
  mqttClient.loop();
  
  static unsigned long lastNet = 0;
  if (millis() - lastNet > 30000) {
    lastNet = millis();
    if (WiFi.status() != WL_CONNECTED || !mqttClient.connected()) connectNetwork();
  }

  if (receivedFlag) {
    receivedFlag = false;
    String received;
    
    if (radio.readData(received) == RADIOLIB_ERR_NONE) {
      float currentRSSI = radio.getRSSI();
      float currentSNR = radio.getSNR();
      
      // Allume la DEL brièvement à la réception LoRa
      digitalWrite(LED_STATUS, HIGH);

      JsonDocument rxDoc;
      deserializeJson(rxDoc, received);
      int potVal = rxDoc["pot"] | received.toInt();

      if (disp) {
        disp->clearBuffer();
        disp->setCursor(0, 10); disp->print("Recu: "); disp->print(potVal);
        disp->setCursor(0, 20); 
        disp->print("RSSI:"); disp->print(currentRSSI, 0); disp->print("dBm ");
        disp->print("SNR:"); disp->print(currentSNR, 1); disp->print("dB");
        disp->setCursor(0, 32); disp->print("Analyse LLM...");
        disp->sendBuffer();
      }

      // Appel bloquant à l'IA (gère son propre clignotement + attente)
      String respJSON = callLLM(potVal);

      JsonDocument r; deserializeJson(r, respJSON);
      String msg = r["status"] | "...";
      String led = r["action"] | "off";

      // Clignotement de transition indiquant la réception de la réponse IA
      for(int i=0; i<4; i++) { 
          digitalWrite(LED_STATUS, !digitalRead(LED_STATUS)); 
          delay(100); 
      }

      if (disp) {
        disp->clearBuffer();
        disp->setCursor(0, 10); disp->print("RAD:"); disp->print(potVal); disp->print(" | LED:"); disp->print(led);
        disp->setCursor(0, 20);
        disp->print("RSSI:"); disp->print(currentRSSI, 0); disp->print("dBm ");
        disp->print("SNR:"); disp->print(currentSNR, 1); disp->print("dB");
        disp->drawStr(0, 26, "-------------------------");
        drawWrappedText(msg, 0, 36);
        
        // --- ACCUSÉ DE RÉCEPTION MATÉRIEL ---
        disp->sendBuffer(); 
        digitalWrite(LED_STATUS, LOW); 
      }

      // --- RETOUR LORA PRIORITAIRE ---
      radio.transmit(respJSON);

      // --- PUBLICATION MQTT EN TÂCHE DE FOND ---
      if (mqttClient.connected()) {
        mqttClient.publish((String(MQTT_ROOT) + "decision").c_str(), respJSON.c_str());
      }
    }
    radio.startReceive();
    receivedFlag = false; 
  }
}