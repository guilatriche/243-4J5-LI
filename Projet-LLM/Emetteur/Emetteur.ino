#include <Arduino.h>
#include <U8g2lib.h>
#include <RadioLib.h>
#include <ArduinoJson.h>
#include "utilities.h" 
#include "LoRaBoards.h"
#include "config.h"

// --- INSTANCES ---
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
SX1262 radio = new Module(RADIO_CS_PIN, RADIO_DIO1_PIN, RADIO_RST_PIN, RADIO_BUSY_PIN);

// --- ÉTATS DU SYSTÈME ---
enum State { SCANNING, SENDING, WAITING, SHOW_RESULT };
State currentState = SCANNING;

// --- VARIABLES ---
int potValue = 0;
String marvinMessage = "";
String ledAction = "off";

// --- FONCTION DE LECTURE STABILISÉE DU POTENTIOMÈTRE ---
int readStablePot(int currentStableValue) {
    long sum = 0;
    // 1. Moyenne sur 16 lectures (Oversampling)
    for (int i = 0; i < 16; i++) {
        sum += analogRead(POT_PIN);
    }
    int average = sum / 16;

    // 2. Forçage des butées extrêmes (Edge Snapping)
    if (average < 15) {
        average = 0;
    } else if (average > 4080) {
        average = 4095;
    }

    // 3. Exception pour les butées immédiates
    if (average == 0 || average == 4095) {
        return average;
    }

    // 4. Blocage (Hystérésis) à 15
    if (abs(average - currentStableValue) > 15) {
        return average;
    }
    
    // Sinon, on garde l'ancienne valeur
    return currentStableValue; 
}

// --- FONCTION DE CENTRAGE AVEC SÉCURITÉ MARGE ---
void drawCenteredStr(int y, const char *s) {
    int strWidth = u8g2.getStrWidth(s);
    int x = (128 - strWidth) / 2;
    if (x < 2) x = 2; // Évite de coller au bord gauche
    u8g2.setCursor(x, y);
    u8g2.print(s);
}

// --- FONCTION DE WRAPPING ---
void drawWrappedText(String text, int x, int y_start) {
    int line_height = 9;
    int max_chars_per_line = 25; 
    int current_y = y_start;
    
    while (text.length() > 0 && current_y < 64) {
        String line = text.substring(0, max_chars_per_line);
        if (text.length() > max_chars_per_line) {
            int last_space = line.lastIndexOf(' ');
            if (last_space > 0) { 
                line = text.substring(0, last_space); 
                text = text.substring(last_space + 1); 
            } else { 
                text = text.substring(max_chars_per_line); 
            }
        } else { 
            text = ""; 
        }
        u8g2.setCursor(x, current_y);
        u8g2.print(line);
        current_y += line_height;
    }
}

void setup() {
    Serial.begin(115200);
    setupBoards();
    u8g2.begin();
    u8g2.enableUTF8Print(); 
    u8g2.setFont(u8g2_font_5x8_tf);

    // Configuration de l'ADC pour utiliser toute la plage de tension (0 - 3.3V)
    analogSetAttenuation(ADC_11db);

    pinMode(LED_ACTION, OUTPUT);
    digitalWrite(LED_ACTION, LOW);
    pinMode(BTN_PIN, INPUT_PULLUP);

    radio.begin(LORA_FREQUENCY, LORA_BANDWIDTH, LORA_SPREADING_FACTOR, 7, LORA_SYNC_WORD, 22, 8);
    
    // Initialisation de la première valeur du potentiomètre
    potValue = analogRead(POT_PIN);
}

void loop() {
    u8g2.clearBuffer();
    bool btnPressed = (digitalRead(BTN_PIN) == LOW);

    switch (currentState) {
        
        case SCANNING:
            // Appel de la fonction de stabilisation
            potValue = readStablePot(potValue);
            
            u8g2.setFont(u8g2_font_5x8_tf);
            drawCenteredStr(10, "--- VALEUR POT ---");
            
            // Gros chiffres au centre
            u8g2.setFont(u8g2_font_logisoso32_tn); 
            {
                char buf[10]; sprintf(buf, "%04d", potValue);
                drawCenteredStr(50, buf);
            }
            
            u8g2.setFont(u8g2_font_5x8_tf);
            drawCenteredStr(62, "PRESSER pour envoyer");

            if (btnPressed) {
                delay(200); 
                currentState = SENDING;
            }
            break;

        case SENDING:
            u8g2.setFont(u8g2_font_5x8_tf);
            drawCenteredStr(32, "TRANSMISSION LORA...");
            u8g2.sendBuffer();
            
            {
                JsonDocument doc;
                doc["pot"] = potValue;
                String msg; serializeJson(doc, msg);
                radio.transmit(msg);
            }
            currentState = WAITING;
            break;

        case WAITING:
            u8g2.setFont(u8g2_font_5x8_tf);
            drawCenteredStr(32, "ATTENTE DE MARVIN...");
            u8g2.sendBuffer();
            
            {
                String reply;
                if (radio.receive(reply, 10000) == RADIOLIB_ERR_NONE) {
                    JsonDocument r;
                    deserializeJson(r, reply);
                    marvinMessage = r["status"] | "Pas de reponse.";
                    ledAction = r["action"] | "off";
                    digitalWrite(LED_ACTION, (ledAction == "on") ? HIGH : LOW);
                    currentState = SHOW_RESULT;
                } else {
                    marvinMessage = "Marvin vous ignore. Timeout.";
                    ledAction = "off";
                    digitalWrite(LED_ACTION, LOW);
                    currentState = SHOW_RESULT;
                }
            }
            break;

        case SHOW_RESULT:
            u8g2.setFont(u8g2_font_5x8_tf);
            u8g2.setCursor(2, 12); 
            u8g2.print("MARVIN DIT :");
            u8g2.drawHLine(0, 15, 128);
            
            drawWrappedText(marvinMessage, 2, 28);
            
            u8g2.drawHLine(0, 54, 128);
            u8g2.setCursor(2, 62);
            u8g2.print("Led: "); u8g2.print(ledAction);
            
            if (btnPressed) {
                delay(200);
                digitalWrite(LED_ACTION, LOW); 
                radio.startReceive();
                currentState = SCANNING;
            }
            break;
    }

    u8g2.sendBuffer();
}