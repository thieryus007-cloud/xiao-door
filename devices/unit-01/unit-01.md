# Unité 01

| Champ | Valeur |
|---|---|
| Numéro d'unité | unit-01 |
| Date de première mise en service | 2026-08-16 |
| Emplacement final (porte) | — (unité de développement/test) |
| Version firmware flashée | `firmware/examples/blink` (smoke test Étape 0) |
| Commit firmware (hash git) | — (à renseigner après premier commit) |
| N° série debug probe | `9C4A557D` (CMSIS-DAP intégrée, VID:PID 0x2886:0x0068) |
| MAC / Extended PAN ID Thread | — (pas encore commissionnée Matter) |
| Code de commissioning Matter (QR/PIN) | — |
| IMU — calibration (offsets X/Y/Z) | — |
| Angle "ouvert" calibré | — |
| NFC — UID tag(s) associé(s) | — |
| Statut | 🔧 en cours — Étape 0 validée |

## Historique

| Date | Action | Notes |
|---|---|---|
| 2026-08-16 | Réception / premier flash | Blinky flashé avec succès via OpenOCD (runner nrfutil incompatible, sonde CMSIS-DAP non J-Link). AP lock engaged au premier flash, récupéré automatiquement. LED clignote — confirmé visuellement. |

## Problèmes rencontrés

- Carte neuve : `AP lock engaged` au premier accès OpenOCD, résolu automatiquement par la procédure de recovery intégrée à OpenOCD (pas d'action manuelle requise, mais efface le firmware existant le cas échéant).
