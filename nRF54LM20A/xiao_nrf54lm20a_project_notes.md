# XIAO nRF54LM20A → BLE IMU → Home Assistant

Objectif : des XIAO nRF54LM20A (Sense), un par porte/étage, diffusent leurs
données IMU en BLE (format BTHome v2). Des proxys BLE (ESPHome, un par étage)
relaient les annonces vers Home Assistant, qui tourne dans une VM VMware
Fusion (192.168.1.10) sur le Mac Mini, via passthrough Bluetooth du Mac.

**Configuration technique actuelle (firmware, architecture, mesures) :**
voir `Configuration-nRF54LM20A-System-ON-IDLE.md` — document de référence
unique pour ce qui fonctionne aujourd'hui, y compris le tableau de
déploiement des unités. Ce présent document ne couvre que
l'infrastructure et les procédures qui ne changent pas selon la version
du firmware (compilation, flash, proxy BLE, pinout, historique du projet
complet dans `archive/`).

## Environnement

| Composant | Emplacement |
|---|---|
| nRF Connect SDK v3.4.0 (workspace west) | `C:\ncs\v3.4.0` |
| Toolchain (compilateur, west, cmake, ninja) | `C:\ncs\toolchains\dcbdc366a1` |
| OpenOCD (xPack v0.12.0-7) | `C:\ncs\tools\xpack-openocd-0.12.0-7` |
| Board files Seeed (cloné, non inclus dans NCS 3.4.0) | `C:\ncs\vendor\platform-seeedboards` |
| Firmware actuel | `C:\ncs\projects\nRF54LM20A\xiao_door_sensor` |
| Autres firmwares (référence/étapes de validation, archivés) | `archive/projets-test/` |
| Banc de test BLE Extended Advertising (ESP32-S3, console only) | `C:\ncs\projects\esp32_ext_scan_bench` |
| ESP-IDF v6.0.2 autonome (nécessaire pour le banc ci-dessus) | `C:\esp\v6.0.2\esp-idf` |
| Proxy BLE temporaire (ESPHome) | `C:\ncs\vendor\esphome_proxy` |
| VS Code + extensions Nordic | nRF Connect, nRF DeviceTree, nRF Kconfig, nRF Terminal |

Cible de build : `xiao_nrf54lm20a/nrf54lm20a/cpuapp`

**Disposition physique des tests** : XIAO, proxy BLE ESPHome et VM Home
Assistant sont tous à moins d'un mètre les uns des autres. Toute cause
liée à la distance/portée radio est donc exclue pour le diagnostic d'un
bug — voir `C:\ncs\CLAUDE.md` § « Chercher la cause dans le code, jamais
dans l'environnement ».

La carte XIAO nRF54LM20A n'est pas incluse nativement dans ce checkout NCS
3.4.0 (sortie trop récente) — les fichiers de carte viennent de
`git clone https://github.com/Seeed-Studio/platform-seeedboards.git`
(déjà cloné dans `vendor/`).

## Compiler / flasher / vérifier

Commandes exactes à jour dans `Configuration-nRF54LM20A-System-ON-IDLE.md`
§ 4. Rappels valables quelle que soit la version du firmware :

- Toujours vérifier avec `verify_image` (jamais `dump_image`+`cmp` : la
  RRAM ne s'efface pas avant écriture, ce qui produit de fausses
  corruptions sur des zones de padding jamais exécutées par le fichier
  courant).
