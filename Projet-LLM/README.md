# ☢️ Système de Télémétrie LoRa Bidirectionnel : Édition Marvin

# Étudiant : Guillaume Retier

# 

# Cours : 243-4J5-LI - Objets connectés (Cégep Limoilou)

# 

# Matériel : 
# 2 × LilyGO T-Beam Supreme (ESP32-S3 + SX1262 + OLED SSD1306 intégré)
# 1 × Potentiomètre 10 kΩ
# 2 × DEL — action côté émetteur, status côté récepteur
# Câbles USB-C, fils de connexion, breadboard
# Accès à un broker MQTT (Mosquitto) et une clé API Groq

# 

# 1\. Description du projet

# Ce projet implémente un pipeline IoT bidirectionnel complet simulant la surveillance de radiations. Un nœud émetteur capte une valeur analogique, la transmet via LoRa à une passerelle (Récepteur) qui interroge un modèle d'intelligence artificielle (LLM Groq) pour obtenir une analyse et une décision de sécurité.

# Ci-dessous un schéma représentant l'architecture du projet

<img width="1001" height="402" alt="Schéma architecture" src="https://github.com/user-attachments/assets/7fef51ee-fce1-4fa8-8c6c-3fffcee3a324" />




# 2\. Structure des fichiers du projet

# Recepteur.ino : Logique centrale de la passerelle (Gestion du flux LoRa, WiFi et API).

# 

# config.h / config-exemple.h : Paramètres du réseau, clés API et prompt système.

# 

# trust\_anchors.h : Certificats racines SSL/TLS permettant les connexions sécurisées (HTTPS et WebSockets).

# 

# LoRaBoards.h / .cpp : Couche d'abstraction matérielle (HAL) spécifique à la carte LilyGo T-Beam S3 Supreme.

# 

# utilities.h : Fonctions utilitaires pour la gestion de l'affichage OLED et des périphériques.

# 

# 3\. Bibliothèques Nécessaires (Dépendances)

# Pour compiler ce projet, les bibliothèques suivantes doivent être installées :

# 

# RadioLib (v6.0.0+) : Gestion du module LoRa SX1262.

# 

# ArduinoJson (v7.0.0+) : Sérialisation et parsing des trames JSON.

# 

# U8g2 : Contrôle de l'écran OLED SSD1306.

# 

# PubSubClient : Communication MQTT.

# 

# ESP\_SSLClient : Support des connexions sécurisées TLS/SSL.

# 

# HTTPClient \& WiFiClientSecure : Incluses nativement dans le cœur ESP32.

# 

# 4\. Architecture et Liaison Radio

# Le système repose sur une communication LoRa point à point isolée.

# 

# Émetteur (Nœud Capteur)

# Acquisition ADC : Lecture stabilisée du potentiomètre (0-4095 mSv) avec suréchantillonnage (16 lectures), hystérésis et edge-snapping.

# 

# Liaison LoRa : Utilisation d'un Sync Word exclusif (0x21) pour isoler la communication et ignorer les interférences des autres réseaux du laboratoire.

# 

# Actionneur : Pilotage de la DEL d'Action (GPIO 43) selon la consigne envoyée par l'IA.

# 

# Récepteur (Passerelle/Gateway)

# WiFi Hybride : Basculement automatique (WPA2-PSK / WPA2-Enterprise) selon la valeur du flag USE\_WPA2\_ENTERPRISE.

# 

# Analyse IA : Appel API à Groq avec schéma JSON strict (Structured Output).

# 

# Témoin d'activité (DEL Status) : La DEL s'active spécifiquement lors de l'envoi de la requête HTTP vers le LLM et lors de la réception de la réponse sur l'écran du récepteur.

# 

# MQTT : Publication du flux décisionnel via WebSockets (Port 443).

# 

# Topic,Description,Format JSON

# .../donnees :Valeur brute provenant du potentiomètre (Émetteur LoRa). Exemple: "{""valeur"": 2500}"

# .../analyses : Commentaire généré par l'IA (Marvin) en fonction du taux. Exemple: "{""status"": ""Texte sarcastique...""}"

# .../actions : État de la DEL piloté par la décision de l'IA. Exemple: "{""action"": ""on""} ou {""action"": ""off""}"
# 

# 6\. Prérequis de Compilation (CRITIQUE)

# En raison de la taille des bibliothèques SSL et JSON, le schéma de partition par défaut est insuffisant.

# Réglages IDE Arduino :

# 

# Menu Outils > Partition Scheme > Sélectionnez "Huge APP (3MB No OTA/1MB SPIFFS)".

# 

# 7\. Spécifications du Schéma JSON

# Le système utilise la nomenclature stricte définie pour le projet :

# 

# 'status' : Analyse textuelle de l'IA (Marvin persona).

# 

# 'action' : État requis de la DEL actionneur ('on' ou 'off').

# 

# Exemple de trame :

# 

# JSON

# {

# &#x20; "status": "4095 mSv. Enfin la fin de votre absurdité biologique. Quel soulagement.",

# &#x20; "action": "on"

# }

# 8\. Fiabilité et Sécurité

# Gestion des erreurs : Algorithme de "Retry" (3 tentatives) pour l'appel LLM en cas d'erreur serveur.

# 

# Robustesse : Reconnexion automatique WiFi et MQTT toutes les 30 secondes en cas de perte de signal.

# 

# Protection des données : Secrets isolés dans config.h (exclu par .gitignore). Un modèle config-exemple.h est fourni pour le déploiement.

