/*
 * Test 05: Validation du BME280 (Climat)
 * Site #7 - Hydro-Limoilou
 */

#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

Adafruit_BME280 bme; 

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);

  // Note: L'adresse par défaut est 0x77, mais SDO au GND donne 0x76
  unsigned status = bme.begin(0x76, &Wire);  
  if (!status) {
    Serial.println("Impossible de trouver un BME280 valide, vérifiez le câblage !");
    while (1);
  }
  Serial.println("BME280 initialisé avec succès");
}

void loop() {
  Serial.print("Température = ");
  Serial.print(bme.readTemperature());
  Serial.println(" *C");

  Serial.print("Pression = ");
  Serial.print(bme.readPressure() / 100.0F);
  Serial.println(" hPa");

  Serial.print("Humidité = ");
  Serial.print(bme.readHumidity());
  Serial.println(" %");

  Serial.println();
  delay(2000);
}
