# Conception Electronique (KiCad)

Ce dossier contient les fichiers de conception du shield pour le projet Mi-Session.

## Contenu
- `shield.kicad_pro` : Fichier de projet KiCad.
- `shield.kicad_sch` : Schéma électronique.
- `shield.kicad_pcb` : Dessin du circuit imprimé (Layout).
- `projet_de_mi-session.pretty/` : Bibliothèque d'empreintes personnalisées (Footprints).
- `fp-lib-table` : Table des bibliothèques locales pour assurer la portabilité du projet.

## Notes Techniques
Les empreintes pour le LilyGO, le potentiomètre B10K et le bouton ont été créées sur mesure ou adaptées. Le fichier `fp-lib-table` permet à KiCad de charger automatiquement ces empreintes sans configuration manuelle.
