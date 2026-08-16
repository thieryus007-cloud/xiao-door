# Matter Door Lock — XIAO nRF54LM20A

Basé sur l'échantillon officiel Nordic `samples/matter/lock` (NCS v3.2.1), non copié dans ce repo — build directement depuis le SDK installé, avec uniquement nos fichiers spécifiques à la board ici :

- `boards/xiao_nrf54lm20a_nrf54lm20a_cpuapp.overlay` — active la flash externe (py25q64, 8 MB) pour le secondary slot MCUboot / OTA, sur le modèle de l'overlay officiel `nrf54lm20dk_nrf54lm20a_cpuapp` (mx25r64 → py25q64)
- `pm_static_xiao_nrf54lm20a_nrf54lm20a_cpuapp.yml` — table de partitions, copiée de la config officielle DK pour le même SoC (2 MB interne), device externe renommé `PY25Q64HA`

## Build

```bash
source firmware/build-env.sh

west build -p always -b xiao_nrf54lm20a/nrf54lm20a/cpuapp \
  -d /tmp/build-lock \
  /opt/nordic/ncs/v3.2.1/nrf/samples/matter/lock \
  -- \
  -DBOARD_ROOT=$(pwd)/firmware \
  -DEXTRA_DTC_OVERLAY_FILE=$(pwd)/firmware/apps/lock/boards/xiao_nrf54lm20a_nrf54lm20a_cpuapp.overlay \
  -DPM_STATIC_YML_FILE=$(pwd)/firmware/apps/lock/pm_static_xiao_nrf54lm20a_nrf54lm20a_cpuapp.yml

west flash -d /tmp/build-lock --runner openocd
```

Résultat (première compilation, succès sans modification supplémentaire) : FLASH 809704 B (40.97%), RAM 174708 B (33.39%).

## Sleepy End Device — déjà configuré par défaut

L'échantillon Nordic configure déjà le device en **Minimal Thread Device** avec polling périodique et gestion d'énergie active, sans configuration supplémentaire nécessaire pour la Priorité 1 :

- `CONFIG_OPENTHREAD_MTD=y` (leaf/end device, pas de routage Thread)
- `CONFIG_OPENTHREAD_POLL_PERIOD=236000` (poll du parent Thread ~toutes les 236s)
- `CONFIG_PM_DEVICE=y` (suspension des périphériques en veille)

Un vrai mode ICD (Intermittently Connected Device, check-in protocol Matter) n'est pas activé par défaut — à évaluer lors de l'optimisation consommation (voir plan de développement, Phase 5).

## ⚠️ Sécurité — ne jamais committer les données de commissioning

Chaque build génère des **factory data** aléatoires et propres à cette compilation (`zephyr/factory_data.*` dans le dossier de build, hors du repo git car sous `/tmp`) :

- Setup passcode (code manuel) + discriminator → QR code de commissioning
- Clés de Device Attestation

Ces fichiers **ne doivent jamais être commit dans ce repo** (public). Le code de commissioning d'une unité est à usage unique par device physique — le noter uniquement en local (hors git) ou physiquement sur l'unité elle-même, pas dans `devices/unit-XX.md`.

## Console série

La sonde de debug CMSIS-DAP intégrée au XIAO **ne fait pas de pont UART** vers `/dev/tty.usbmodem*` — aucun log de boot n'est accessible par ce port. Pour du débogage nécessitant les logs Matter/Thread en direct, il faudra câbler un adaptateur USB-série externe sur les pins UART20 (TX/RX) du connecteur XIAO, ou utiliser RTT si réactivé (`CONFIG_USE_SEGGER_RTT`, désactivé par défaut dans ce sample pour réduire la taille du firmware).
