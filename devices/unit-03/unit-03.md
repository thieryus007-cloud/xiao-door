# Unité 03

| Champ | Valeur |
|---|---|
| Numéro d'unité | unit-03 |
| Date de première mise en service | 2026-08-17 |
| Emplacement final (porte) | — (unité de développement/test) |
| Version firmware flashée | `firmware/apps/lock` (identique à unit-01/unit-02 au moment du flash : fork NCS v3.2.1 `samples/matter/lock` + IMU/Boolean State Priorité 2 + `fix_factory_data.py`) |
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
| 2026-08-17 | Premier flash + test comparatif PMIC/IMU (3ᵉ unité, tie-breaker) | Flashée avec le même firmware que `unit-01`/`unit-02`, pour trancher si le problème IMU de `unit-01` était isolé ou général (voir `firmware/apps/lock/KNOWN-ISSUES.md`). `AP lock engaged` au premier accès OpenOCD, récupéré automatiquement. Résultat : comme `unit-02`, le bug PMIC (`-EIO`) est présent mais **l'IMU fonctionne quand même** — confirmé par breakpoints matériels (lecture + mise à jour du cluster Boolean State réussies). Avec ce 3ᵉ test, le score est 2/3 unités avec IMU fonctionnel malgré le bug PMIC général → `unit-01` a très probablement un défaut matériel isolé (soudure/composant), pas un problème de design général. |

## Problèmes rencontrés

- Bug PMIC I2C général (voir `firmware/apps/lock/KNOWN-ISSUES.md`) : présent mais n'empêche pas l'IMU de fonctionner sur cette unité, contrairement à `unit-01`.

## ⚠️ Sécurité

Ce dépôt est **public** sur GitHub. Ne jamais y noter : code de commissioning Matter (passcode/QR), clés NFC, ou tout autre secret propre à une unité physique. Garder ces informations en local (hors git) ou physiquement sur l'unité. Voir `firmware/apps/lock/README.md`.
