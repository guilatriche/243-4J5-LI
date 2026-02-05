import tkinter as tk
import serial

# Configuration série
try:
    ser = serial.Serial('/dev/ttyACM0', 115200)
except Exception:
    ser = None
    print("Port /dev/ttyACM0 non détecté - Mode simulation")

def envoyer_rouge():
    if ser: ser.write(b"rouge\n")
    print("Envoyé: rouge")

def envoyer_verte():
    if ser: ser.write(b"verte\n")
    print("Envoyé: verte")

# Interface UI
root = tk.Tk()
root.title("Contrôle LED")
root.geometry("400x600")

# Bouton ROUGE
btn_rouge = tk.Button(root, text="ROUGE", bg="red", fg="white",
                      font=("Arial", 30, "bold"),
                      command=envoyer_rouge)
btn_rouge.pack(expand=True, fill="both")

# Bouton VERT
btn_vert = tk.Button(root, text="VERT", bg="green", fg="white",
                     font=("Arial", 30, "bold"),
                     command=envoyer_verte)
btn_vert.pack(expand=True, fill="both")

root.mainloop()
