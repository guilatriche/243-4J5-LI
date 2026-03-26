# Projet Mi-Session IoT - Équipe 1 (Kessel Run)

Ce projet est un système IoT interactif complet utilisant un LilyGO T-SIM A7670G (ESP32 + LTE) et un Raspberry Pi 5 avec écran tactile. Le concept est un simulateur de vol spatial inspiré de Star Wars : **Le Raid de Kessel**.

## 🚀 Concept du Jeu
Le joueur pilote le Faucon Millenium à travers un champ d'astéroïdes. L'objectif est de parcourir **1000 km en moins de 120 secondes**.

## 🛠️ Utilisation des Composants (Hardware)

| Composant | Fonction en Jeu | Impact |
| :--- | :--- | :--- |
| **MPU6050** | Pilotage Gyroscopique | Incliner le LilyGO pour déplacer le vaisseau. |
| **Potentiomètre 1** | Propulsion | Contrôle la vitesse (crucial pour le chrono). |
| **Potentiomètre 2** | Bouclier Vampirique | Protège des chocs mais consomme la vie (HP) du vaisseau. |
| **Potentiomètre 3** | Stabilisateur Hyperdrive | Annule les vibrations mortelles à haute vitesse. |
| **Bouton Physique** | Gâchette de Tir | Détruit les astéroïdes (Laser continu ou Bombes). |
| **LEDs (Verte/Bleue/Rouge)** | Sélecteur d'Arsenal | Indique l'arme sélectionnée sur l'écran tactile. |

## 📡 Protocole de Communication (MQTT)

Le système utilise un broker Mosquitto (port 443/WebSockets) avec la racine : `etudiant/guillaume-retier/`.

### Publications (LilyGO → Broker)
- `.../sensors/buttons` : État du bouton (`{"btn1": bool}`)
- `.../sensors/pots`    : Valeurs 0-4095 (`{"pot1": int, "pot2": int, "pot3": int}`)
- `.../sensors/accel`   : Inclinaison (`{"roll": float, "pitch": float}`)
- `.../status`          : Diagnostic (`{"uptime": int, "network": "wifi"|"lte", "rssi": int}`)

### Souscriptions (LilyGO ← Broker)
- `.../actuators/led1`  : Commande LED 1 (`{"state": "on"|"off"}`)
- `.../actuators/led2`  : Commande LED 2
- `.../actuators/led3`  : Commande LED 3
- `.../config`          : Switch réseau (`{"network": "wifi"|"lte"}`)

## 📁 Structure du Dépôt
- `/firmware` : Code ESP32 avec filtrage statistique (64 échantillons) et gestion LTE Pro.
- `/interface` : Application Python/Pygame fluide avec synchronisation passive du matériel.
- `/kicad` : Fichiers de conception du shield PCB.
- `/fabrication` : Fichiers Gerbers et Bill of Materials (BOM) originaux.
- `/docs` : Photos du prototype et captures d'écran.

## ⚙️ Installation Rapide
1. **Firmware** : Téléverser `firmware.ino` via Arduino IDE (voir README interne).
2. **Interface** : 
   ```bash
   pip install -r interface/requirements.txt
   sudo cp interface/iot-interface.service /etc/systemd/system/
   sudo systemctl daemon-reload && sudo systemctl enable iot-interface.service
   sudo systemctl start iot-interface.service
   ```

## 🔐 Sécurité
Les identifiants sensibles sont isolés dans `auth.h` et `mqtt_config.py`, lesquels sont ignorés par Git. Des modèles sont fournis (`auth.h.example` et `mqtt_config.py.example`).

---
**Auteur :** Guillaume Retier
