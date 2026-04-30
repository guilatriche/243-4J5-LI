/*
 * Test 04: Validation du BH1750 (Luminosité)
 * Site #7 - Hydro-Limoilou
 */

#include <Wire.h>
#include <BH1750.h>

BH1750 lightMeter;

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);

  if (lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE)) {
    Serial.println(F("BH1750 initialisé avec succès"));
  } else {
    Serial.println(F("Erreur d'initialisation du BH1750"));
  }
}

void loop() {
  float lux = lightMeter.readLightLevel();
  Serial.print("Luminosité: ");
  Serial.print(lux);
  Serial.println(" lx");
  delay(1000);
}
