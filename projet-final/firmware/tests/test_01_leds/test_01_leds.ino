/*
 * Test 01: Validation des LEDs (GPIO 14 et 13)
 * Site #7 - Hydro-Limoilou
 */

#define LED_VERTE 14
#define LED_BLEUE 13

void setup() {
  Serial.begin(115200);
  pinMode(LED_VERTE, OUTPUT);
  pinMode(LED_BLEUE, OUTPUT);
  Serial.println("Test des LEDs démarré. Alternance entre Verte et Bleue...");
}

void loop() {
  digitalWrite(LED_VERTE, HIGH);
  digitalWrite(LED_BLEUE, LOW);
  Serial.println("LED Verte: ON | LED Bleue: OFF");
  delay(1000);

  digitalWrite(LED_VERTE, LOW);
  digitalWrite(LED_BLEUE, HIGH);
  Serial.println("LED Verte: OFF | LED Bleue: ON");
  delay(1000);
}
