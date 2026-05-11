import os
import pygame
import paho.mqtt.client as mqtt
import json
import ssl
import time
from datetime import datetime
from threading import Thread
from config_loader import load_mqtt_config

# Configuration du driver vidéo pour Raspberry Pi (KMS/DRM)
if not os.environ.get('DISPLAY'):
    os.environ['SDL_VIDEODRIVER'] = 'kmsdrm'
    os.environ['SDL_MOUSE_TOUCH_EVENTS'] = '1'
    os.environ['SDL_INPUT_LINUX_EVDEV'] = '1'

# Chargement de la configuration centralisée depuis auth.h
auth_path = os.path.join(os.path.dirname(__file__), "../firmware/auth.h")
MQTT_CONFIG = load_mqtt_config(auth_path)

# Couleurs (Hydro-Limoilou Style)
COLOR_BG = (10, 15, 25)
COLOR_HUD_BG = (20, 30, 45)
COLOR_TEXT = (236, 240, 241)
COLOR_ACCENT = (52, 152, 219)
COLOR_SUCCESS = (46, 204, 113)
COLOR_DANGER = (231, 76, 60)
COLOR_WARN = (241, 196, 15)

# Paramètres écran
SCREEN_W, SCREEN_H = 720, 1280

class App:
    def __init__(self):
        pygame.init()
        # On tente le plein écran si sur Pi, sinon fenêtre
        if os.environ.get('SDL_VIDEODRIVER') == 'kmsdrm':
            self.screen = pygame.display.set_mode((SCREEN_W, SCREEN_H), pygame.FULLSCREEN)
        else:
            self.screen = pygame.display.set_mode((SCREEN_W, SCREEN_H))
            
        self.clock = pygame.time.Clock()
        self.running = True
        self.current_page = "TELEMETRY" # TELEMETRY, ALARMS, LINK
        
        # Polices
        self.font_title = pygame.font.SysFont("Arial", 48, bold=True)
        self.font_mid = pygame.font.SysFont("Arial", 32, bold=True)
        self.font_small = pygame.font.SysFont("Arial", 20)
        self.font_sensor = pygame.font.SysFont("Arial", 40, bold=True)
        
        # Données
        self.telemetry = {
            "temperature": {"value": 0, "unit": "C"},
            "humidity": {"value": 0, "unit": "%"},
            "pressure": {"value": 0, "unit": "hPa"},
            "light": {"value": 0, "unit": "lux"}
        }
        self.alarms = []
        self.motion_active = False
        self.door_alarm_active = False
        self.alarm_latched = False  # Verrouillage pour clignotement persistant
        self.link_status = {
            "rssi": -100,
            "uptime": 0,
            "network": "unknown",
            "battery_v": 0.0,
            "connected": False
        }
        self.led_states = {"led_1": False, "led_2": False}
        self.llm_summary = "En attente du premier diagnostic..."
        self.current_broker = "primary"
        
        # Charger l'historique local
        self.load_history()
        
        # Le client sera initialisé dans le thread MQTT
        self.client = None
        
        # Navigation Buttons
        self.nav_rects = {
            "TELEMETRY": pygame.Rect(0, 1180, 240, 100),
            "ALARMS": pygame.Rect(240, 1180, 240, 100),
            "LINK": pygame.Rect(480, 1180, 240, 100)
        }
        
        # Action Buttons
        self.btn_led1 = pygame.Rect(100, 850, 240, 80)
        self.btn_led2 = pygame.Rect(380, 850, 240, 80)
        self.btn_ack = pygame.Rect(210, 1000, 300, 100)
        self.btn_switch = pygame.Rect(50, 1000, 620, 100) # Bouton switch broker sur page LINK

    def load_history(self):
        try:
            full_path = os.path.join(os.path.dirname(__file__), MQTT_CONFIG["history_file"])
            if os.path.exists(full_path):
                with open(full_path, 'r') as f:
                    self.alarms = json.load(f)
            else:
                self.alarms = []
        except Exception as e:
            print(f"Error loading history: {e}")
            self.alarms = []

    def save_history(self):
        try:
            full_path = os.path.join(os.path.dirname(__file__), MQTT_CONFIG["history_file"])
            with open(full_path, 'w') as f:
                json.dump(self.alarms[:50], f)
        except Exception as e:
            print(f"Error saving history: {e}")

    def on_connect(self, client, userdata, flags, rc):
        if rc == 0:
            self.link_status["connected"] = True
            root = MQTT_CONFIG["topic_root"]
            client.subscribe(f"{root}/#")
            print(f"Connected to {root}")

    def on_message(self, client, userdata, msg):
        try:
            payload = json.loads(msg.payload.decode())
            topic = msg.topic
            
            # Télémétrie
            if "telemetry/" in topic:
                sensor = topic.split("/")[-1]
                if sensor in self.telemetry:
                    self.telemetry[sensor]["value"] = payload.get("value", 0)
                    self.telemetry[sensor]["unit"] = payload.get("unit", "")
            
            # Alarmes
            elif "alarm/motion" in topic:
                level = payload.get("level", "warning")
                if level == "ok":
                    self.motion_active = False
                else:
                    self.motion_active = True
                    self.alarm_latched = True # Verrouille l'alerte visuelle
                    ts = datetime.fromtimestamp(payload.get("ts", time.time())).strftime('%H:%M:%S')
                    alarm_entry = f"{ts} - MOUVEMENT DETECTE"
                    self.alarms.insert(0, alarm_entry)
                    if len(self.alarms) > 50: self.alarms.pop()
                    self.save_history()
            
            elif "alarm/door" in topic:
                level = payload.get("level", "critical")
                if level == "ok":
                    self.door_alarm_active = False
                    val = payload.get("value", 0)
                    self.telemetry["light"]["value"] = val
                else:
                    self.door_alarm_active = True
                    self.alarm_latched = True # Verrouille l'alerte visuelle
                    val = payload.get("value", 0)
                    self.telemetry["light"]["value"] = val # Mise à jour temps réel HUD
                    ts = datetime.fromtimestamp(payload.get("ts", time.time())).strftime('%H:%M:%S')
                    alarm_entry = f"{ts} - ALERTE PORTE: {val} LUX"
                    self.alarms.insert(0, alarm_entry)
                    if len(self.alarms) > 50: self.alarms.pop()
                    self.save_history()
            
            # Actuators feedback (Strict check to ignore legacy/status messages)
            elif topic == f"{MQTT_CONFIG['topic_root']}/actuators/led_1" or topic == f"{MQTT_CONFIG['topic_root']}/actuators/led_2":
                led_num = topic.split("/")[-1]
                self.led_states[led_num] = payload.get("state", "off").lower() == "on"

            elif "status/llm" in topic:
                self.llm_summary = payload.get("summary", "Format de resume invalide")

            # Status
            elif topic.endswith("/status"):
                self.link_status["rssi"] = payload.get("rssi", -100)
                self.link_status["uptime"] = payload.get("uptime", 0)
                self.link_status["network"] = payload.get("link", "unknown")
                self.link_status["battery_v"] = payload.get("battery_v", 0.0)

        except Exception as e:
            print(f"Error parsing message: {e}")

    def send_led_command(self, led_id, state):
        if self.client and self.link_status["connected"]:
            topic = f"{MQTT_CONFIG['topic_root']}/actuators/{led_id}"
            payload = json.dumps({"state": "on" if state else "off"})
            self.client.publish(topic, payload)

    def handle_press(self, pos):
        # Navigation
        for page, rect in self.nav_rects.items():
            if rect.collidepoint(pos):
                self.current_page = page
        
        # LEDs control (sur page Telemetry)
        if self.current_page == "TELEMETRY":
            if self.btn_led1.collidepoint(pos):
                self.led_states["led_1"] = not self.led_states["led_1"]
                self.send_led_command("led_1", self.led_states["led_1"])
            elif self.btn_led2.collidepoint(pos):
                self.led_states["led_2"] = not self.led_states["led_2"]
                self.send_led_command("led_2", self.led_states["led_2"])
        
        # Ack Alarms
        if self.current_page == "ALARMS":
            if self.btn_ack.collidepoint(pos):
                self.alarm_latched = False # Libère le clignotement
                self.motion_active = False
                self.door_alarm_active = False
        
        # Switch Broker
        if self.current_page == "LINK":
            if self.btn_switch.collidepoint(pos):
                self.current_broker = "secondary" if self.current_broker == "primary" else "primary"
                if self.client:
                    self.client.disconnect() # Force la reconnexion


    def handle_events(self):
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                self.running = False
            elif event.type == pygame.MOUSEBUTTONDOWN:
                self.handle_press(event.pos)
            elif event.type == pygame.FINGERDOWN:
                # Coordonnées normalisées (0.0 à 1.0) -> Pixels
                self.handle_press((event.x * SCREEN_W, event.y * SCREEN_H))

    def draw_navbar(self):
        for page, rect in self.nav_rects.items():
            color = COLOR_ACCENT if self.current_page == page else COLOR_HUD_BG
            pygame.draw.rect(self.screen, color, rect)
            pygame.draw.rect(self.screen, COLOR_TEXT, rect, 1)
            text = self.font_small.render(page, True, COLOR_TEXT)
            self.screen.blit(text, (rect.centerx - text.get_width()//2, rect.centery - text.get_height()//2))

    def draw_wrapped_text(self, text, font, color, rect):
        words = text.split(' ')
        lines = []
        current_line = ""
        for word in words:
            test_line = current_line + word + " "
            if font.size(test_line)[0] < rect.width - 20:
                current_line = test_line
            else:
                lines.append(current_line)
                current_line = word + " "
        lines.append(current_line)
        
        for i, line in enumerate(lines):
            if i > 4: break # Max lines
            img = font.render(line.strip(), True, color)
            self.screen.blit(img, (rect.x + 10, rect.y + 10 + i * 25))

    def draw_telemetry(self):
        # Header
        title = self.font_title.render("TELEMETRIE", True, COLOR_ACCENT)
        self.screen.blit(title, (SCREEN_W//2 - title.get_width()//2, 50))
        
        # Capteurs
        sensors = [
            ("TEMP", "temperature", COLOR_WARN),
            ("HUM", "humidity", COLOR_ACCENT),
            ("PRES", "pressure", COLOR_SUCCESS),
            ("LUM", "light", COLOR_WARN)
        ]
        
        for i, (label, key, col) in enumerate(sensors):
            y_off = 150 + i * 180
            pygame.draw.rect(self.screen, COLOR_HUD_BG, (50, y_off, 620, 150), border_radius=15)
            
            lbl = self.font_mid.render(label, True, COLOR_TEXT)
            self.screen.blit(lbl, (80, y_off + 20))
            
            val_str = f"{self.telemetry[key]['value']:.1f} {self.telemetry[key]['unit']}"
            val = self.font_sensor.render(val_str, True, col)
            self.screen.blit(val, (620 - val.get_width(), y_off + 60))

        # LED Controls
        l1_col = COLOR_SUCCESS if self.led_states["led_1"] else COLOR_HUD_BG
        pygame.draw.rect(self.screen, l1_col, self.btn_led1, border_radius=10)
        pygame.draw.rect(self.screen, COLOR_TEXT, self.btn_led1, 2, border_radius=10)
        t1 = self.font_small.render("LED 1", True, COLOR_TEXT)
        self.screen.blit(t1, (self.btn_led1.centerx - t1.get_width()//2, self.btn_led1.centery - t1.get_height()//2))

        l2_col = COLOR_SUCCESS if self.led_states["led_2"] else COLOR_HUD_BG
        pygame.draw.rect(self.screen, l2_col, self.btn_led2, border_radius=10)
        pygame.draw.rect(self.screen, COLOR_TEXT, self.btn_led2, 2, border_radius=10)
        t2 = self.font_small.render("LED 2", True, COLOR_TEXT)
        self.screen.blit(t2, (self.btn_led2.centerx - t2.get_width()//2, self.btn_led2.centery - t2.get_height()//2))

        # Panneau Analyse IA (§3.6)
        llm_rect = pygame.Rect(50, 1000, 620, 150)
        pygame.draw.rect(self.screen, (30, 40, 60), llm_rect, border_radius=15)
        pygame.draw.rect(self.screen, COLOR_ACCENT, llm_rect, 2, border_radius=15)
        
        lbl_ia = self.font_small.render("ANALYSE IA EN TEMPS RÉEL", True, COLOR_ACCENT)
        self.screen.blit(lbl_ia, (llm_rect.x + 10, llm_rect.y - 25))
        
        self.draw_wrapped_text(self.llm_summary, self.font_small, COLOR_TEXT, llm_rect)

    def draw_alarms(self):
        title = self.font_title.render("ALARMES", True, COLOR_DANGER)
        self.screen.blit(title, (SCREEN_W//2 - title.get_width()//2, 50))
        
        # Etat actuel
        if self.door_alarm_active:
            status_text = "CRITICAL : INTRUSION PORTE"
            status_col = COLOR_DANGER
        elif self.motion_active:
            status_text = "ALERTE : MOUVEMENT DETECTE"
            status_col = COLOR_WARN
        else:
            status_text = "SYSTEME OK"
            status_col = COLOR_SUCCESS
            
        st = self.font_mid.render(status_text, True, status_col)
        self.screen.blit(st, (SCREEN_W//2 - st.get_width()//2, 120))
        
        # Liste
        pygame.draw.rect(self.screen, COLOR_HUD_BG, (50, 200, 620, 750), border_radius=15)
        for i, alarm in enumerate(self.alarms):
            if i > 12: break
            txt = self.font_small.render(alarm, True, COLOR_TEXT)
            self.screen.blit(txt, (80, 230 + i * 50))
            
        # Bouton ACK
        pygame.draw.rect(self.screen, COLOR_HUD_BG, self.btn_ack, border_radius=15)
        pygame.draw.rect(self.screen, COLOR_TEXT, self.btn_ack, 2, border_radius=15)
        ack_t = self.font_mid.render("ACQUITTER", True, COLOR_TEXT)
        self.screen.blit(ack_t, (self.btn_ack.centerx - ack_t.get_width()//2, self.btn_ack.centery - ack_t.get_height()//2))

    def draw_link(self):
        title = self.font_title.render("ETAT DU LIEN", True, COLOR_SUCCESS)
        self.screen.blit(title, (SCREEN_W//2 - title.get_width()//2, 50))
        
        current_br = MQTT_CONFIG["broker_primary"] if self.current_broker == "primary" else MQTT_CONFIG["broker_secondary"]
        
        # Logique Batterie Simplifiée
        bat_v = self.link_status["battery_v"]
        if bat_v > 0.1:
            bat_str = f"{bat_v:.2f} V"
            bat_col = COLOR_DANGER if bat_v < 3.4 else COLOR_TEXT
        else:
            bat_str = "N/A"
            bat_col = COLOR_TEXT

        info = [
            ("Broker", current_br),
            ("Topic Root", MQTT_CONFIG["topic_root"]),
            ("Réseau", self.link_status["network"].upper()),
            ("RSSI", f"{self.link_status['rssi']} dBm"),
            ("Tension Batterie", bat_str),
            ("Uptime", f"{self.link_status['uptime']}s"),
            ("MQTT Status", "CONNECTE" if self.link_status["connected"] else "DECONNECTE")
        ]
        
        for i, (label, val) in enumerate(info):
            y = 150 + i * 95
            pygame.draw.rect(self.screen, COLOR_HUD_BG, (50, y, 620, 85), border_radius=10)
            l_t = self.font_small.render(label, True, COLOR_ACCENT)
            
            # Utiliser la couleur spécifique pour la batterie
            col = bat_col if label == "Tension Batterie" else COLOR_TEXT
            v_t = self.font_mid.render(str(val), True, col)
            
            self.screen.blit(l_t, (70, y + 10))
            self.screen.blit(v_t, (70, y + 35))

        # Bouton Switch Broker
        btn_col = COLOR_WARN if self.current_broker == "primary" else COLOR_SUCCESS
        pygame.draw.rect(self.screen, COLOR_HUD_BG, self.btn_switch, border_radius=15)
        pygame.draw.rect(self.screen, btn_col, self.btn_switch, 2, border_radius=15)
        
        lbl_text = "PASSER AU WAN PRIVÉ" if self.current_broker == "primary" else "PASSER AU CLOUD"
        lbl_btn = self.font_mid.render(lbl_text, True, btn_col)
        self.screen.blit(lbl_btn, (self.btn_switch.centerx - lbl_btn.get_width()//2, self.btn_switch.centery - lbl_btn.get_height()//2))

    def run(self):
        def mqtt_thread():
            while self.running:
                try:
                    broker = MQTT_CONFIG["broker_primary"] if self.current_broker == "primary" else MQTT_CONFIG["broker_secondary"]
                    port = MQTT_CONFIG["port_primary"] if self.current_broker == "primary" else MQTT_CONFIG["port_secondary"]
                    
                    print(f"Connecting to {self.current_broker} broker: {broker}:{port}")
                    
                    self.client = mqtt.Client(client_id=MQTT_CONFIG["client_id"], transport="websockets")
                    self.client.username_pw_set(MQTT_CONFIG["username"], MQTT_CONFIG["password"])
                    
                    if self.current_broker == "primary":
                        self.client.tls_set(cert_reqs=ssl.CERT_REQUIRED, tls_version=ssl.PROTOCOL_TLS)
                    
                    self.client.on_connect = self.on_connect
                    self.client.on_message = self.on_message
                    
                    self.client.connect(broker, port, 30)
                    self.client.loop_forever()
                except Exception as e:
                    self.link_status["connected"] = False
                    print(f"MQTT Error ({self.current_broker}): {e}")
                    self.current_broker = "secondary" if self.current_broker == "primary" else "primary"
                    time.sleep(5)
                    
        Thread(target=mqtt_thread, daemon=True).start()
        
        while self.running:
            self.screen.fill(COLOR_BG)
            self.handle_events()
            
            # Alerte clignotante si alarme verrouillée
            if self.alarm_latched and (time.time() * 2) % 2 > 1:
                self.screen.fill((40, 0, 0))
                
            # Clignotement prioritaire pour intrusion porte
            if self.door_alarm_active and (time.time() * 4) % 2 > 1:
                self.screen.fill(COLOR_DANGER)
                
            if self.current_page == "TELEMETRY": self.draw_telemetry()
            elif self.current_page == "ALARMS": self.draw_alarms()
            elif self.current_page == "LINK": self.draw_link()
            
            self.draw_navbar()
            pygame.display.flip()
            self.clock.tick(30)
            
        pygame.quit()

if __name__ == "__main__":
    App().run()
