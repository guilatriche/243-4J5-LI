# Projet Final : Infrastructure de Télémétrie - Poste 07 (Centrale Thermique)
**Cours 243-4J5-LI | Hydro-Limoilou**

## 📖 Contexte du Projet
Ce projet simule le déploiement de l'infrastructure de télémétrie pour **Hydro-Limoilou**. L'objectif est de surveiller les conditions opérationnelles (climat, sécurité, alimentation) de 8 sites de distribution d'énergie. 

Le **Poste 07**, simulant une **Centrale Thermique**, est responsable de son propre répéteur RF, de ses capteurs, de son courtier MQTT local (Raspberry Pi 5) et de son écran de monitoring tactile. Un serveur central (VM gérée par l'enseignant) agrège les données via une convention de topics standardisée.

---

## 🛠️ Matériel & Capteurs
- **Contrôleur Principal** : LilyGO T-SIM7600G-H (ESP32 avec modem LTE)
- **Interface Tactile** : Raspberry Pi 5 + Écran officiel 7"
- **Capteurs (I2C)** :
  - **BME280** : Température, Humidité et Pression atmosphérique.
  - **BH1750** : Intensité lumineuse (Lux) pour détection d'intrusion.
- **Sécurité (GPIO)** :
  - **Capteur PIR** : Détection de mouvement infrarouge.
- **Actuateurs** :
  - 2 LEDs de statut (Contrôlables via MQTT).

---

## 📐 Conception Matérielle (PCB)
Le shield PCB pour l'ESP32 a été conçu sous KiCad pour assurer une connectivité fiable et robuste des capteurs.

### Livrables de Conception (Dossier `/kicad`)
- **Schéma Électrique** : `shield.kicad_sch` (Validé par ERC)
- **Design du PCB** : `shield.kicad_pcb` (Validé par DRC, 2 couches)
- **Fichiers de Fabrication** : Dossier incluant les **Gerbers** (RS-274X) et le **BOM** (Bill of Materials).

### Consultation Rapide (Dossier `/docs`)
- **Schéma de principe** : [schéma_électrique.pdf](./docs/schéma_électrique.pdf)
- **Vue du montage** : [Photo_breadboard.jpeg](./docs/Photo_breadboard.jpeg)
- **Vidéo du scénario 1 - Démonstration de l'installation** : [Scénario_1_Présentation.MP4](./docs/Scénario 1 - Présentation.MP4)
- **Vidéo du scénario 2 - Démonstration des alarmes** : [Scénario_2_Alarmes.MP4](./docs/Scénario 2 - Alarmes.MP4)
- **Vidéo du scénario 3 - Démonstration du lien de secours** : [Scénario_3_Lien de secours.MP4](./docs/Scénario 3 - Lien de secours.MP4)

---

## 🔌 Schéma de Câblage & Connexions
Le nœud capteur utilise un LilyGO T-SIM7600G-H. Voici l'assignation des broches (Pinout) utilisée pour le montage :

### 1. Bus I2C (Capteurs environnementaux)
*Partagé entre le BME280 et le BH1750 (Adresses 0x76 et 0x23).*
- **SDA** : GPIO 21
- **SCL** : GPIO 22
- **VCC** : 3.3V
- **GND** : Ground

### 2. Sécurité & Indicateurs
- **Capteur PIR (Mouvement)** : GPIO 32 (Entrée digitale)
- **LED 1 (Statut)** : GPIO 14 (Sortie via résistance 220Ω)
- **LED 2 (Alerte)** : GPIO 13 (Sortie via résistance 220Ω)

### 3. Gestion Énergie & Modem (Interne LilyGO)
- **Batterie ADC** : GPIO 35 (Pont diviseur interne pour mesure de tension)
- **Modem TX/RX** : GPIO 26 / 27
- **Modem Power Key** : GPIO 4
- **Board Power On** : GPIO 12 (Doit être HIGH pour alimenter les périphériques)

---

## 🚀 Fonctionnalités Clés

- **Surveillance & IA** : Publication des données environnementales et génération d'un diagnostic technique en langage naturel (IA LLM Groq) toutes les 2 minutes.
- **Sécurité Temps Réel** : Alarmes critiques (mouvement et intrusion porte) publiées en moins de 1 seconde avec QoS 1.
- **Connectivité Hybride** : Basculement automatique (Failover) entre le WiFi (Lien principal) et le réseau LTE (Lien de secours WAN privé).
- **Interface IHM** : HUD Pygame tactile sur le Pi 5 avec gestion des alarmes (verrouillage visuel et acquittement manuel).

---

## 🛠 Architecture MQTT & Contrat de Données

Le système respecte strictement la convention de topics de Hydro-Limoilou. Tous les topics sont obligatoirement préfixés par : `hydro-limoilou/poste-07/`.

| Catégorie | Topic (Suffixe) | QoS | Rétention | Description |
| :--- | :--- | :--- | :--- | :--- |
| **Télémétrie** | `telemetry/{capteur}` | 0 | Non | Mesures périodiques (10s) |
| **Statut** | `status` | 0 | Oui | État du nœud (Uptime, RSSI, Batterie) |
| **Statut IA** | `status/llm` | 0 | Oui | Diagnostic généré par l'IA (120s) |
| **Alarmes** | `alarm/{type}` | 1 | Non | Évènements : `motion`, `door` |
| **Actuateurs**| `actuators/{nom}` | 0 | Non | Commandes descendantes (VM → site) |

### 📊 Format des Payloads (JSON)
Les payloads sont minifiés et incluent obligatoirement un timestamp Unix.
- **Télémétrie** : `{"value": 22.4, "unit": "C", "ts": 1778526188}`
- **Alarmes** : `{"level": "critical", "value": 2500, "unit": "lux", "ts": 1778526188}`
- **Statut** : `{"uptime": 3600, "rssi": -67, "link": "wifi", "battery_v": 3.92, "ts": 1778526188}`

---

## 🔒 Exposition & Sécurité
Le courtier Mosquitto local du Pi 5 est accessible au serveur central via un **Tunnel Cloudflare** sécurisé.

- **URL de connexion** : `wss://mqtt.gretier.xyz:443/mqtt`
- **Username** : `central`
- **Password** : `<TRANSMIS_VIA_CANAL_SECURISE>` (Identifiants restreints par ACL)
- **Contrôle d'Accès (ACL)** : L'utilisateur central dispose d'un accès en **lecture seule** sur le site, sauf pour le dossier `/actuators/` où l'écriture est permise.

---

## 🧪 Procédure de Démonstration (Scénarios de Test)

### Scénario 1 : Fonctionnement Nominal -> **RÉSULTAT : PASSÉ ✅**
1.  Démarrer l'ESP32 et l'interface tactile.
2.  Vérifier sur l'interface que les jauges de température et humidité se mettent à jour toutes les 10 secondes.
3.  Confirmer sur le tableau de bord central (VM) la réception des données avec le bon `site-id`.

### Scénario 2 : Déclenchement d'Alarme -> **RÉSULTAT : PASSÉ ✅**
1.  Simuler une intrusion en passant la main devant le capteur PIR ou en éclairant brusquement le BH1750.
2.  **Résultat attendu** : L'interface tactile passe immédiatement en mode alerte (clignotement rouge). Un message avec `QoS 1` est publié sur `alarm/motion` ou `alarm/door`.
3.  Appuyer sur le bouton **ACQUITTER** sur l'écran tactile pour rétablir l'état normal.

### Scénario 3 : Perte et Reprise de Lien -> **RÉSULTAT : PASSÉ ✅**
1.  Brancher le câble Ethernet reliant le Pi 5 au serveur de la VM.
2.  Sur l'interface tactile (Page **LINK**), appuyer sur le bouton **PASSER AU WAN PRIVÉ**.
3.  **Résultat attendu** : L'interface se déconnecte du Cloud et se reconnecte instantanément au lien local (`ws://192.168.100.7:9001`). Le champ "Broker" et "Réseau" sur l'écran se mettent à jour.
4.  Appuyer sur **PASSER AU CLOUD** pour revenir au mode de fonctionnement nominal.

---

## 📦 Installation & Déploiement

### Configuration des Secrets
Le projet utilise un fichier de configuration unique pour le firmware et l'interface afin d'éviter la duplication des identifiants et sécuriser le dépôt.
1.  **Copiez** le fichier modèle : `cp firmware/auth.h.example firmware/auth.h`.
2.  **Éditez** `firmware/auth.h` avec vos vrais identifiants (WiFi, MQTT, API Groq).
3.  Le script Python dans `/interface` chargera automatiquement ces valeurs via le module `config_loader.py`.

### Firmware (LilyGO ESP32)
1.  Compilez et téléversez :
    ```bash
    arduino-cli compile --fqbn esp32:esp32:esp32 firmware/firmware.ino
    arduino-cli upload -p /dev/ttyUSB0 --fqbn esp32:esp32:esp32 firmware/firmware.ino
    ```

### Interface (Raspberry Pi)
1.  Installez les dépendances : `pip install pygame paho-mqtt`.
2.  Lancez l'interface :
    ```bash
    python3 interface/interface.py
    ```

## 📝 Auteurs
Projet final réalisé dans le cadre du laboratoire Hydro-Limoilou par l'étudiant responsable du Poste 07.
