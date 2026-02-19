import serial
import time
import sys

PORT = "/dev/ttyACM0"
BAUDRATE = 115200

def test_leds():
    try:
        print(f"Connexion à {PORT} @ {BAUDRATE}...")
        # Timeout est important pour ne pas bloquer si pas de réponse
        ser = serial.Serial(PORT, BAUDRATE, timeout=1)
        time.sleep(2) # Attendre que le port série s'initialise (reset Arduino)

        print("Envoi de la commande: 'rouge'")
        ser.write(b"rouge\n")
        time.sleep(2)

        print("Envoi de la commande: 'verte'")
        ser.write(b"verte\n")
        time.sleep(2)
        
        print("Test terminé.")
        ser.close()
        
    except serial.SerialException as e:
        print(f"Erreur série : {e}")
    except Exception as e:
        print(f"Erreur : {e}")

if __name__ == "__main__":
    test_leds()
