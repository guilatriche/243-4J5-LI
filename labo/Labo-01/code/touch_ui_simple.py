import threading
import time
from queue import Queue

import curses
import serial
from evdev import InputDevice, ecodes, list_devices

# ---------- GESTION DU TOUCH ----------

class TouchReader(threading.Thread):
    def __init__(self, event_queue: Queue):
        super().__init__(daemon=True)
        self.event_queue = event_queue
        self.device = self._find_touch_device()
        if not self.device:
            # On log juste l'erreur, le programme continuera (sans touch)
            print("Attention: Aucun périphérique touchscreen trouvé.")
            self.min_x, self.max_x = 0, 100
            self.min_y, self.max_y = 0, 100
        else:
            # On récupère les infos d’axes pour calibrer
            abs_x = self.device.absinfo(ecodes.ABS_MT_POSITION_X)
            abs_y = self.device.absinfo(ecodes.ABS_MT_POSITION_Y)

            self.min_x, self.max_x = abs_x.min, abs_x.max
            self.min_y, self.max_y = abs_y.min, abs_y.max

        self.current_x = (self.min_x + self.max_x) // 2
        self.current_y = (self.min_y + self.max_y) // 2

    def _find_touch_device(self):
        """
        Essaie de trouver un device dont le nom contient 'touch' ou 'ft5406'
        (fréquent sur les écrans Raspberry Pi).
        """
        for path in list_devices():
            try:
                dev = InputDevice(path)
                name = dev.name.lower()
                if "touch" in name or "ft5406" in name:
                    return dev
            except:
                pass
        return None

    def run(self):
        if not self.device:
            return
        try:
            for event in self.device.read_loop():
                if event.type == ecodes.EV_ABS:
                    if event.code == ecodes.ABS_MT_POSITION_X:
                        self.current_x = event.value
                    elif event.code == ecodes.ABS_MT_POSITION_Y:
                        self.current_y = event.value

                elif event.type == ecodes.EV_KEY and event.code == ecodes.BTN_TOUCH:
                    # 1 = touch down, 0 = touch up
                    if event.value == 1:
                        self.event_queue.put(("tap", self.current_x, self.current_y))
        except Exception:
            pass


# ---------- UI CURSES ----------