- `vid_pid 0x2886 0x0068` nécessaire (VID Seeed absent de la liste par
  défaut d'OpenOCD).
- Pont CMSIS-DAP (SAMD11) parfois intermittent (`unable to find a
  matching CMSIS-DAP device`) — relancer la même commande suffit
  systématiquement, jusqu'à 5 essais, pas besoin de débrancher/rebrancher.
- **Après un flash, débrancher puis rebrancher complètement l'USB-C**
  avant de tester le comportement réel — tant qu'une session OpenOCD a
  touché la carte, le SoC reste en « Debug Interface mode » (datasheet
  nRF54LM20A §9.3), qui émule le System OFF au lieu de l'appliquer
  réellement.
- **PPK2 et USB-C ne sont jamais connectés en même temps** — sinon le SoC
  ne perd jamais réellement l'alimentation, symptôme : courant de repos
  mesuré anormalement élevé, invariant d'un test à l'autre.
- HardFault possible au tout premier flash d'une carte donnée avec une
  nouvelle version de firmware (`pc: 0xeffffffe`) — reflasher
  immédiatement la même commande suffit systématiquement à date.

## Identifier une carte branchée

```powershell
Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -like "*VID_2886*" } | Select-Object FriendlyName, InstanceId, Status
```
→ la ligne `Périphérique USB composite` donne le **numéro de série du pont
USB↔SWD (SAMD11)**, unique et fixe par carte — comparer au tableau de
déploiement dans `Configuration-nRF54LM20A-System-ON-IDLE.md` § 6.
→ la ligne `USB Serial Device (COMx)` donne le port COM à utiliser pour la
lecture série.

Plusieurs cartes branchées en même temps : ajouter
`-c "adapter serial <numéro-de-série-pont>"` (une commande séparée, PAS un
argument de `cmsis-dap vid_pid` ni de `cmsis-dap serial`) pour cibler une
carte précise sans ambiguïté.

## Bug connu de ce snapshot NCS 3.4.0 : `nrf_usbhs_wrapper.c`

Le SoC nRF54LM20A active par défaut un nœud devicetree `usbhs_wrapper` dont
le driver ne compile pas sur ce snapshot précis. Comme aucun firmware ici
n'a besoin de la stack USB device, contournement systématique dans l'overlay
de chaque projet :

```dts
&usbhs { status = "disabled"; };
&usbhs_wrapper { status = "disabled"; };
```

## Pinout debug de la carte (si reflash via sonde externe nécessaire)

Le pont USB↔SWD embarqué (SAMD11) suffit en usage normal — pas besoin de
sonde externe. Si un jour nécessaire :

| Port | Cible | Où |
|---|---|---|
| `SWCLK`/`SWDIO`/`GND`/`RST` | nRF54LM20A (puce applicative) | Petit connecteur 6 pads au dos, au-dessus de l'USB-C |
| `SWCLK2`/`SWDIO2` | SAMD11 (pont USB↔SWD) | Même connecteur (rangée du bas), ou pads `D11`/`D13` sur le bord de la carte |

Les deux ports sont électriquement indépendants. Sources :
[schéma officiel](https://files.seeedstudio.com/wiki/XIAO_nRF54LM20A/getting_start/RES/XIAO_nRF54LM20A_Schematic.pdf),
[pinout xlsx](https://files.seeedstudio.com/wiki/XIAO_nRF54LM20A/getting_start/RES/XIAO_nRF54LM20A_Pin_definition.xlsx).

Firmware de récupération SAMD11 (si le pont embarqué venait à ne plus
répondre du tout) : récupéré chez Seeed, dans
`C:\ncs\vendor\SAMD11_RescueTool\SAMD11_LM20A_DAPLink\`.

## Proxy BLE temporaire (ESPHome, en attendant les ESP32 dédiés)

Un ESP32-U sert de proxy Bluetooth ESPHome vers HA, à décommissionner quand
les ESP32-S3 dédiés (un par étage, commandés) arrivent.

- Config : `C:\ncs\vendor\esphome_proxy\ble-proxy-temp.yaml`
- IP statique : **192.168.1.20**, port API ESPHome 6053
- Board : `esp32dev` / variant `esp32`, framework `esp-idf`

Logs en direct par le réseau :
```powershell
cd C:\ncs\vendor\esphome_proxy
python -m esphome logs ble-proxy-temp.yaml --device 192.168.1.20
```
⚠️ Sans `--device 192.168.1.20`, la commande demande un choix interactif
et échoue silencieusement en usage non-interactif.

Interface web : `http://192.168.1.20` (`web_server: version: 1`).

Compiler (PowerShell uniquement — l'installeur ESP-IDF d'ESPHome refuse
Git Bash/MSYS) :
```powershell
cd C:\ncs\vendor\esphome_proxy
python -m esphome compile ble-proxy-temp.yaml
```

Flasher (l'upload intégré `esphome upload` échoue systématiquement sur ce
clone) :
```powershell
cd C:\ncs\vendor\esphome_proxy\.esphome\build\ble-proxy-temp\build
python -m esptool --chip esp32 -p COM6 -b 115200 --before no-reset --after hard-reset --connect-attempts 60 write-flash -z --flash-size detect 0x0 firmware.factory.bin
```
Entrée en mode flash (auto-reset non fiable sur ce clone) : maintenir
**Boot**, appuyer/relâcher **EN**, garder **Boot** ~1s de plus puis
relâcher, lancer la commande pendant/juste après.

Mise à jour ultérieure par OTA :
```powershell
python -m esphome upload ble-proxy-temp.yaml --device 192.168.1.20
```

## Banc de test BLE Extended Advertising (Publicité Étendue)

XIAO nRF54LM20A émettant en advertising étendu (`CONFIG_BT_EXT_ADV=y`,
trame unique BTHome jusqu'à 255 octets au lieu de plusieurs trames de
31 octets) reçu par un ESP32-S3 (Autosport Labs ESP32-CAN-X2, firmware
ESP-IDF natif `esp_ble_gap_start_ext_scan()` —
`C:\ncs\projects\esp32_ext_scan_bench`, sortie console uniquement, pas de
Wi-Fi/ESPHome). Réception confirmée et stable lors du dernier test.

**Ce qui manque encore avant un déploiement en production :**
- Un récepteur qui remonte réellement vers HA — voie retenue : passerelle
  MQTT (le firmware décode `0xFCD2` et publie en MQTT Discovery, déjà
  configuré côté HA).
- Seules les familles ESP32-**S3/C3/C6** supportent l'extended scan.
- **En attente des Seeed XIAO ESP32-S3 dédiés** (un par étage, commandés)
  avant de démarrer ce travail — la carte Autosport Labs CAN-X2 reste un
  outil de banc de test, pas un proxy de production.
- Comparaison portée/consommation legacy vs étendu non encore mesurée.

## Architecture cible

- Chaque XIAO diffuse en BLE (BTHome v2, non connectable, adresse fixe)
- Un proxy BLE par étage (ESPHome) relaie vers Home Assistant
- HA (VM VMware Fusion, 192.168.1.10) reçoit via passthrough Bluetooth du Mac
- Aucun appairage, aucune connexion GATT — scale bien à plusieurs XIAO

## Prochaines étapes

1. **Fait le 2026-08-29** : détection de mouvement portée (trames A/C :
   mouvement, orientation pitch/roll/yaw, bouton, tamper=0, vibration=0)
   sur l'architecture System ON IDLE — voir
   `Configuration-nRF54LM20A-System-ON-IDLE.md` § 3.3/§ 5. Unité #01
   flashée et en cours de vérification fonctionnelle dans HA. Chute/choc
   et double-tap restent non implémentés (comme en production, le driver
   LSM6DSL n'expose pas ces événements matériels).
2. Porter le même firmware complet (A/B/C) sur l'unité #02 (encore sur le
   build santé-seule).
3. Re-mesurer la consommation PPK2 avec trafic événementiel réel (le
   repos seul devrait rester proche de ~20-22 µA).
4. Reprendre les tests fonctionnels complets dans Home Assistant pour
   les unités #01/#02 (nouvelle architecture) — au-delà de la simple
   présence des entités (mouvement réel, angle, bouton, IMU brut).
5. Résoudre ou contourner le point bloquant nPM1300 `imu_vdd`/LDO1
   (~250-300 µA, question posée à Nordic) — voir
   `Nordic-Support-Report-XIAO-nRF54LM20A.md`.
6. Déploiement des ~17 XIAO nRF54LM20A restants — en attente de la
   re-mesure de consommation avec trafic événementiel (étape 3) avant
   tout flash de lot.
7. Remplacer le proxy BLE ESPHome temporaire par les ESP32-S3 dédiés une
   fois reçus, puis décommissionner `ble-proxy-temp`.
8. Démarrage nRF52840 — voir `Transition-nRF52840-Sense-Demarrage.md`
   (projet frère, dépôt séparé).

## Référence BTHome v2 — Object IDs (générique, réutilisable quelle que soit la version du firmware)

Format, tailles et facteurs vérifiés dans le code source de `bthome-ble`
(bibliothèque exécutée par l'intégration BTHome de Home Assistant) —
spécification complète : [bthome.io/format](https://bthome.io/format/).

| ID | Grandeur | Format | Taille | Facteur | Unité |
|---|---|---|---|---|---|
| `0x00` | packet id | non signé | 1 | 1 | — |
| `0x01` | battery | non signé | 1 | 1 | % |
| `0x02` | temperature | signé | 2 | 0.01 | °C |
| `0x0C` | voltage | non signé | 2 | 0.001 | V |
| `0x0F` | generic (binaire) | non signé | 1 | — | 0/1 |
| `0x15` | battery low (binaire) | non signé | 1 | — | 0/1 |
| `0x21` | motion (binaire) | non signé | 1 | — | 0/1 |
| `0x2B` | tamper/free-fall (binaire) | non signé | 1 | — | 0/1 |
| `0x2C` | vibration/double-tap (binaire) | non signé | 1 | — | 0/1 |
| `0x3A` | button (événement) | non signé | 1 | — | code événement |
| `0x3F` | rotation | **signé** | 2 | 0.1 | ° |
| `0x51` | acceleration | non signé | 2 | 0.001 | m/s² |
| `0x52` | gyroscope | non signé | 2 | 0.001 | °/s |
| `0x63` | acceleration (signée, par axe) | signé | 4 | 0.000001 | m/s² |

**Règles de décodage à respecter pour toute évolution du firmware :**
- Object ID inconnu → le décodage s'arrête net à cet ID.
- Les IDs doivent apparaître en **ordre croissant** dans une même trame.
- **Déduplication par `packet_id`** : deux trames au même `packet_id`
  reçues à moins de 4s d'intervalle → la seconde est silencieusement
  écartée. Chaque jeu de données distinct doit incrémenter le compteur.
- `device_info` doit valoir `0x44` (v2, clair, trigger-based) sur toutes
  les trames.
- Budget de taille legacy BLE = 31 octets par trame
  (`BT_GAP_ADV_MAX_ADV_DATA_LEN`).

## Historique complet

Toute l'implémentation détaillée de l'ancienne architecture (System OFF +
réveil IMU par interruption matérielle, trames A/B/C complètes,
chute/choc, double-tap, bouton, yaw, budget énergétique désormais
invalide) est conservée dans
`archive/docs-historique/xiao_nrf54lm20a_project_notes-ARCHIVE-2026-08-29.md`.
Cette logique a servi de référence directe pour le portage des trames
A/C sur l'architecture System ON IDLE (étape 1 ci-dessus, fait le
2026-08-29) — le fichier de code source lui-même (`main_full_2026-08-27.c.bak`)
est conservé dans
`archive/xiao_door_sensor-logs-et-backups/reference/`. Ce document
d'historique ne décrit plus le firmware actuellement flashé sur #01/#02.
Le reste de l'historique de tests (tests #1-39) est dans
`archive/docs-historique/XIAO-nRF54LM20A-Solution-System-OFF.md`.
