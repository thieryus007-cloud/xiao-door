# Unité 03

| Champ | Valeur |
|---|---|
| Numéro d'unité | unit-03 |
| Date de première mise en service | 2026-08-17 |
| Emplacement final (porte) | — (unité de développement/test) |
| Version firmware flashée | `firmware/apps/lock` + `unit-secrets/unit-03.conf` (discriminator/passcode/salt uniques, générés par `generate_unit_secrets.py`) — build dédié `/tmp/build-lock-unit-03` |
| Commit firmware (hash git) | — |
| N° série debug probe | `C5F0E209` (CMSIS-DAP intégrée, VID:PID 0x2886:0x0068) |
| MAC / Extended PAN ID Thread | — |
| Code de commissioning Matter (QR/PIN) | ⚠️ ne pas noter ici (repo public) — **unique à cette unité** depuis le 17/08/2026 (voir note ci-dessous), noté en local uniquement (`firmware/apps/lock/unit-secrets/unit-03.conf`, gitignored) |
| IMU — calibration (offsets X/Y/Z) | — |
| Angle "ouvert" calibré | — |
| NFC — UID tag(s) associé(s) | — |
| Statut | 🔧 en cours — Étape 0 ✅ (premier flash réussi), **IMU fonctionnel** (confirmé par breakpoints matériels OpenOCD : lecture accéléromètre/gyroscope + mise à jour du cluster Boolean State réussies), pas encore commissionnée dans Home Assistant |

## Historique

| Date | Action | Notes |
|---|---|---|
| 2026-08-17 | Premier flash + test comparatif PMIC/IMU (3ᵉ unité, tie-breaker) | Flashée avec le même firmware que `unit-01`/`unit-02`, pour trancher si le problème IMU de `unit-01` était isolé ou général (voir `firmware/apps/lock/KNOWN-ISSUES.md`). `AP lock engaged` au premier accès OpenOCD, récupéré automatiquement. Résultat : comme `unit-02`, le bug PMIC (`-EIO`) est présent mais **l'IMU fonctionne quand même** — confirmé par breakpoints matériels (lecture + mise à jour du cluster Boolean State réussies). Avec ce 3ᵉ test, le score est 2/3 unités avec IMU fonctionnel malgré le bug PMIC général → `unit-01` a très probablement un défaut matériel isolé (soudure/composant), pas un problème de design général. |
| 2026-08-17 | Reflash avec code de commissioning unique | Constat que les 3 unités partageaient le même passcode/discriminator par défaut du SDK (`firmware/apps/lock/KNOWN-ISSUES.md`), risque de sécurité identifié comme à corriger immédiatement vu que le parc final visera jusqu'à 25 unités. Création de `firmware/apps/lock/generate_unit_secrets.py` (génère discriminator/passcode/salt SPAKE2+ aléatoires et uniques, écrit dans `unit-secrets/<unit>.conf`, jamais commit). Rebuild dédié avec `-DEXTRA_CONF_FILE=unit-secrets/unit-03.conf`, reflashé avec succès. |

## Problèmes rencontrés

- Bug PMIC I2C général (voir `firmware/apps/lock/KNOWN-ISSUES.md`) : présent mais n'empêche pas l'IMU de fonctionner sur cette unité, contrairement à `unit-01`.

## ⚠️ Sécurité

Ce dépôt est **public** sur GitHub. Ne jamais y noter : code de commissioning Matter (passcode/QR), clés NFC, ou tout autre secret propre à une unité physique. Garder ces informations en local (hors git) ou physiquement sur l'unité. Voir `firmware/apps/lock/README.md`.
