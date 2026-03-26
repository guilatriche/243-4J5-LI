## Checklist de validation — Firmware

### Périphériques
- [x] MPU6050 détecté sur le bus I2C (adresse 0x68)
- [x] Valeurs X, Y, Z cohérentes (axe Z ≈ 9.81 m/s² au repos)
- [x] Chaque bouton réagit à la pression (digitalRead correct)
- [x] Chaque potentiomètre produit une plage 0–4095 (ADC 12 bits)
- [x] Chaque LED s'allume et s'éteint sur commande

### Communication MQTT
- [x] Connexion LTE établie (signal RSSI affiché)
- [x] Connexion au broker MQTT réussie
- [x] Publication sur tous les topics configurés
- [x] Réception des commandes LED et exécution correcte
- [x] Reconnexion automatique après coupure (test manuel)

### Robustesse
- [x] Aucun crash après 5 minutes de fonctionnement continu
- [x] Gestion du cas MPU6050 absent (message d'erreur, pas de plantage)
