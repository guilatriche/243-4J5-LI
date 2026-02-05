# AGENTS.md - Labo 01: Environnement Distant & Contrôle Hardware

Ce document définit les processus de développement pour le **Laboratoire 01** (Raspberry Pi + LilyGO ESP32).

## 1. Environnement & Commandes

### Arduino (LilyGO ESP32)
Le code Arduino (`.ino`) contrôle le hardware (LEDs) via le port série.
**Outil** : `arduino-cli` est utilisé en ligne de commande.

*   **Compilation** :
    ```bash
    arduino-cli compile --fqbn esp32:esp32:esp32 led-control/led-control.ino
    ```
*   **Téléversement** (LilyGO connecté sur `/dev/ttyUSB0`) :
    ```bash
    arduino-cli upload -p /dev/ttyUSB0 --fqbn esp32:esp32:esp32 led-control/led-control.ino
    ```
*   **Moniteur Série** :
    ```bash
    arduino-cli monitor -p /dev/ttyUSB0 -c baudrate=115200
    ```

### Python (Raspberry Pi)
Scripts d'interface tactile et d'animations terminal.
**Exécution Standard** : `python3 nom_du_script.py`

*   **Interface Tactile (Écran officiel)** :
    Pour prendre le contrôle du framebuffer/tty1 (écran physique) :
    ```bash
    sudo chvt 1
    sudo setsid sh -c 'exec </dev/tty1 >/dev/tty1 2>&1 python3 led-control/touch_ui_led.py'
    ```
    *Note: Nécessite `sudo` pour l'accès aux inputs (`evdev`) et au TTY.*

*   **Animations** :
    Utilisent la librairie `curses`. Exécuter simplement dans un terminal SSH ou local :
    ```bash
    python3 terminal-animation/matrix_animation.py
    ```

## 2. Standards de Code

### Python
*   **Style** : PEP8 simplifié.
*   **Imports** : Groupés au début. `import curses`, `import serial`, `import time`.
*   **Gestion d'Erreurs** :
    *   `try...except KeyboardInterrupt`: Pour quitter proprement les boucles `while True`.
    *   `curses.wrapper(main)`: Pour gérer correctement l'initialisation/nettoyage du terminal.
    *   Port Série : Gérer le cas où `/dev/ttyUSB0` est absent ou occupé.
*   **Hardware** : Utiliser des chemins absolus pour les devices si nécessaire, mais préférer les variables configurables.

### C++ (Arduino)
*   **Structure** : `setup()` pour l'initialisation, `loop()` pour la logique principale.
*   **Constantes** : Utiliser `#define` pour les broches (ex: `#define LED_ROUGE 25`).
*   **Série** : Toujours initialiser avec `Serial.begin(115200);`.
*   **Non-bloquant** : Éviter `delay()` long dans `loop()` si on doit écouter le port série. Utiliser `Serial.available() > 0`.

### Documentation & Conventions
*   **Langue** : Commentaires et documentation en **Français**.
*   **Noms de variables** : `snake_case` pour Python, `camelCase` pour C++. Anglais accepté pour les noms techniques (`ledPin`, `serialData`).
*   **Logs** : En Python, utiliser `print()` (si non-curses) ou écrire dans un fichier de log si l'interface graphique capture stdout.

## 3. Workflow de Test
1.  **Hardware** : Compiler et téléverser le code Arduino.
2.  **Validation Série** : Tester les commandes (`rouge`, `vert`, `off`) via `arduino-cli monitor`.
3.  **Interface** : Lancer le script Python. Vérifier la détection des touches et l'envoi des commandes sur le port série.
