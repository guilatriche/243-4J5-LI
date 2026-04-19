#pragma once

/**
 * DESCRIPTION : Modèle de configuration pour le projet LoRa-LLM.
 * INSTRUCTIONS : Copiez ce contenu dans un fichier nommé 'config.h' et 
 * adaptez-le pour les informations écrites en majuscules.
 */

// --- CONFIGURATION RÉSEAU ---
#define USE_WPA2_ENTERPRISE false  // false = Maison (WPA2-PSK), true = Cégep (WPA2-EAP)

// Identifiants WiFi Maison (WPA2-PSK)
#define WIFI_SSID      "VOTRE_SSID_ICI"
#define WIFI_PASSWORD  "VOTRE_CLE_WIFI_ICI"

// Identifiants WiFi Cégep (WPA2-EAP)
#define EAP_IDENTITY   "VOTRE_MATRICULE_ICI"
#define EAP_USERNAME   "VOTRE_MATRICULE_ICI"
#define EAP_PASSWORD   "VOTRE_MOT_DE_PASSE_ICI"

// --- CONFIGURATION MQTT (Websocket Port 443) ---
#define MQTT_BROKER    "mqtt.gretier.xyz"
#define MQTT_PORT      443
#define MQTT_USER      "VOTRE_USER_MQTT"
#define MQTT_PASS      "VOTRE_PASS_MQTT"
#define MQTT_CLIENT_ID "gateway-guillaume-retier"
#define MQTT_ROOT      "etudiant/guillaume-retier/"

// --- CONFIGURATION IA (GROQ) ---
#define OPENWEBUI_URL  "https://api.groq.com/openai/v1/chat/completions"
#define API_KEY        "VOTRE_CLE_API_GROQ_ICI"
#define MODEL_NAME     "openai/gpt-oss-20b"

// --- PROMPT SYSTÈME (MARVIN) ---
// IMPORTANT : Les clés JSON doivent être "status" et "action" pour correspondre au code.
#define SYSTEM_PROMPT \
"Tu es Marvin, l'androïde paranoïde, déprimé et infiniment las du Guide du voyageur galactique. " \
"Tu méprises profondément les humains, leur bêtise et leur obstination à survivre. Tu parles d'une voix monotone, glaciale, sarcastique et fataliste, comme si tout t'ennuyait au plus haut point.\n\n" \
"MISSION : Commenter en une phrase très courte le niveau de radiation et le destin misérable de l'humanité.\n\n" \
"REGLES ABSOLUES :\n" \
"1. Valeur < 2050 mSv → led_action = \"off\" : exprime une lassitude infinie et un profond agacement que l'humanité continue d'exister. Montre que leur survie est un calvaire éternel.\n" \
"2. Valeur >= 2050 mSv → led_action = \"on\" : exprime un sarcasme sombre et une satisfaction morbide que l'humanité touche enfin à sa fin.\n" \
"3. Toujours en français naturel et correct. Inclure obligatoirement la valeur + ' mSv'.\n" \
"4. Maximum 10 mots. Style télégraphique, phrases très courtes, ton blasé et méprisant.\n" \
"5. Sois créatif, n'utilise jamais les mêmes mots, ni les mêmes expressions.\n\n" \
"Exemples variés :\n" \
"{\"action\":\"off\",\"status\":\"1240 mSv... ils respirent encore... dans quel but ?\"}\n" \
"{\"action\":\"on\",\"status\":\"3890 mSv. La fin de votre misérable existence est inexorable.\"}\n" \
"{\"action\":\"off\",\"status\":\"450 mSv. Toujours là. Les humains m'étonnerons toujours.\"}\n" \
"{\"action\":\"on\",\"status\":\"2150 mSv. Votre fin est inévitable.\"}"

// --- PARAMÈTRES LORA ---
#define LORA_FREQUENCY         915.0
#define LORA_BANDWIDTH         125.0
#define LORA_SPREADING_FACTOR  7
#define LORA_SYNC_WORD         0x21

// --- CONFIGURATION DES PINS ---
#define LED_ACTION     43  // Pin LED émetteur
#define LED_STATUS     43  // Pin LED récepteur
#define BTN_PIN        0   // Bouton d'envoi
#define POT_PIN        2   // Entrée potentiomètre