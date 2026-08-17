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
- [ ] Priorité 2 — IMU 6 axes + Wake-up — 🟡 code écrit et **fonctionnel sur matériel réel** (lecture IMU + cluster Matter Boolean State confirmés sur `unit-02`/`unit-03`), mais **`unit-01` a un défaut matériel isolé** empêchant l'IMU de fonctionner dessus — voir « Test comparatif sur 3 unités » dans [firmware/apps/lock/KNOWN-ISSUES.md](firmware/apps/lock/KNOWN-ISSUES.md). Reste à faire : commissioning + validation en conditions réelles dans HA sur `unit-02` ou `unit-03`.
- [ ] Priorité 3 — NFC (commissioning) — 🟡 déjà actif sans code supplémentaire (framework Nordic partagé, `CONFIG_CHIP_NFC_ONBOARDING_PAYLOAD=y` par défaut + `&nfct` déjà activé), **bloqué uniquement par la soudure de l'antenne NFC (pads N1/N2)**, jamais confirmée faite sur aucune unité — voir `docs/XIAO-Door-specs.md` §Priorité 3
- [ ] Option 4 — Aliro / HomeKey (reportée)

Détail par étape : voir `docs/`. **Point de reprise pour la prochaine session : commissionner `unit-02` ou `unit-03` dans Home Assistant et valider l'IMU/Boolean State en conditions réelles** (le firmware fonctionne déjà sur ces deux unités, testé par breakpoints matériels — reste à le voir depuis HA). En parallèle, deux points annexes en attente d'intervention physique : (1) bug PMIC I2C général (nPM1300, `-EIO`, présent sur les 3 unités mais n'empêche pas l'IMU de fonctionner sur 2/3 — non bloquant dans l'immédiat, voir `firmware/apps/lock/KNOWN-ISSUES.md`) ; (2) soudure de l'antenne NFC pour tester le commissioning NFC déjà fonctionnel côté firmware. Sécuriser aussi les factory data avant de dupliquer le firmware sur les ~20 unités (voir avertissement sécurité ci-dessous). Priorité 1 (commissioning, sur `unit-01`) reste acquise : trois causes distinctes le bloquaient, toutes corrigées — deux bugs firmware (génération des factory data du SDK, Kconfig `CHIP_ENABLE_PAIRING_AUTOSTART=n` par défaut) et un réglage côté commissioner (« Enable test-net DCL usage » dans Home Assistant, nécessaire tant que le firmware utilise les certificats de développement du SDK) — voir [firmware/apps/lock/KNOWN-ISSUES.md](firmware/apps/lock/KNOWN-ISSUES.md). Depuis la Priorité 2 (IMU 6 axes), l'app Matter Door Lock est forkée directement dans `firmware/apps/lock/` (build sur ce dossier, plus sur le SDK) — voir la commande de build à jour et le détail dans [firmware/apps/lock/README.md](firmware/apps/lock/README.md). Ne pas oublier `python3 firmware/apps/lock/fix_factory_data.py <build_dir>` avant `west flash`.

**⚠️ Avant de dupliquer sur les ~20 unités** : les factory data générées par le build (passcode, discriminator, salt SPAKE2) sont actuellement **identiques à chaque build** au lieu d'être uniques par unité — à corriger avant tout déploiement réel, voir `firmware/apps/lock/KNOWN-ISSUES.md`.

## Suivi des ~20 unités

Chaque unité physique a sa propre fiche dans `devices/unit-XX/` : firmware flashé, calibration IMU, MAC/EUI Thread, historique de commissioning, notes. Voir [devices/README.md](devices/README.md).
