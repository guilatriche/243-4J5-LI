#define LED_ROUGE 13
#define LED_VERTE 15

void setup() {
  Serial.begin(115200);
  pinMode(LED_ROUGE, OUTPUT);
  pinMode(LED_VERTE, OUTPUT);
  // Éteindre tout au démarrage
  digitalWrite(LED_ROUGE, LOW);
  digitalWrite(LED_VERTE, LOW);
}

void loop() {
  if (Serial.available() > 0) {
    String commande = Serial.readStringUntil('\n');
    commande.trim(); // Enlever les espaces ou \r cachés

    if (commande == "rouge") {
      digitalWrite(LED_ROUGE, HIGH);
      digitalWrite(LED_VERTE, LOW);
    } 
    else if (commande == "verte") {
      digitalWrite(LED_ROUGE, LOW);
      digitalWrite(LED_VERTE, HIGH);
    }
  }
}
