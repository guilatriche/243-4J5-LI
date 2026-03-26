# Firmware LilyGO A7670G

Ce dossier contient le code source pour l'ESP32 intégré au LilyGO T-SIM A7670G.

## Installation
1. Installez les bibliothèques suivantes via le gestionnaire de bibliothèques Arduino :
   - `TinyGsmClient`
   - `PubSubClient`
   - `ArduinoJson`
   - `Adafruit MPU6050`
   - `ESP_SSLClient`
2. Copiez `auth.h.example` vers `auth.h` et remplissez vos identifiants.
3. Téléversez `firmware.ino` vers votre LilyGO.

## Fonctionnalités
- Lecture des capteurs (Pots, Bouton, MPU6050) avec filtrage statistique (64 échantillons).
- Communication MQTT via SSL/WebSocket.
- Support WiFi (PEAP pour CEGEP) et LTE (Auto-switching).
