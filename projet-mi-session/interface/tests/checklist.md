## Checklist de validation — Interface

### Affichage
- [x] Interface s'ouvre en plein écran sans erreur
- [x] Données des boutons mises à jour en temps réel (< 500 ms)
- [x] Valeurs des potentiomètres affichées avec unité ou pourcentage
- [x] Données accéléromètre (X, Y, Z) affichées et actualisées
- [x] Indicateur de connexion MQTT visible et exact

### Contrôle
- [x] Chaque bouton tactile LED envoie la commande MQTT correcte
- [x] L'état de la LED se reflète dans l'interface (feedback visuel passif)

### Démarrage automatique
- [x] Service systemd `iot-interface.service` créé et activé
- [x] Interface se lance automatiquement après un redémarrage
- [x] Service redémarre après un kill forcé

### Qualité
- [x] Aucune exception Python non gérée lors de l'utilisation normale
- [x] Interface utilisable au doigt (zones tactiles ≥ 44×44 px)
