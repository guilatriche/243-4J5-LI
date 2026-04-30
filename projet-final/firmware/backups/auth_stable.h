#ifndef AUTH_H
#define AUTH_H

// Configuration WiFi (CEGEP - WPA2 Enterprise/PEAP)
//const char WIFI_SSID[]     = "climoilou";
//const char WIFI_IDENTITY[] = "2447103";
//const char WIFI_PASSWORD[] = "Delphine2110?";

const char WIFI_SSID[]     = "SIB-786A_2.4Ghz";
const char WIFI_IDENTITY[] = "SIB-786A_2.4Ghz";
const char WIFI_PASSWORD[] = "internet24991";


// Configuration LTE (Keepgo)
const char APN[]      = "internet.keepgo.com";
const char APN_USER[] = "";
const char APN_PASS[] = "";

// Configuration MQTT
const char MQTT_BROKER[]    = "mqtt.gretier.xyz";
const int  MQTT_PORT        = 443;
const char MQTT_USER[]      = "esp_user";
const char MQTT_PASS[]      = "Teladmin1$";
const char MQTT_CLIENT_ID[] = "lte-2447103-cegep";

// Configuration Projet Final (Hydro-Limoilou)
const char SITE_ID[]      = "poste-07";
const char TOPIC_PREFIX[] = "hydro-limoilou/poste-07/";

#endif
