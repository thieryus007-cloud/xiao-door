# Unité 01

| Champ | Valeur |
|---|---|
| Numéro d'unité | unit-01 |
| Date de première mise en service | 2026-08-16 |
| Emplacement final (porte) | — (unité de développement/test) |
| Version firmware flashée | build de test `/tmp/build-lock-swsplit` (Matter lock, contrôleur BLE logiciel — expérimental, hors repo/perdu au reboot) |
| Commit firmware (hash git) | — |
| N° série debug probe | `9C4A557D` (CMSIS-DAP intégrée, VID:PID 0x2886:0x0068) |
| MAC / Extended PAN ID Thread | — (pas encore commissionnée Matter, bloqué — voir Priorité 1) |
| Code de commissioning Matter (QR/PIN) | — |
| IMU — calibration (offsets X/Y/Z) | — |
| Angle "ouvert" calibré | — |
| NFC — UID tag(s) associé(s) | — |
| Statut | 🔧 en cours — Étape 0 ✅, Priorité 1 🔴 bloquée (voir `firmware/apps/lock/KNOWN-ISSUES.md`) |

## Historique

| Date | Action | Notes |
|---|---|---|
| 2026-08-16 | Réception / premier flash | Blinky flashé avec succès via OpenOCD (runner nrfutil incompatible, sonde CMSIS-DAP non J-Link). AP lock engaged au premier flash, récupéré automatiquement. LED clignote — confirmé visuellement. |
| 2026-08-16 | Tentatives de commissioning Matter (6 essais) | Toutes échouées. Bug MCUboot bloquant trouvé et corrigé en cours de route (voir `firmware/apps/lock/README.md`). Après correction, nouveau bug plus profond découvert : le stack Matter/CHIP ne s'initialise jamais (voir `firmware/apps/lock/KNOWN-ISSUES.md`). Antenne 2.4 GHz connectée pendant la session (n'a pas résolu le problème mais reste utile pour la suite). Puce entièrement effacée (`nrf54l_mass_erase`) à un moment du diagnostic. |

## Problèmes rencontrés

- Carte neuve : `AP lock engaged` au premier accès OpenOCD, résolu automatiquement par la procédure de recovery intégrée à OpenOCD (pas d'action manuelle requise, mais efface le firmware existant le cas échéant).
- Voir `firmware/apps/lock/KNOWN-ISSUES.md` pour le détail du bug de commissioning Matter en cours d'investigation — spécifique au firmware/config, pas à cette unité physique (devrait affecter les 19 autres unités de la même façon jusqu'à résolution).
