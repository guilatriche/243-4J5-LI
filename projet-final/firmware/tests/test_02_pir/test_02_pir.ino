/*
 * Test 02: Validation du capteur PIR (GPIO 32)
 * Site #7 - Hydro-Limoilou
 * Capteur: EKMC4607112K
 */

#define PIN_PIR 32

void setup() {
  Serial.begin(115200);
  pinMode(PIN_PIR, INPUT); // EKMC4607112K est généralement actif haut
  Serial.println("Test du capteur PIR démarré. Surveillez les détections de mouvement...");
}

void loop() {
  int etat = digitalRead(PIN_PIR);
  if (etat == HIGH) {
    Serial.println("[ALERTE] Mouvement détecté !");
  } else {
    Serial.println("Calme...");
  }
  delay(500);
}
