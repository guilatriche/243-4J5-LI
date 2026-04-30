# Tests Unitaires - Site #7 (Centrale thermique de secours)

Cette section contient les scripts de validation pour chaque composant du projet Hydro-Limoilou.

## Séquence de test recommandée

1.  **test_01_leds.ino** : Vérifie la connexion des LEDs sur les GPIO 14 (Verte) et 13 (Bleue).
2.  **test_02_pir.ino** : Vérifie que le détecteur de mouvement EKMC sur le GPIO 32 fonctionne.
3.  **test_03_i2c_scan.ino** : Scanne le bus I2C (21, 22). Vous devriez voir deux adresses (typiquement `0x76` pour le BME280 et `0x23` pour le BH1750).
4.  **test_04_bh1750.ino** : Valide la lecture de la luminosité (Lux).
5.  **test_05_bme280.ino** : Valide les mesures climatiques (T, H, P).

## Commandes utiles

Pour compiler et téléverser (exemple pour les LEDs) :
```bash
arduino-cli compile --fqbn esp32:esp32:esp32 projet-final/firmware/tests/test_01_leds.ino
arduino-cli upload -p /dev/ttyUSB0 --fqbn esp32:esp32:esp32 projet-final/firmware/tests/test_01_leds.ino
arduino-cli monitor -p /dev/ttyUSB0 -c baudrate=115200
```
