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

- [x] Étape 0 — Préparation environnement
- [x] Priorité 1 — Matter Door Lock sur réseau Thread existant — ✅ commissioning de bout en bout validé via Home Assistant + iPhone (17/08/2026) — voir [firmware/apps/lock/KNOWN-ISSUES.md](firmware/apps/lock/KNOWN-ISSUES.md)
- [ ] Priorité 2 — IMU 6 axes + Wake-up
- [ ] Priorité 3 — NFC (commissioning)
- [ ] Option 4 — Aliro / HomeKey (reportée)

Détail par étape : voir `docs/`. **Point de reprise pour la prochaine session : Priorité 2 (IMU 6 axes + Wake-up)**, ou sécurisation des factory data avant de dupliquer le firmware sur les ~20 unités (voir avertissement sécurité ci-dessous et dans `KNOWN-ISSUES.md`). Trois causes distinctes bloquaient le commissioning, toutes corrigées : deux bugs firmware (génération des factory data du SDK, Kconfig `CHIP_ENABLE_PAIRING_AUTOSTART=n` par défaut) et un réglage côté commissioner (« Enable test-net DCL usage » dans Home Assistant, nécessaire tant que le firmware utilise les certificats de développement du SDK) — voir [firmware/apps/lock/KNOWN-ISSUES.md](firmware/apps/lock/KNOWN-ISSUES.md). Build à utiliser : ajouter `-DEXTRA_CONF_FILE=firmware/apps/lock/pairing-autostart.conf` et lancer `python3 firmware/apps/lock/fix_factory_data.py <build_dir>` avant `west flash` (voir README du dossier `lock/`).

**⚠️ Avant de dupliquer sur les ~20 unités** : les factory data générées par le build (passcode, discriminator, salt SPAKE2) sont actuellement **identiques à chaque build** au lieu d'être uniques par unité — à corriger avant tout déploiement réel, voir `firmware/apps/lock/KNOWN-ISSUES.md`.

## Suivi des ~20 unités

Chaque unité physique a sa propre fiche dans `devices/unit-XX/` : firmware flashé, calibration IMU, MAC/EUI Thread, historique de commissioning, notes. Voir [devices/README.md](devices/README.md).
