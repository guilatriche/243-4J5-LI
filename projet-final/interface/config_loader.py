import re
import os

def load_mqtt_config(auth_h_path):
    """
    Analyse le fichier auth.h pour extraire la configuration MQTT.
    """
    config = {
        "broker_primary": "",
        "port_primary": 443,
        "broker_secondary": "192.168.100.7",
        "port_secondary": 9001,
        "username": "",
        "password": "",
        "client_id": "rpi-interface-touch",
        "topic_root": "hydro-limoilou/poste-07",
        "history_file": "alarm_history.json"
    }
    
    if not os.path.exists(auth_h_path):
        print(f"Erreur: {auth_h_path} introuvable.")
        return config

    with open(auth_h_path, 'r', encoding='utf-8') as f:
        content = f.read()
        
        # Extraction via Regex
        broker = re.search(r'const char MQTT_BROKER\[\]\s*=\s*"(.*?)";', content)
        user = re.search(r'const char MQTT_USER\[\]\s*=\s*"(.*?)";', content)
        pw = re.search(r'const char MQTT_PASS\[\]\s*=\s*"(.*?)";', content)
        site_id = re.search(r'const char SITE_ID\[\]\s*=\s*"(.*?)";', content)
        
        if broker: config["broker_primary"] = broker.group(1)
        if user: config["username"] = user.group(1)
        if pw: config["password"] = pw.group(1)
        if site_id: config["topic_root"] = f"hydro-limoilou/{site_id.group(1)}"
            
    return config
