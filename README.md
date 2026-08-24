# XIAO-Door — Capteur de Porte/Fenêtre BLE (BTHome v2)

Projet de capteur de porte/fenêtre basé sur le **XIAO nRF54LM20A Sense**, diffusant ses données IMU en BLE au format **BTHome v2**, relayées par un Bluetooth Proxy vers Home Assistant. 3 unités déployées, 10 supplémentaires attendues fin septembre 2026.

Spécification technique : [XIAO-nRF54LM20A-BTHome-HA-v5.md](XIAO-nRF54LM20A-BTHome-HA-v5.md)
État complet du projet, procédures de build/flash/vérification : [xiao_nrf54lm20a_project_notes.md](xiao_nrf54lm20a_project_notes.md)

## Structure du dépôt

| Dossier | Contenu |
|---|---|
| `docs/` | Cahier des charges, notes techniques |
| `firmware/apps/xiao_door_sensor/` | Firmware déployé (Zephyr / nRF Connect SDK) |
| `firmware/boards/` | Board support Seeed XIAO nRF54LM20A (vendorisé) |
| `firmware/examples/` | Exemples de référence (Blinky) |
| `hardware/` | Schémas, fichiers KiCad/Gerber |
| `apps/` | Applications compagnon (mobile, scripts) |
| `images/` | Photos, captures d'écran |
| `devices/` | Un dossier par unité physique (voir `devices/README.md`) |

## Avancement

- [x] Environnement de build/flash fonctionnel (OpenOCD, toolchain NCS v3.4.0)
- [x] Firmware BLE BTHome v2 déployé sur 3 unités, intégré dans Home Assistant (System OFF, réveil GPIO/GRTC)
- [ ] Mesure de consommation réelle (PPK II) — voir [PPK-Mesures-Transition.md](PPK-Mesures-Transition.md)
- [ ] Déploiement du lot de 10 unités supplémentaires (fin septembre 2026)
- [ ] Démarrage XIAO nRF52840 Sense — voir [capteur-angle-porte-nRF52840-BTHome.md](capteur-angle-porte-nRF52840-BTHome.md)

**Projet Matter over Thread (Door Lock) abandonné** au profit de cette approche BLE BTHome — conservé dans la branche `archive/matter-lock` pour référence (commissioning validé, historique de debug PMIC/IMU sur 3 unités).

## Suivi des unités

Chaque unité physique a sa propre fiche dans `devices/unit-XX/`. Voir [devices/README.md](devices/README.md).
