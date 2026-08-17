# Unité 02

| Champ | Valeur |
|---|---|
| Numéro d'unité | unit-02 |
| Date de première mise en service | 2026-08-17 |
| Emplacement final (porte) | — (unité de développement/test) |
| Version firmware flashée | `firmware/apps/lock` (identique à unit-01 au moment du flash : fork NCS v3.2.1 `samples/matter/lock` + IMU/Boolean State Priorité 2 + `fix_factory_data.py`) |
| Commit firmware (hash git) | — |
| MAC / Extended PAN ID Thread | — (non commissionnée sur le réseau Thread/Matter à ce stade, utilisée uniquement pour un test de comparaison matériel) |
| Code de commissioning Matter (QR/PIN) | ⚠️ ne pas noter ici (repo public) — voir note ci-dessous |
| IMU — calibration (offsets X/Y/Z) | — |
| Angle "ouvert" calibré | — |
| NFC — UID tag(s) associé(s) | — |
| Statut | 🔧 en cours — Étape 0 ✅ (premier flash réussi), **IMU fonctionnel** (confirmé par breakpoints matériels OpenOCD : lecture accéléromètre/gyroscope + mise à jour du cluster Boolean State réussies), pas encore commissionnée dans Home Assistant |

## Historique

| Date | Action | Notes |
|---|---|---|
| 2026-08-17 | Premier flash + test comparatif PMIC/IMU | Flashée avec le firmware déjà validé sur `unit-01` (Priorité 1 + Priorité 2), dans le cadre de l'investigation du bug PMIC I2C (voir `firmware/apps/lock/KNOWN-ISSUES.md`). `AP lock engaged` au premier accès OpenOCD, récupéré automatiquement (normal sur carte neuve). Résultat clé : le bug PMIC (`mfd_npm13xx_init()` échoue avec `-EIO`) est **identique à `unit-01`**, mais contrairement à `unit-01`, l'IMU **fonctionne quand même** — `device_is_ready(lsm6ds3tr_c)` réussit, la lecture périodique et la mise à jour du cluster Boolean State ont été confirmées par breakpoints matériels. |

## Problèmes rencontrés

- Bug PMIC I2C général (voir `firmware/apps/lock/KNOWN-ISSUES.md`) : présent mais n'empêche pas l'IMU de fonctionner sur cette unité, contrairement à `unit-01`.

## ⚠️ Sécurité

Ce dépôt est **public** sur GitHub. Ne jamais y noter : code de commissioning Matter (passcode/QR), clés NFC, ou tout autre secret propre à une unité physique. Garder ces informations en local (hors git) ou physiquement sur l'unité. Voir `firmware/apps/lock/README.md`.
