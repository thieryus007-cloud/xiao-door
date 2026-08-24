# Unité 01

| Champ | Valeur |
|---|---|
| Numéro d'unité | unit-01 |
| Adresse BLE (fixe) | `D2:3A:F7:B1:E8:18` |
| N° série debug probe | `C5F0E209` (CMSIS-DAP intégrée, VID:PID 0x2886:0x0068) |
| Firmware flashé | `firmware/apps/xiao_door_sensor` (BLE BTHome v2, profil L, System OFF) |
| Emplacement final (porte) | — |
| Statut HA | ✅ Intégré (découverte automatique BTHome) |

## Historique Matter (abandonné)

Cette unité a d'abord servi de banc de test pour un projet Matter over
Thread (commissioning Door Lock validé le 17/08/2026) — abandonné au
profit du BLE BTHome (voir `xiao_nrf54lm20a_project_notes.md`). Détail
conservé dans la branche `archive/matter-lock`.

## Sécurité

Ce dépôt est **public** sur GitHub — ne jamais y noter de secrets
(clés, passcodes, identifiants réseau).
