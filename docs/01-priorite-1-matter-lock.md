# Priorité 1 — Matter Door Lock sur le réseau Thread existant

Objectif : l'appareil rejoint le réseau Thread/Matter existant et apparaît dans Home Assistant.

**Statut global (16/08/2026) : 🔴 bloqué** — le firmware compile et flashe, mais le stack Matter ne s'initialise jamais (aucun crash, mais aucun Bluetooth non plus). Voir `firmware/apps/lock/KNOWN-ISSUES.md` pour l'investigation complète et la prochaine étape à reprendre.

## Statut détaillé

- [x] Partir de l'échantillon officiel Nordic `samples/matter/lock`
- [x] Adapter le board target pour le XIAO nRF54LM20A Sense (overlay flash externe + table de partitions) — voir `firmware/apps/lock/README.md`
- [x] Sleepy End Device — déjà configuré par défaut dans l'échantillon (MTD + poll period + PM_DEVICE), pas de config supplémentaire nécessaire à ce stade
- [x] Compiler — succès dès la première tentative (FLASH 40.97%, RAM 33.39%)
- [x] Flasher sur unit-01 via OpenOCD
- [x] Bug bloquant #1 identifié et corrigé — MCUboot restait bloqué au démarrage (config flash externe manquante côté MCUboot), voir `firmware/apps/lock/README.md`.
- [ ] 🔴 **Bug bloquant #2, non résolu** — le stack Matter/CHIP (et donc Bluetooth) ne s'initialise jamais, sans crash ni log visible. Cause précisément circonscrite mais pas encore trouvée. Voir `firmware/apps/lock/KNOWN-ISSUES.md` pour le détail complet de l'investigation (méthode GDB, ce qui a été écarté, prochaine étape).
- [ ] Commissionner l'appareil sur le réseau Thread existant (via Home Assistant / app Matter) — bloqué par le bug #2
- [ ] Vérifier dans Home Assistant que l'appareil apparaît correctement (état Locked/Unlocked)

## Build

Voir `firmware/apps/lock/README.md` pour la commande complète, les fichiers spécifiques à la board, et une note de sécurité importante (ne jamais committer les données de commissioning générées à chaque build).

## Commissioning — procédure

1. Récupérer le QR code / code manuel généré au build (`zephyr/factory_data.txt` et `.png` dans le dossier de build — **jamais committés**, propres à chaque flash)
2. Dans Home Assistant : Paramètres → Appareils et services → Ajouter une intégration → Matter → scanner le QR code ou saisir le code manuel
3. Confirmer que l'appareil rejoint le réseau Thread existant (3 OTBR déjà en place — Santuario)
4. Vérifier l'entité `lock.*` créée dans HA, tester Lock/Unlock depuis l'interface

## Notes

- Console série non disponible sur cette carte (sonde CMSIS-DAP sans pont UART) — pas de logs de boot en l'état. À revisiter si du débogage approfondi est nécessaire (adaptateur USB-série externe sur UART20).
- Prochaine unité à commissionner : dupliquer la procédure de `firmware/apps/lock/README.md`, chaque unité génère ses propres données de commissioning à chaque flash.
