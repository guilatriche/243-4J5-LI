# Interface Raspberry Pi 5

Interface de jeu "Kessel Run" développée en Python avec Pygame.

## Installation
```bash
pip install -r requirements.txt
```

## Configuration
1. Copiez `mqtt_config.py.example` vers `mqtt_config.py`.
2. Modifiez `mqtt_config.py` avec vos identifiants MQTT.

## Configuration du service
Pour lancer l'interface au boot :
```bash
sudo cp iot-interface.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable iot-interface.service
sudo systemctl start iot-interface.service
```
