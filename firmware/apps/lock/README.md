# Matter Door Lock — XIAO nRF54LM20A

Basé sur l'échantillon officiel Nordic `samples/matter/lock` (NCS v3.2.1), non copié dans ce repo — build directement depuis le SDK installé, avec uniquement nos fichiers spécifiques à la board ici :

- `boards/xiao_nrf54lm20a_nrf54lm20a_cpuapp.overlay` — active la flash externe (py25q64, 8 MB) pour le secondary slot MCUboot / OTA, **côté application**, sur le modèle de l'overlay officiel `nrf54lm20dk_nrf54lm20a_cpuapp` (mx25r64 → py25q64)
- `pm_static_xiao_nrf54lm20a_nrf54lm20a_cpuapp.yml` — table de partitions, copiée de la config officielle DK pour le même SoC (2 MB interne), device externe renommé `PY25Q64HA`
- `sysbuild/mcuboot/boards/xiao_nrf54lm20a_nrf54lm20a_cpuapp.overlay` + `.conf` — **la même activation de flash externe, mais côté image MCUboot**. Indispensable, voir bug critique ci-dessous.

## 🐛 Bug critique corrigé — MCUboot restait bloqué au démarrage

**Symptôme** : le firmware compile et flashe sans erreur, mais l'appareil n'apparaît jamais en Bluetooth, aucune LED ne s'allume, "Unable to Add Accessory" côté app de commissioning — à chaque tentative.

**Cause** : la table de partitions (`pm_static`) assigne le secondary slot MCUboot à la flash externe (`py25q64`), mais MCUboot est buildé par sysbuild comme une **image séparée avec son propre devicetree** — l'overlay activant `py25q64` n'était appliqué qu'à l'application, pas à MCUboot. Résultat : MCUboot essaie d'accéder au secondary slot sur une flash externe qu'il ne sait pas piloter, et reste bloqué indéfiniment **avant même de sauter vers l'application**. Le firmware applicatif ne démarre donc jamais — aucun symptôme classique (pas de crash, pas de hardfault) car MCUboot lui-même ne plante pas, il attend.

**Diagnostic** : confirmé en lisant le Program Counter via OpenOCD après reset (`halt` + `reg pc`) et en résolvant l'adresse avec `addr2line` — le PC restait figé exactement au même endroit à chaque halt, dans le binaire MCUboot (adresse < 0xD000), jamais dans la plage de l'application (> 0xD800). Confirmation supplémentaire : scan BLE actif (Python + `bleak`) ne détectait jamais l'appareil, cohérent avec une application qui ne tourne pas.

**Correction** : ajout des fichiers `sysbuild/mcuboot/boards/...overlay` et `.conf` (repris de l'overlay MCUboot officiel du DK Nordic pour ce SoC), passés au build via `-Dmcuboot_EXTRA_DTC_OVERLAY_FILE=` et `-Dmcuboot_EXTRA_CONF_FILE=`. Après correction, le PC démarre bien dans la plage applicative après reset.

**⚠️ Cette étape sera nécessaire sur chacune des ~20 unités** — c'est un problème de configuration de build, pas un défaut matériel unité par unité. Toujours utiliser la commande de build complète ci-dessous (avec les flags `mcuboot_EXTRA_*`), jamais la version simplifiée sans ces flags.

## Build

```bash
source firmware/build-env.sh

west build -p always -b xiao_nrf54lm20a/nrf54lm20a/cpuapp \
  -d /tmp/build-lock \
  /opt/nordic/ncs/v3.2.1/nrf/samples/matter/lock \
  -- \
  -DBOARD_ROOT=$(pwd)/firmware \
  -DEXTRA_DTC_OVERLAY_FILE=$(pwd)/firmware/apps/lock/boards/xiao_nrf54lm20a_nrf54lm20a_cpuapp.overlay \
  -DPM_STATIC_YML_FILE=$(pwd)/firmware/apps/lock/pm_static_xiao_nrf54lm20a_nrf54lm20a_cpuapp.yml \
  -DEXTRA_CONF_FILE=$(pwd)/firmware/apps/lock/pairing-autostart.conf \
  -Dmcuboot_EXTRA_DTC_OVERLAY_FILE=$(pwd)/firmware/apps/lock/sysbuild/mcuboot/boards/xiao_nrf54lm20a_nrf54lm20a_cpuapp.overlay \
  -Dmcuboot_EXTRA_CONF_FILE=$(pwd)/firmware/apps/lock/sysbuild/mcuboot/boards/xiao_nrf54lm20a_nrf54lm20a_cpuapp.conf

# Étape indispensable — contourne un bug de génération des factory data du SDK
# qui empêche le stack Matter/BLE de démarrer (voir KNOWN-ISSUES.md) :
python3 firmware/apps/lock/fix_factory_data.py /tmp/build-lock

west flash -d /tmp/build-lock --runner openocd
```

Résultat : FLASH 809704 B (40.97%), RAM 174708 B (33.39%).

**⚠️ `-DEXTRA_CONF_FILE=.../pairing-autostart.conf` et le script `fix_factory_data.py` sont indispensables** —
sans eux le stack Matter/Bluetooth ne s'initialise jamais et aucune publicité BLE n'est émise. Voir
[KNOWN-ISSUES.md](KNOWN-ISSUES.md) pour le détail des deux bugs (génération des factory data + Kconfig
NCS `CHIP_ENABLE_PAIRING_AUTOSTART=n` par défaut) et leur diagnostic.

**✅ Commissioning de bout en bout validé** (17/08/2026, Home Assistant + iPhone). Avec les deux fixes
ci-dessus, l'appareil publicise en BLE et le Thread join réussit, mais le commissioning échoue quand même
côté Device Attestation : ce build utilise les certificats DAC/PAI de **développement** fournis par le SDK
Nordic, non reconnus par la politique de confiance par défaut de Home Assistant. Il faut activer **« Enable
test-net DCL usage »** dans les options du Matter Server (Home Assistant → Paramètres → Appareils et
services → Matter → Configurer) avant de commissionner — voir cause n°3 dans
[KNOWN-ISSUES.md](KNOWN-ISSUES.md). Ce réglage se fait côté Home Assistant, aucun rebuild nécessaire.

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

## Console série / débogage

- La sonde de debug CMSIS-DAP intégrée au XIAO **ne fait pas de pont UART** vers `/dev/tty.usbmodem*` — aucun log de boot n'est accessible par ce port.
- RTT (`CONFIG_USE_SEGGER_RTT` + `CONFIG_LOG_BACKEND_RTT` + `CONFIG_RTT_CONSOLE`, voir `debug-rtt.conf`) a été testé mais le control block RTT n'est jamais initialisé en mémoire sur cette carte/config — cause non résolue, non bloquant (le diagnostic du bug MCUboot ci-dessus a été fait par lecture directe du Program Counter via OpenOCD, sans logs).
- Pour du débogage nécessitant de vrais logs Matter/Thread en direct, il faudra câbler un adaptateur USB-série externe sur les pins UART20 (TX/RX) du connecteur XIAO.
