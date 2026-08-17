# Matter Door Lock — XIAO nRF54LM20A

Forké depuis l'échantillon officiel Nordic `samples/matter/lock` (NCS v3.2.1) **directement dans ce repo**
(`CMakeLists.txt`, `Kconfig*`, `VERSION`, `sysbuild.conf`, `prj.conf`, `src/`) — nécessaire depuis la
Priorité 2 pour pouvoir ajouter du code applicatif (lecture IMU) et modifier le modèle de données Matter
(nouveau cluster). Avant la Priorité 2, le build pointait directement vers le SDK installé sans copie
locale ; voir l'historique git si besoin de comparer.

Fichiers spécifiques à la board/l'app, en plus du fork :

- `boards/xiao_nrf54lm20a_nrf54lm20a_cpuapp.overlay` — active la flash externe (py25q64, 8 MB) pour le secondary slot MCUboot / OTA, **côté application**, sur le modèle de l'overlay officiel `nrf54lm20dk_nrf54lm20a_cpuapp` (mx25r64 → py25q64) ; active aussi `regulator-boot-on` sur `imu_vdd` (alimentation IMU, voir Priorité 2 ci-dessous)
- `pm_static_xiao_nrf54lm20a_nrf54lm20a_cpuapp.yml` — table de partitions, copiée de la config officielle DK pour le même SoC (2 MB interne), device externe renommé `PY25Q64HA`
- `mcuboot-overlay/xiao_nrf54lm20a_nrf54lm20a_cpuapp.overlay` + `.conf` — **la même activation de flash externe, mais côté image MCUboot**. Indispensable, voir bug critique ci-dessous. (Renommé depuis `sysbuild/mcuboot/boards/` lors du fork Priorité 2 — ce chemin précis est une convention sysbuild réservée à l'auto-découverte de config par image, qui entrait en conflit avec notre usage en `-Dmcuboot_EXTRA_*_FILE=` explicite.)

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
  firmware/apps/lock \
  -- \
  -DBOARD_ROOT=$(pwd)/firmware \
  -DEXTRA_DTC_OVERLAY_FILE=$(pwd)/firmware/apps/lock/boards/xiao_nrf54lm20a_nrf54lm20a_cpuapp.overlay \
  -DPM_STATIC_YML_FILE=$(pwd)/firmware/apps/lock/pm_static_xiao_nrf54lm20a_nrf54lm20a_cpuapp.yml \
  -Dmcuboot_EXTRA_DTC_OVERLAY_FILE=$(pwd)/firmware/apps/lock/mcuboot-overlay/xiao_nrf54lm20a_nrf54lm20a_cpuapp.overlay \
  -Dmcuboot_EXTRA_CONF_FILE=$(pwd)/firmware/apps/lock/mcuboot-overlay/xiao_nrf54lm20a_nrf54lm20a_cpuapp.conf

# Étape indispensable — contourne un bug de génération des factory data du SDK
# qui empêche le stack Matter/BLE de démarrer (voir KNOWN-ISSUES.md). Utiliser le
# python de la toolchain NCS (celui de $PATH n'a pas forcément cbor2 installé) :
python3 firmware/apps/lock/fix_factory_data.py /tmp/build-lock

west flash -d /tmp/build-lock --runner openocd
```

Résultat (17/08/2026, avec IMU + cluster Boolean State Priorité 2) : FLASH 816152 B (41.30%), RAM 174900 B
(33.42%) — contre 809704 B (40.97%) / 174708 B (33.39%) pour la Priorité 1 seule.

**⚠️ Le script `fix_factory_data.py` est indispensable** — sans lui le stack Matter/Bluetooth ne s'initialise
jamais et aucune publicité BLE n'est émise. `CONFIG_CHIP_ENABLE_PAIRING_AUTOSTART=y` (ex-`pairing-autostart.conf`,
fusionné dans `prj.conf` depuis le fork) est nécessaire pour la même raison. Voir [KNOWN-ISSUES.md](KNOWN-ISSUES.md)
pour le détail des bugs (génération des factory data + Kconfig NCS `CHIP_ENABLE_PAIRING_AUTOSTART=n` par défaut)
et leur diagnostic.

**✅ Commissioning de bout en bout validé** (17/08/2026, Home Assistant + iPhone). Avec les deux fixes
ci-dessus, l'appareil publicise en BLE et le Thread join réussit, mais le commissioning échoue quand même
côté Device Attestation : ce build utilise les certificats DAC/PAI de **développement** fournis par le SDK
Nordic, non reconnus par la politique de confiance par défaut de Home Assistant. Il faut activer **« Enable
test-net DCL usage »** dans les options du Matter Server (Home Assistant → Paramètres → Appareils et
services → Matter → Configurer) avant de commissionner — voir cause n°3 dans
[KNOWN-ISSUES.md](KNOWN-ISSUES.md). Ce réglage se fait côté Home Assistant, aucun rebuild nécessaire.

## Priorité 2 — IMU 6 axes → cluster Boolean State (porte ouverte/fermée)

**🔴 En pause (17/08/2026)** : le code ci-dessous compile et flashe, mais ne fonctionne pas encore sur
matériel réel — bloqué par un bug distinct (PMIC nPM1300 inaccessible en I2C, empêchant l'alimentation de
l'IMU). Voir « PMIC I2C : bug non résolu » en tête de [KNOWN-ISSUES.md](KNOWN-ISSUES.md) pour le détail complet et les pistes de reprise.

`src/imu_manager.h/.cpp` lit périodiquement (toutes les 2s, `k_timer`) l'accéléromètre/gyroscope
LSM6DS3TR-C (`sensor_sample_fetch`/`sensor_channel_get`, Zephyr sensor API standard, pas de trigger
matériel), calcule un angle d'inclinaison approximatif, et met à jour le cluster Matter **Boolean State**
(0x0045, ajouté sur l'endpoint 1 aux côtés de Door Lock) via `BooleanState::FindClusterOnEndpoint(1)->SetStateValue()`.

Deux points d'attention devicetree/Kconfig résolus (voir commentaires dans les fichiers concernés) :

- L'alimentation de l'IMU (régulateur `imu_vdd`, LDO1 du nPM1300) est **éteinte par défaut** (pas de
  `regulator-boot-on`), mais le driver Zephyr LSM6DSL s'auto-initialise très tôt au boot (avant que du code
  applicatif puisse l'activer) — sans fix, le capteur reste `device_is_ready() == false` en permanence.
  Fix : `regulator-boot-on` ajouté sur `imu_vdd` dans `boards/....overlay` (priorité d'init du régulateur <
  priorité d'init du capteur, donc l'alimentation est prête à temps).
- `CONFIG_LSM6DSL_ACCEL_ODR`/`GYRO_ODR` valent `0` ("runtime") par défaut, ce qui laisse le capteur en
  power-down tant que l'appli n'appelle pas `sensor_attr_set()`. Fixé en prj.conf avec une fréquence fixe
  (index 2 = 26 Hz) pour que le driver configure tout seul une fréquence sensée au boot.

**Étape volontairement non faite ici** : réveil bas-niveau sur mouvement (registres `WAKE_UP_THS`/`MD1_CFG`
du chip, non exposés par le driver Zephyr standard) — pour l'instant le timer tourne en polling classique,
ce qui maintient le CPU éveillé plus souvent que le strict nécessaire pour un Sleepy End Device. À traiter
en étape de suivi une fois ce pipeline de données validé sur matériel réel. Pas de calibration par unité de
l'angle "porte ouverte" non plus (référence 0°, seuil 15° fixes) — prévu en Phase 5 du cahier des charges.

**Régénération du cluster ZAP** : si vous devez modifier à nouveau `src/default_zap/lock.zap`/`.matter`
(ex: ajouter le futur cluster Power Source), régénérer `src/default_zap/zap-generated/` avec :

```bash
source firmware/build-env.sh
export ZAP_INSTALL_PATH=/opt/nordic/ncs/v3.2.1/modules/lib/matter/.zap-install
python3 /opt/nordic/ncs/v3.2.1/modules/lib/matter/scripts/tools/zap/generate.py \
  "$(pwd)/firmware/apps/lock/src/default_zap/lock.zap" \
  -t /opt/nordic/ncs/v3.2.1/modules/lib/matter/src/app/zap-templates/app-templates.json \
  -z /opt/nordic/ncs/v3.2.1/modules/lib/matter/src/app/zap-templates/zcl/zcl.json \
  -o "$(pwd)/firmware/apps/lock/src/default_zap/zap-generated" --keep-output-dir
```

**⚠️ Ne PAS utiliser `west zap-generate`** tel quel sur ce repo : cette commande NCS ne transmet pas le
chemin du zcl.json au script sous-jacent, qui retombe alors sur une résolution par chemin relatif supposant
que le `.zap` vit dans l'arborescence du SDK — invalide depuis que le `.zap` est forké dans ce repo (erreur
`does not exists or is not a file`). Utiliser `generate.py` directement comme ci-dessus. **Ne jamais** passer
le chemin du `.zap` lui-même comme destination `-m` (régénération du `.matter`) — ça écrase le `.zap` avec du
texte IDL (vécu pendant cette session, corrigé en restaurant depuis le sample SDK). Éditer `lock.matter` à la
main reste plus sûr pour de petits ajouts.

## Sleepy End Device — déjà configuré par défaut

L'échantillon Nordic configure déjà le device en **Minimal Thread Device** avec polling périodique et gestion d'énergie active, sans configuration supplémentaire nécessaire pour la Priorité 1 :

- `CONFIG_OPENTHREAD_MTD=y` (leaf/end device, pas de routage Thread)
- `CONFIG_OPENTHREAD_POLL_PERIOD=236000` (poll du parent Thread ~toutes les 236s)
- `CONFIG_PM_DEVICE=y` (suspension des périphériques en veille)

Un vrai mode ICD (Intermittently Connected Device, check-in protocol Matter) n'est pas activé par défaut — à évaluer lors de l'optimisation consommation (voir plan de développement, Phase 5).

## ⚠️ Sécurité — code de commissioning unique par unité, jamais commit

**Important** : par défaut, un build sans `-DEXTRA_CONF_FILE=unit-secrets/<unit>.conf` (voir ci-dessous)
réutilise le passcode/discriminator d'exemple du SDK (`20202021` / `0xF00`), **identiques sur chaque
compilation** — ce n'est pas aléatoire par défaut, contrairement à ce qu'on pourrait supposer. Sur un parc de
plusieurs unités (jusqu'à 25 en configuration finale), partager le même code de commissioning est un vrai
risque de sécurité : la fuite du code d'une porte compromettrait la fenêtre de commissioning de toutes les
autres.

**Avant de flasher une unité destinée à un usage réel** (pas juste un test de dev jetable), générer des
secrets uniques pour elle :

```bash
python3 firmware/apps/lock/generate_unit_secrets.py unit-03
```

Puis ajouter au build (voir commande complète plus haut) :

```bash
-DEXTRA_CONF_FILE=$(pwd)/firmware/apps/lock/unit-secrets/unit-03.conf
```

**Utiliser un dossier de build dédié par unité** (ex: `/tmp/build-lock-unit-03`), pas le dossier générique
`/tmp/build-lock` utilisé pour le développement — sinon on écrase/mélange les factory data entre unités.

Le fichier `unit-secrets/<unit>.conf` (contient le passcode, un vrai secret) est **gitignored** — ne jamais
le commit, ne jamais copier ses valeurs dans un fichier suivi par git (`devices/unit-XX.md` inclus, voir son
avertissement dédié). Le garder localement (utile pour reconstruire le même firmware plus tard pour cette
même unité sans changer son code de pairing) ou le noter physiquement sur l'unité elle-même.

Les certificats de Device Attestation (DAC/PAI), eux, restent partagés entre toutes les unités (ceux fournis
par le SDK pour le développement) — problème distinct du passcode de commissioning, voir « Enable test-net
DCL usage » plus haut.

## Console série / débogage

- La sonde de debug CMSIS-DAP intégrée au XIAO **ne fait pas de pont UART** vers `/dev/tty.usbmodem*` — aucun log de boot n'est accessible par ce port.
- RTT (`CONFIG_USE_SEGGER_RTT` + `CONFIG_LOG_BACKEND_RTT` + `CONFIG_RTT_CONSOLE`, voir `debug-rtt.conf`) a été testé mais le control block RTT n'est jamais initialisé en mémoire sur cette carte/config — cause non résolue, non bloquant (le diagnostic du bug MCUboot ci-dessus a été fait par lecture directe du Program Counter via OpenOCD, sans logs).
- Pour du débogage nécessitant de vrais logs Matter/Thread en direct, il faudra câbler un adaptateur USB-série externe sur les pins UART20 (TX/RX) du connecteur XIAO.