class CoolConsoleUI:
    def __init__(self, stdscr, touch_reader: TouchReader, event_queue: Queue):
        self.stdscr = stdscr
        self.touch_reader = touch_reader
        self.event_queue = event_queue
        self.running = True
        self.status_message = "Prêt."
        self.red_on = False
        self.green_on = False

        # Init serial
        self.ser = None
        try:
            self.ser = serial.Serial('/dev/ttyACM0', 115200, timeout=1)
        except Exception:
            try:
                self.ser = serial.Serial('/dev/ttyUSB0', 115200, timeout=1)
            except Exception as e:
                self.status_message = f"Serial Error: {e}"

        self.buttons = []

    def _init_colors(self):
        curses.start_color()
        curses.use_default_colors()
        # 1: Blanc sur Noir (Défaut)
        curses.init_pair(1, curses.COLOR_WHITE, curses.COLOR_BLACK)
        # 2: Blanc sur Rouge (ROUGE Actif)
        curses.init_pair(2, curses.COLOR_WHITE, curses.COLOR_RED)
        # 3: Blanc sur Vert (VERT Actif)
        curses.init_pair(3, curses.COLOR_WHITE, curses.COLOR_GREEN)
        # 4: Rouge sur Noir (ROUGE Inactif)
        curses.init_pair(4, curses.COLOR_RED, curses.COLOR_BLACK)
        # 5: Vert sur Noir (VERT Inactif)
        curses.init_pair(5, curses.COLOR_GREEN, curses.COLOR_BLACK)

    def _build_buttons(self, h, w):
        """
        Construit 2 gros boutons centrés.
        """
        self.buttons = []
        margin = 2
        
        # On divise la hauteur disponible par 2
        available_h = h - (margin * 3)
        btn_h = available_h // 2
        btn_w = w - (margin * 2)

        if btn_h < 3: btn_h = 3 # Minimum height safety

        # Bouton ROUGE (Haut)
        self.buttons.append({
            "label": "ROUGE",
            "row": margin,
            "col": margin,
            "height": btn_h,
            "width": btn_w,
            "active": self.red_on,
            "pair_on": 2,
            "pair_off": 4
        })

        # Bouton VERT (Bas)
        self.buttons.append({
            "label": "VERT",
            "row": margin + btn_h + margin,
            "col": margin,
            "height": btn_h,
            "width": btn_w,
            "active": self.green_on,
            "pair_on": 3,
            "pair_off": 5
        })

    def _draw(self):
        # Fond global toujours noir pour bien faire ressortir les boutons
        self.stdscr.bkgd(' ', curses.color_pair(1))

        self.stdscr.erase()
        h, w = self.stdscr.getmaxyx()

        # Update button definitions based on screen size and state
        self._build_buttons(h, w)

        for btn in self.buttons:
            # On inverse un peu la logique visuelle pour que les boutons ressortent 
            # sur le fond coloré, ou on garde le style simple.
            # Ici on garde la logique existante : bouton actif = couleur vive.
            attr_idx = btn["pair_on"] if btn["active"] else btn["pair_off"]
            attr = curses.color_pair(attr_idx)
            if btn["active"]:
                attr |= curses.A_BOLD
            else:
                attr |= curses.A_DIM

            # Dessin du rectangle plein
            for r in range(btn["row"], btn["row"] + btn["height"]):
                if 0 <= r < h:
                    self.stdscr.attron(attr)
                    try:
                        self.stdscr.addstr(r, btn["col"], " " * btn["width"])
                    except curses.error:
                        pass
                    self.stdscr.attroff(attr)

            # Label centré
            label_text = f"[ {btn['label']} ]"
            label_col = btn["col"] + max(0, (btn["width"] - len(label_text)) // 2)
            label_row = btn["row"] + btn["height"] // 2
            
            if 0 <= label_row < h:
                self.stdscr.attron(attr)
                try:
                    self.stdscr.addstr(label_row, label_col, label_text)
                except curses.error:
                    pass
                self.stdscr.attroff(attr)

        # Status footer
        info = f"Q: Quitter | {self.status_message}"
        try:
            self.stdscr.addstr(h-1, 0, info[:w-1])
        except:
            pass

        self.stdscr.refresh()

    def _touch_to_rowcol(self, x_raw, y_raw):
        h, w = self.stdscr.getmaxyx()
        dx = max(1, self.touch_reader.max_x - self.touch_reader.min_x)
        dy = max(1, self.touch_reader.max_y - self.touch_reader.min_y)

        x_norm = (x_raw - self.touch_reader.min_x) / dx
        y_norm = (y_raw - self.touch_reader.min_y) / dy

        col = int(x_norm * (w - 1))
        row = int(y_norm * (h - 1))

        row = max(0, min(h - 1, row))
        col = max(0, min(w - 1, col))
        return row, col

    def _handle_touch_tap(self, x_raw, y_raw):
        row, col = self._touch_to_rowcol(x_raw, y_raw)

        clicked_btn = None
        for btn in self.buttons:
            if (btn["row"] <= row < btn["row"] + btn["height"] and
                    btn["col"] <= col < btn["col"] + btn["width"]):
                clicked_btn = btn
                break
        
        if not clicked_btn:
            return

        label = clicked_btn["label"]
        cmd = None

        if label == "ROUGE":
            self.red_on = not self.red_on
            # Exclusive check: si on allume rouge, on éteint vert
            if self.red_on:
                self.green_on = False
                cmd = b"rouge\n"
            else:
                # Si on éteint le rouge
                cmd = b"off\n"

        elif label == "VERT":
            self.green_on = not self.green_on
            if self.green_on:
                self.red_on = False
                cmd = b"verte\n"
            else:
                # Si on éteint le vert
                cmd = b"off\n"

        if cmd and self.ser:
            try:
                self.ser.write(cmd)
                self.status_message = f"Sent: {cmd.strip()}"
            except Exception as e:
                self.status_message = f"Err: {e}"

    def run(self):
        self.stdscr.nodelay(True)
        curses.curs_set(0)
        self._init_colors()

        last_redraw = 0

        while self.running:
            now = time.time()
            if now - last_redraw > 0.05:
                self._draw()
                last_redraw = now

            # Clavier
            try:
                ch = self.stdscr.getch()
            except curses.error:
                ch = -1
            if ch == ord('q'):
                self.running = False

            # Touch
            try:
                event = self.event_queue.get_nowait()
            except Exception:
                event = None

            if event:
                kind, x_raw, y_raw = event
                if kind == "tap":
                    self._handle_touch_tap(x_raw, y_raw)

            time.sleep(0.01)


def main(stdscr):
    event_queue = Queue()
    touch_reader = TouchReader(event_queue)
    touch_reader.start()

    ui = CoolConsoleUI(stdscr, touch_reader, event_queue)
    ui.run()


if __name__ == "__main__":
    curses.wrapper(main)
