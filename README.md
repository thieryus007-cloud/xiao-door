# XIAO-Door — Capteur de Porte Intelligent Matter over Thread

Projet de capteur de porte connecté basé sur le **XIAO nRF54LM20A Sense**, intégré à Home Assistant via **Matter over Thread**. ~20 unités à produire, flasher et déployer.

Cahier des charges complet : [docs/XIAO-Door-specs.md](docs/XIAO-Door-specs.md)

## Structure du dépôt

| Dossier | Contenu |
|---|---|
| `docs/` | Cahier des charges, guides d'étapes, notes techniques |
| `firmware/` | Projet Zephyr / nRF Connect SDK (overlays, code source, Kconfig) |
| `hardware/` | Schémas, fichiers KiCad/Gerber |
| `apps/` | Applications compagnon (mobile, scripts, outils de commissioning) |
| `images/` | Photos, captures d'écran, schémas exportés |
| `devices/` | Un dossier par unité physique (voir `devices/README.md`) |

## Avancement

- [ ] Étape 0 — Préparation environnement
- [ ] Priorité 1 — Matter Door Lock sur réseau Thread existant
- [ ] Priorité 2 — IMU 6 axes + Wake-up
- [ ] Priorité 3 — NFC (commissioning)
- [ ] Option 4 — Aliro / HomeKey (reportée)

Détail par étape : voir `docs/`.

## Suivi des ~20 unités

Chaque unité physique a sa propre fiche dans `devices/unit-XX/` : firmware flashé, calibration IMU, MAC/EUI Thread, historique de commissioning, notes. Voir [devices/README.md](devices/README.md).
