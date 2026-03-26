import os
import pygame
import paho.mqtt.client as mqtt
import json
import ssl
import time
import math
import random
from threading import Thread

# Configuration du driver vidéo pour Raspberry Pi (KMS/DRM)
if not os.environ.get('DISPLAY'):
    os.environ['SDL_VIDEODRIVER'] = 'kmsdrm'
    os.environ['SDL_INPUT_LINUX_EVDEV'] = '1'
os.environ['SDL_VIDEO_GL_DRIVER'] = 'libGLESv2.so'

# Configuration MQTT
try:
    from mqtt_config import MQTT_CONFIG
except ImportError:
    MQTT_CONFIG = {
        "broker": "mqtt.gretier.xyz",
        "port": 443,
        "username": "esp_user",
        "password": "Teladmin1$",
        "client_id": "rpi-interface-v2",
        "topic_root": "etudiant/guillaume-retier"
    }

# Couleurs
COLOR_BG = (5, 5, 10)
COLOR_TEXT = (236, 240, 241)
COLOR_ACCENT = (52, 152, 219)
COLOR_SUCCESS = (46, 204, 113)
COLOR_DANGER = (231, 76, 60)
COLOR_WARN = (241, 196, 15)
COLOR_HUD_BG = (15, 15, 25)
COLOR_ENGINE = (100, 200, 255)

# Paramètres du jeu
SCREEN_W, SCREEN_H = 720, 1280
WIN_DISTANCE = 1000.0
TIME_LIMIT = 120.0

class Star:
    def __init__(self):
        self.x = random.randint(0, SCREEN_W)
        self.y = random.randint(0, SCREEN_H)
        self.size = random.randint(1, 3)
        self.speed = random.uniform(0.5, 3.0)

    def update(self, speed_multiplier):
        self.y += self.speed * speed_multiplier
        if self.y > SCREEN_H:
            self.y = 0
            self.x = random.randint(0, SCREEN_W)

    def draw(self, surface):
        pygame.draw.circle(surface, (200, 200, 255), (int(self.x), int(self.y)), self.size)

