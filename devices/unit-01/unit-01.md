# Unité 01

| Champ | Valeur |
|---|---|
| Numéro d'unité | unit-01 |
| Date de première mise en service | 2026-08-16 |
| Emplacement final (porte) | — (unité de développement/test) |
| Version firmware flashée | `firmware/apps/lock` (fork NCS v3.2.1 `samples/matter/lock`) + IMU/Boolean State (Priorité 2) + `fix_factory_data.py` — build validé, voir `firmware/apps/lock/KNOWN-ISSUES.md`. Dernier `softwareVersion` Matter connu (avant Priorité 2) : 50462976 ("3.2.1+0") |
| Commit firmware (hash git) | — |
| N° série debug probe | `9C4A557D` (CMSIS-DAP intégrée, VID:PID 0x2886:0x0068) |
| MAC / Extended PAN ID Thread | — (non documenté — device commissionné, non ré-identifié dans la topologie OTBR après coup) |
| Code de commissioning Matter (QR/PIN) | passcode `20202021` / discriminator `0xF00` — ⚠️ valeurs d'exemple SDK non uniques, voir avertissement sécurité dans `KNOWN-ISSUES.md` |
| IMU — calibration (offsets X/Y/Z) | — |
| Angle "ouvert" calibré | — |
| NFC — UID tag(s) associé(s) | — |
| Statut | 🔧 en cours — Étape 0 ✅, Priorité 1 ✅ (commissionnée avec succès dans Home Assistant, nœud Matter `@1:22`, fabric "Santuario", endpoint `DoorLock`), Priorité 2 🟡 en pause (code IMU/Boolean State prêt, bloqué par un bug PMIC I2C matériel/driver, voir `KNOWN-ISSUES.md`) |

## Historique

| Date | Action | Notes |
|---|---|---|
| 2026-08-16 | Réception / premier flash | Blinky flashé avec succès via OpenOCD (runner nrfutil incompatible, sonde CMSIS-DAP non J-Link). AP lock engaged au premier flash, récupéré automatiquement. LED clignote — confirmé visuellement. |
| 2026-08-16 | Tentatives de commissioning Matter (6 essais) | Toutes échouées. Bug MCUboot bloquant trouvé et corrigé en cours de route (voir `firmware/apps/lock/README.md`). Après correction, nouveau bug plus profond découvert : le stack Matter/CHIP ne s'initialise jamais (voir `firmware/apps/lock/KNOWN-ISSUES.md`). Antenne 2.4 GHz connectée pendant la session (n'a pas résolu le problème mais reste utile pour la suite). Puce entièrement effacée (`nrf54l_mass_erase`) à un moment du diagnostic. |
| 2026-08-17 | Diagnostic root-cause + fix (session GDB/OpenOCD) | Découverte que le diagnostic du 16/08 était faussé par des déconnexions GDB silencieuses (lectures mémoire brutes OpenOCD utilisées à la place). Deux bugs firmware réels identifiés et corrigés : génération des factory data du SDK (`fix_factory_data.py`) et Kconfig `CHIP_ENABLE_PAIRING_AUTOSTART=n` par défaut (`pairing-autostart.conf`). Appareil de nouveau visible en BLE (`MatterLock`). |
| 2026-08-17 | Commissioning Matter réussi via Home Assistant + iPhone | Premiers essais échoués (« Unable to Add Accessory ») malgré Thread join réussi — logs Matter Server ont montré un rejet à l'étape Device Attestation (certificats DAC/PAI de développement du SDK non reconnus par la DCL de production). Activation de « Enable test-net DCL usage » côté Matter Server Home Assistant → commissioning réussi immédiatement, sans reflash ni power-cycle. Device commissionné comme nœud `@1:22`, fabric label "Santuario", endpoint 1 type `DoorLock` (0x0a, rev 3). |
| 2026-08-17 | Priorité 2 — IMU/Boolean State, bloqué sur bug PMIC I2C | App forkée dans le repo, `imu_manager.cpp` + cluster Matter Boolean State ajoutés et flashés avec succès (build compile, Lock/Unlock non re-régressé). Home Assistant ne montre aucune nouvelle entity, même en inclinant la carte : diagnostiqué via breakpoints matériels OpenOCD que l'IMU n'est jamais alimenté à cause d'un échec du driver MFD du PMIC nPM1300 (`-EIO` sur sa première écriture I2C, `pmic_i2c` bit-bangé GPIO1.15/16). Trois hypothèses testées et écartées (timing de boot jusqu'à 500ms, pull-up seul, open-drain+pull-up) — cause racine encore non identifiée, voir détail complet dans `firmware/apps/lock/KNOWN-ISSUES.md`. |

## Problèmes rencontrés

- Carte neuve : `AP lock engaged` au premier accès OpenOCD, résolu automatiquement par la procédure de recovery intégrée à OpenOCD (pas d'action manuelle requise, mais efface le firmware existant le cas échéant).
- Voir `firmware/apps/lock/KNOWN-ISSUES.md` pour le détail complet des trois causes du blocage de commissioning (deux bugs firmware + un réglage côté commissioner), toutes résolues — spécifiques au firmware/config, donc à reproduire (fixes + réglage Home Assistant) sur les 19 autres unités.
- 🔴 PMIC (nPM1300) inaccessible en I2C sur cette unité — bloque l'alimentation de l'IMU (Priorité 2). Cause
  racine non identifiée à ce stade (voir `KNOWN-ISSUES.md`). Point important pour la suite : à vérifier si ce
  problème est **spécifique à cette unité** (défaut de soudure/câblage) ou **général au board design** avant
  de dupliquer sur les 2 autres XIAO — un des tout premiers tests à faire sur la 2ᵉ unité.
- ⚠️ Avant de commissionner d'autres unités : les factory data (passcode/discriminator) générées par le build sont actuellement identiques à chaque build, pas uniques par unité — à corriger avant tout déploiement réel (voir `KNOWN-ISSUES.md`).