class Explosion(pygame.sprite.Sprite):
    def __init__(self, x, y, max_radius=350):
        super().__init__()
        self.x, self.y = x, y
        self.radius = 10
        self.max_radius = max_radius
        self.image = pygame.Surface((max_radius*2, max_radius*2), pygame.SRCALPHA)
        self.rect = self.image.get_rect(center=(x, y))
        self.alpha = 255

    def update(self, dt):
        self.radius += 20
        self.alpha -= 10
        if self.radius >= self.max_radius or self.alpha <= 0:
            self.kill()
        else:
            self.image.fill((0,0,0,0))
            pygame.draw.circle(self.image, (255, 150, 50, self.alpha), (self.max_radius, self.max_radius), self.radius)
            pygame.draw.circle(self.image, (255, 255, 200, self.alpha), (self.max_radius, self.max_radius), self.radius//2)

class Obstacle(pygame.sprite.Sprite):
    def __init__(self):
        super().__init__()
        self.size = random.randint(60, 100)
        self.base_image = pygame.Surface((self.size, self.size), pygame.SRCALPHA)
        pts = self.get_asteroid_points(self.size)
        pygame.draw.polygon(self.base_image, (100, 100, 110), pts)
        pygame.draw.polygon(self.base_image, (60, 60, 70), pts, 3)
        self.image = self.base_image.copy()
        self.rect = self.image.get_rect()
        self.rect.x = random.randint(0, SCREEN_W - self.size)
        self.rect.y = -self.size
        self.speed = random.uniform(2, 5)

    def get_asteroid_points(self, size):
        points = []
        for i in range(10):
            angle = math.radians(i * 36)
            r = size/2 * random.uniform(0.7, 1.0)
            points.append((size/2 + math.cos(angle)*r, size/2 + math.sin(angle)*r))
        return points

    def update(self, speed_multiplier):
        self.rect.y += self.speed * speed_multiplier
        if self.rect.y > SCREEN_H:
            self.kill()

class Projectile(pygame.sprite.Sprite):
    def __init__(self, x, y, p_type):
        super().__init__()
        self.p_type = p_type
        if p_type == "mitraille":
            self.image = pygame.Surface((10, 25), pygame.SRCALPHA)
            pygame.draw.rect(self.image, COLOR_SUCCESS, (0, 0, 10, 25), border_radius=5)
            self.speed = -25
        else: # bombe
            self.image = pygame.Surface((40, 40), pygame.SRCALPHA)
            pygame.draw.circle(self.image, COLOR_DANGER, (20, 20), 20)
            pygame.draw.circle(self.image, COLOR_WARN, (20, 20), 20, 4)
            self.speed = -12
        self.rect = self.image.get_rect(center=(x, y))

    def update(self, _):
        self.rect.y += self.speed
        if self.rect.bottom < 0:
            self.kill()

class Player(pygame.sprite.Sprite):
    def __init__(self):
        super().__init__()
        self.size = 120
        self.image = pygame.Surface((self.size, self.size), pygame.SRCALPHA)
        self.rect = self.image.get_rect(center=(SCREEN_W//2, SCREEN_H - 250))
        self.target_x = SCREEN_W // 2
        self.target_y = SCREEN_H - 250
        self.health = 100.0
        self.score = 0
        self.distance = 0.0
        self.time_left = TIME_LIMIT

    def draw_falcon(self, surface, roll):
        cx, cy = self.size // 2, self.size // 2
        pygame.draw.arc(surface, COLOR_ENGINE, (cx-45, cy+30, 90, 20), 0, 3.14, 8)
        pygame.draw.circle(surface, (180, 180, 190), (cx, cy), 45)
        pygame.draw.circle(surface, (100, 100, 110), (cx, cy), 45, 3)
        pygame.draw.rect(surface, (180, 180, 190), (cx-35, cy-60, 25, 50))
        pygame.draw.rect(surface, (180, 180, 190), (cx+10, cy-60, 25, 50))
        pygame.draw.rect(surface, (100, 100, 110), (cx-35, cy-60, 25, 50), 2)
        pygame.draw.rect(surface, (100, 100, 110), (cx+10, cy-60, 25, 50), 2)
        pygame.draw.circle(surface, (160, 160, 170), (cx+50, cy-10), 15)
        pygame.draw.circle(surface, (50, 50, 60), (cx+50, cy-10), 15, 2)
        pygame.draw.rect(surface, (160, 160, 170), (cx+35, cy-15, 15, 10))

    def update(self, raw_sensors, dt):
        roll = raw_sensors.get("roll", 0)
        pitch = raw_sensors.get("pitch", 0)
        self.target_x += roll * 0.8
        self.target_y += pitch * 0.8
        self.target_x = max(60, min(SCREEN_W - 60, self.target_x))
        self.target_y = max(100, min(SCREEN_H - 380, self.target_y))
        self.rect.centerx += (self.target_x - self.rect.centerx) * 15 * dt
        self.rect.centery += (self.target_y - self.rect.centery) * 15 * dt
        self.image.fill((0,0,0,0))
        self.draw_falcon(self.image, roll)

class App:
    def __init__(self):
        pygame.init()
        self.screen = pygame.display.set_mode((SCREEN_W, SCREEN_H), pygame.FULLSCREEN if os.environ.get('SDL_VIDEODRIVER') == 'kmsdrm' else 0)
        self.clock = pygame.time.Clock()
        self.running = True
        self.state = "MENU"
        self.font_big = pygame.font.SysFont("Arial", 64, bold=True)
        self.font_mid = pygame.font.SysFont("Arial", 32, bold=True)
        self.font_hud = pygame.font.SysFont("Arial", 20, bold=True)
        
        self.data = {"btn1": False, "connected": False, "net": "wifi", "uptime": 0, "rssi": 0}
        self.raw_sensors = {"pot1": 0, "pot2": 0, "pot3": 0, "roll": 0.0, "pitch": 0.0}
        self.last_btn_state = False
        self.hardware_synced = True
        self.active_leds = [False, False, False]
        
        self.client = mqtt.Client(transport="websockets")
        self.client.tls_set(cert_reqs=ssl.CERT_REQUIRED, tls_version=ssl.PROTOCOL_TLS)
        self.client.username_pw_set(MQTT_CONFIG["username"], MQTT_CONFIG["password"])
        self.client.on_connect = self.on_connect
        self.client.on_message = self.on_message
        
        self.stars = [Star() for _ in range(60)]
        self.player = Player()
        self.obstacles = pygame.sprite.Group()
        self.projectiles = pygame.sprite.Group()
        self.explosions = pygame.sprite.Group()
        self.weapon_types = ["mitraille", "laser", "bombe"]
        self.current_weapon_idx = 0
        self.last_shot_time = 0
        self.shake_offset = (0, 0)
        self.stab_target = 2048
        
        self.btn_wifi = pygame.Rect(110, 450, 500, 150)
        self.btn_lte = pygame.Rect(110, 650, 500, 150)
        self.weapon_btns = [
            pygame.Rect(40, 1130, 210, 70),
            pygame.Rect(255, 1130, 210, 70),
            pygame.Rect(470, 1130, 210, 70)
        ]

    def on_connect(self, client, userdata, flags, rc):
        if rc == 0:
            self.data["connected"] = True
            client.subscribe(f"{MQTT_CONFIG['topic_root']}/#")
            self.all_leds_off()

    def on_message(self, client, userdata, msg):
        try:
            payload = json.loads(msg.payload.decode())
            topic = msg.topic
            
            if "/actuators/led" in topic:
                state = payload.get("state", "off").lower() == "on"
                try:
                    idx = int(topic.split("led")[-1]) - 1
                    if 0 <= idx < 3:
                        self.active_leds[idx] = state
                        if state: 
                            self.current_weapon_idx = idx
                            self.hardware_synced = True
                except: pass
                return

            if "sensors/pots" in topic: self.raw_sensors.update(payload)
            elif "sensors/buttons" in topic: self.data["btn1"] = payload.get("btn1", False)
            elif "sensors/accel" in topic: self.raw_sensors.update(payload)
            elif "status" in topic:
                self.data["net"] = payload.get("network", "wifi")
                self.data["uptime"] = payload.get("uptime", 0)
                self.data["rssi"] = payload.get("rssi", 0)
                if not self.hardware_synced:
                    self.select_weapon(self.current_weapon_idx)
        except: pass

    def all_leds_off(self):
        """Hard Reset sans blocage."""
        self.active_leds = [False, False, False]
        for i in range(1, 4):
            self.client.publish(f"{MQTT_CONFIG['topic_root']}/actuators/led{i}", json.dumps({"state": "off"}))

    def select_weapon(self, idx):
        for i in range(1, 4):
            state = "on" if (i-1) == idx else "off"
            self.client.publish(f"{MQTT_CONFIG['topic_root']}/actuators/led{i}", json.dumps({"state": state}))

    def fire(self):
        now = time.time()
        if self.weapon_types[self.current_weapon_idx] != "laser":
            cd = 0.15 if self.current_weapon_idx == 0 else 1.0
            if now - self.last_shot_time > cd:
                p = Projectile(self.player.rect.centerx, self.player.rect.top, self.weapon_types[self.current_weapon_idx])
                self.projectiles.add(p)
                self.last_shot_time = now

    def run(self):
        def mqtt_thread():
            try:
                self.client.connect(MQTT_CONFIG["broker"], 443, 15)
                self.client.loop_forever()
            except: pass
        Thread(target=mqtt_thread, daemon=True).start()
        while self.running:
            dt = self.clock.tick(60) / 1000.0
            self.handle_events()
            if self.state == "GAME": self.update_game(dt)
            self.draw()
            pygame.display.flip()
        pygame.quit()

    def handle_events(self):
        for event in pygame.event.get():
            if event.type == pygame.QUIT: self.running = False
            elif event.type == pygame.MOUSEBUTTONDOWN: self.handle_press(event.pos)
            elif event.type == pygame.FINGERDOWN: self.handle_press((event.x * SCREEN_W, event.y * SCREEN_H))
        if self.data["btn1"]:
            if self.weapon_types[self.current_weapon_idx] != "laser":
                if not self.last_btn_state: self.fire()
        self.last_btn_state = self.data["btn1"]

    def handle_press(self, pos):
        if self.state == "MENU":
            if self.btn_wifi.collidepoint(pos):
                self.client.publish(f"{MQTT_CONFIG['topic_root']}/config", json.dumps({"network": "wifi"}))
                self.all_leds_off(); self.hardware_synced = False; self.state = "GAME"
            elif self.btn_lte.collidepoint(pos):
                self.client.publish(f"{MQTT_CONFIG['topic_root']}/config", json.dumps({"network": "lte"}))
                self.all_leds_off(); self.hardware_synced = False; self.state = "GAME"
        elif self.state == "GAME":
            for i, btn in enumerate(self.weapon_btns):
                if btn.collidepoint(pos):
                    self.hardware_synced = False
                    self.select_weapon(i)
        elif self.state in ["GAMEOVER", "WIN"]:
            self.all_leds_off(); self.player = Player()
            self.obstacles.empty(); self.projectiles.empty(); self.explosions.empty()
            self.state = "MENU"

    def update_game(self, dt):
        pot1, pot2, pot3 = self.raw_sensors["pot1"], self.raw_sensors["pot2"], self.raw_sensors["pot3"]
        speed_mult = 1.0 + (pot1 / 4095.0) * 8.0
        is_high_speed = (pot1 > 2048)
        shake_intensity = 0
        self.stab_target = 500 + int((pot1 / 4095.0) * 3000)
        if is_high_speed:
            diff = abs(pot3 - self.stab_target)
            if diff > 300:
                shake_intensity = int((diff / 4095.0) * 20)
                self.player.health -= shake_intensity * 0.25 * dt
        self.shake_offset = (random.randint(-shake_intensity, shake_intensity), random.randint(-shake_intensity, shake_intensity)) if shake_intensity > 0 else (0, 0)
        self.player.distance += speed_mult * dt * 2.0
        if is_high_speed and shake_intensity == 0: self.player.distance += speed_mult * dt * 1.5
        self.player.time_left -= dt
        shield_intensity = pot2 / 4095.0
        if shield_intensity > 0.05:
            self.player.health -= shield_intensity * 5.0 * dt
        if self.player.time_left <= 0 or self.player.health <= 0: 
            self.state = "GAMEOVER"; self.all_leds_off()
        if self.player.distance >= WIN_DISTANCE: 
            self.state = "WIN"; self.all_leds_off()
        for star in self.stars: star.update(speed_mult)
        if random.random() < 0.015 * speed_mult: self.obstacles.add(Obstacle())
        self.player.update(self.raw_sensors, dt)
        self.obstacles.update(speed_mult)
        self.projectiles.update(speed_mult)
        self.explosions.update(dt)
        if self.data["btn1"] and self.weapon_types[self.current_weapon_idx] == "laser":
            beam_w = 10 + int(shield_intensity * 30)
            beam_rect = pygame.Rect(self.player.rect.centerx - beam_w//2, 0, beam_w, self.player.rect.top)
            for obs in list(self.obstacles):
                if beam_rect.colliderect(obs.rect): obs.kill(); self.player.score += 5
        hits = pygame.sprite.spritecollide(self.player, self.obstacles, True)
        if hits: self.player.health -= 20 * (1.0 - shield_intensity)
        for p in self.projectiles:
            hit_obs = pygame.sprite.spritecollide(p, self.obstacles, True)
            if hit_obs:
                if p.p_type == "bombe": self.explosions.add(Explosion(p.rect.centerx, p.rect.centery, 350))
                p.kill()
        for expl in self.explosions:
            killed = pygame.sprite.spritecollide(expl, self.obstacles, True, pygame.sprite.collide_circle)
            if killed: self.player.score += len(killed) * 10

    def draw(self):
        game_surf = pygame.Surface((SCREEN_W, SCREEN_H))
        game_surf.fill(COLOR_BG)
        for star in self.stars: star.draw(game_surf)
        
        if self.state == "MENU":
            self.draw_text("KESSEL RUN", self.font_big, COLOR_WARN, SCREEN_W//2, 200, game_surf)
            pygame.draw.rect(game_surf, COLOR_ACCENT, self.btn_wifi, border_radius=20)
            pygame.draw.rect(game_surf, COLOR_SUCCESS, self.btn_lte, border_radius=20)
            self.draw_text("WIFI MODE", self.font_mid, COLOR_HUD_BG, self.btn_wifi.centerx, self.btn_wifi.centery, game_surf)
            self.draw_text("LTE MODE", self.font_mid, COLOR_HUD_BG, self.btn_lte.centerx, self.btn_lte.centery, game_surf)
        elif self.state == "GAME":
            # Header Status (Requirements)
            pygame.draw.rect(game_surf, (10, 10, 20), (0, 0, SCREEN_W, 60))
            net_status = f"NET: {self.data['net'].upper()}"
            mqtt_status = "MQTT: OK" if self.data["connected"] else "MQTT: ERR"
            self.draw_text(net_status, self.font_hud, COLOR_ACCENT, 120, 30, game_surf)
            self.draw_text(mqtt_status, self.font_hud, COLOR_SUCCESS if self.data["connected"] else COLOR_DANGER, 600, 30, game_surf)

            if self.data["btn1"] and self.weapon_types[self.current_weapon_idx] == "laser":
                bx = self.player.rect.centerx
                pygame.draw.line(game_surf, (200, 255, 255), (bx, self.player.rect.top), (bx, 0), 15)
                pygame.draw.line(game_surf, COLOR_ACCENT, (bx, self.player.rect.top), (bx, 0), 5)
            self.explosions.draw(game_surf); self.projectiles.draw(game_surf); self.obstacles.draw(game_surf)
            game_surf.blit(self.player.image, self.player.rect)
            s_val = self.raw_sensors["pot2"]
            if s_val > 200:
                rad = 70 + int((s_val/4095.0)*60)
                pygame.draw.circle(game_surf, (100, 200, 255, 100), self.player.rect.center, rad, 4)
            self.draw_hud(game_surf)
        elif self.state == "GAMEOVER":
            self.draw_text("MISSION ECHEC", self.font_big, COLOR_DANGER, SCREEN_W//2, SCREEN_H//2, game_surf)
        elif self.state == "WIN":
            self.draw_text("RECORD BATTU !", self.font_big, COLOR_SUCCESS, SCREEN_W//2, SCREEN_H//2, game_surf)
            
        self.screen.blit(game_surf, self.shake_offset)

    def draw_hud(self, surf):
        pygame.draw.rect(surf, COLOR_HUD_BG, (0, 1000, SCREEN_W, 280))
        prog = min(1.0, self.player.distance / WIN_DISTANCE)
        pygame.draw.rect(surf, (50, 50, 50), (50, 100, 620, 20))
        pygame.draw.rect(surf, COLOR_SUCCESS, (50, 100, 620 * prog, 20))
        self.draw_text(f"DISTANCE: {int(self.player.distance)}/1000 KM", self.font_hud, COLOR_TEXT, SCREEN_W//2, 80, surf)
        self.draw_text(f"CHRONO: {max(0, self.player.time_left):.1f}s", self.font_mid, COLOR_TEXT, SCREEN_W//2, 150, surf)
        
        self.draw_gauge(60, 1030, 180, "THRUST", self.raw_sensors["pot1"], COLOR_ACCENT, surf)
        self.draw_gauge(270, 1030, 180, "SHIELD", self.raw_sensors["pot2"], COLOR_DANGER, surf)
        self.draw_gauge(480, 1030, 180, "STABILIZER", self.raw_sensors["pot3"], COLOR_WARN, surf)
        if self.raw_sensors["pot1"] > 2048:
            tx = 480 + (self.stab_target/4095.0)*180
            pygame.draw.line(surf, (255, 255, 255), (tx, 1050), (tx, 1075), 4)
        
        colors = [COLOR_SUCCESS, COLOR_ACCENT, COLOR_DANGER]
        for i, btn in enumerate(self.weapon_btns):
            is_physically_on = self.active_leds[i]
            col = colors[i] if is_physically_on else (30, 30, 40)
            pygame.draw.rect(surf, col, btn, border_radius=10)
            border_col = (255, 255, 255) if self.current_weapon_idx == i else (70, 70, 80)
            pygame.draw.rect(surf, border_col, btn, 2, border_radius=10)
            self.draw_text(["MITRAILLE", "BEAM", "BOMB"][i], self.font_hud, COLOR_TEXT, btn.centerx, btn.centery, surf)

        pygame.draw.rect(surf, (100, 0, 0), (50, 1240, 620, 15))
        pygame.draw.rect(surf, COLOR_SUCCESS, (50, 1240, 6.2 * self.player.health, 15))

    def draw_gauge(self, x, y, w, label, val, col, surf):
        pygame.draw.rect(surf, (10, 10, 20), (x, y+20, w, 25), border_radius=5)
        pygame.draw.rect(surf, col, (x, y+20, (val/4095.0)*w, 25), border_radius=5)
        # Numerical value overlay
        self.draw_text(str(val), self.font_hud, COLOR_TEXT, x + w//2, y + 32, surf)
        self.draw_text(label, self.font_hud, (200,200,200), x + w//2, y, surf)

    def draw_text(self, text, font, color, x, y, surf=None):
        if surf is None: surf = self.screen
        img = font.render(text, True, color)
        surf.blit(img, (x - img.get_width()//2, y - img.get_height()//2))

if __name__ == "__main__":
    App().run()
