# XIAO nRF54LM20A → BLE IMU → Home Assistant

Notes consolidées de la session de mise en place (Windows). Objectif final : un ou
plusieurs XIAO nRF54LM20A (Sense) diffusent leurs données IMU en BLE, captées par
des proxys BLE (un par étage) et remontées dans Home Assistant, qui tourne dans une
VM VMware Fusion (192.168.1.10) sur le Mac Mini, via passthrough Bluetooth du Mac.

## Procédure validée (résumé — à suivre telle quelle)

Ceci est la procédure qui **fonctionne réellement**, confirmée sur silicium
(firmware `xiao_imu_test`, IMU lu en direct sur COM3). Ne contient que les
étapes correctes — le détail des essais/erreurs est plus bas dans ce document
pour référence, mais n'est pas nécessaire pour reproduire le résultat.

**Matériel** : XIAO nRF54LM20A branché en USB-C directement sur ce PC (pont
SAMD11 embarqué — pas besoin de sonde externe ni d'ESP32 pour ce chemin).

**1. Compiler le firmware**

```bash
export TCROOT="/c/ncs/toolchains/dcbdc366a1"
export PATH="$TCROOT/mingw64/bin:$TCROOT/bin:$TCROOT/opt/bin:$TCROOT/opt/bin/Scripts:$TCROOT/nrfutil/bin:$TCROOT/opt/zephyr-sdk/gnu/arm-zephyr-eabi/bin:$PATH"
export PYTHONPATH="C:/ncs/toolchains/dcbdc366a1/opt/bin;C:/ncs/toolchains/dcbdc366a1/opt/bin/Lib;C:/ncs/toolchains/dcbdc366a1/opt/bin/Lib/site-packages"
export NRFUTIL_HOME="C:/ncs/toolchains/dcbdc366a1/nrfutil/home"
export ZEPHYR_TOOLCHAIN_VARIANT="zephyr"
export ZEPHYR_SDK_INSTALL_DIR="C:/ncs/toolchains/dcbdc366a1/opt/zephyr-sdk"
export ZEPHYR_BASE="C:/ncs/v3.4.0/zephyr"

cd "C:/ncs/projects/xiao_imu_test"   # ou xiao_ble_imu
west build -b xiao_nrf54lm20a/nrf54lm20a/cpuapp -d build --pristine \
  -- -DBOARD_ROOT="C:/ncs/vendor/platform-seeedboards/zephyr"
```

Produit `build/xiao_imu_test/zephyr/zephyr.hex` (adapter le nom selon le
projet).

**2. Flasher (la commande qui marche — pas `program ... verify reset exit`,**
**voir pourquoi dans la section "Cause racine" plus bas)**

```bash
export PATH="C:/ncs/tools/xpack-openocd-0.12.0-7/bin:$PATH"
BOARD_DIR="C:/ncs/vendor/platform-seeedboards/zephyr/boards/arm/xiao_nrf54lm20a"
HEX="C:/ncs/projects/xiao_imu_test/build/xiao_imu_test/zephyr/zephyr.hex"

openocd -s "$BOARD_DIR/support" -f "$BOARD_DIR/support/openocd.cfg" \
  -c "cmsis-dap vid_pid 0x2886 0x0068" \
  -c "adapter speed 500" \
  -c "init" -c "reset halt" \
  -c "nrf54lm20a-load \"$HEX\"" \
  -c "reset" -c "exit"
```

- `vid_pid 0x2886 0x0068` : obligatoire, le VID Seeed n'est pas reconnu par
  défaut par OpenOCD
- Pas de message de succès explicite si tout va bien (pas de "Verified OK") —
  c'est normal, voir étape 3 pour la vraie confirmation
- Si le pont USB embarqué est instable au moment du flash (voir section
  "Pont SWD embarqué" plus bas), relancer la commande — la connexion elle-même
  fonctionne bien une fois établie, ce n'était pas la vraie cause des échecs
  précédents

**3. Vérifier que ça a marché (obligatoire — la commande de flash ne le confirme pas)**

```python
import serial, time
s = serial.Serial('COM3', 115200, timeout=2)   # adapter le port si différent
time.sleep(0.5)
print(s.read(4096))
s.close()
```

Pour `xiao_imu_test` : doit afficher des lignes `accel x:... gyro x:...` avec
`trig_cnt` qui incrémente. Pour `xiao_ble_imu` : pas de sortie IMU verbeuse,
vérifier plutôt la diffusion BLE (scanner BLE / nRF Connect mobile, chercher
le nom `XIAO-IMU`).

## État au 2026-08-21

- ✅ Toolchain NCS/Zephyr Windows validée de bout en bout (compile proprement)
- ✅ Board port Seeed récupéré et adapté (bug driver contourné)
- ✅ Firmware 1 : lecture IMU brute (test/validation) — compile
- ✅ Firmware 2 : BLE broadcaster BTHome v2 + IMU — compile
- ⏸ **Flash réel bloqué** : le pont de débogage USB embarqué (SAMD11) de la carte
  est instable (apparaît/disparaît, cause matérielle/firmware, pas un souci d'outillage)
- ⏳ En attente : ST-Link V2 + XIAO SAMD21 (sonde externe de secours), en cours de livraison
- ✅ Sonde ESP32-U : flashée et opérationnelle. IP **192.168.1.79**, port **4441**
  (CMSIS-DAP-TCP), GPIO SWCLK=18 SWDIO=19 NRESET=21 — voir
  [Sonde SWD via ESP32](#sonde-swd-via-esp32)
- ✅ **`xiao_imu_test` flashé et validé sur silicium réel**, via le pont USB
  embarqué (pas la sonde ESP32 — voir cause racine ci-dessous). Log IMU en
  direct confirmé sur COM3 : accel/gyro plausibles, `trig_cnt` incrémente en
  continu. **Objectif initial "valider la lecture IMU" atteint.**
- 📝 Tentative sonde ESP32 (WiFi) sur le port nRF54LM20A du XIAO : bloquée
  ("cannot read IDR" reproductible, cause non identifiée avec certitude —
  possiblement un point de contact sur le petit connecteur debug 6 pads,
  jamais confirmé). Non résolu, mis de côté — le pont USB embarqué a fini par
  fonctionner en contournant le vrai problème (voir ci-dessous), donc pas
  cherché plus loin pour l'instant.
- ✅ Firmware de secours SAMD11 (factory DAPLink) récupéré chez Seeed, prêt à
  flasher si le pont embarqué s'avère réparable plutôt que défaillant, voir
  [Piste B — reflasher le SAMD11](#piste-b--reflasher-le-samd11-bridge)
- 🔧 En cours côté utilisateur : câblage physique du XIAO (pads debug)

## Environnement local

| Composant | Emplacement |
|---|---|
| nRF Connect SDK v3.4.0 (workspace west) | `C:\ncs\v3.4.0` |
| Toolchain (compilateur, west, cmake, ninja) | `C:\ncs\toolchains\dcbdc366a1` |
| nrfutil / nrfutil-device | `C:\ncs\toolchains\dcbdc366a1\nrfutil\` |
| OpenOCD (xPack v0.12.0-7, installé manuellement) | `C:\ncs\tools\xpack-openocd-0.12.0-7` |
| Board files Seeed (platform-seeedboards, cloné) | `C:\ncs\vendor\platform-seeedboards` |
| Projet firmware — test IMU brut | `C:\ncs\projects\xiao_imu_test` |
| Projet firmware — BLE BTHome + IMU | `C:\ncs\projects\xiao_ble_imu` |
| VS Code + extensions Nordic | nRF Connect, nRF DeviceTree, nRF Kconfig, nRF Terminal |
| SEGGER J-Link (runtime, pas de sonde physique) | Program Files\SEGGER |

Cible de build : `xiao_nrf54lm20a/nrf54lm20a/cpuapp`

### Pourquoi un board externe (platform-seeedboards) ?

La carte XIAO nRF54LM20A (sortie ~juin 2026) n'est **pas encore incluse** dans le
checkout NCS v3.4.0 local (daté 2026-07-01, vérifié via `git log` dans `zephyr/` —
aucun commit `xiao_nrf54lm20a`). Seeed maintient les fichiers de carte séparément :

```
git clone https://github.com/Seeed-Studio/platform-seeedboards.git
```

Les fichiers utiles sont sous `zephyr/boards/arm/xiao_nrf54lm20a/` (déjà au format
HWMv2 : `board.yml`, `board.cmake`, dts, pinctrl, defconfig — rien à adapter). Le
repo contient aussi des exemples tout faits sous `examples/`, dont
`examples/zephyr-imu/` qui a servi de base au firmware de test IMU.

### Bug connu de ce snapshot NCS 3.4.0 : `nrf_usbhs_wrapper.c`

Le SoC nRF54LM20A active par défaut un nœud devicetree `usbhs_wrapper` (compatible
`nordic,nrf-usbhs-wrapper`) même quand l'USB device n'est pas utilisé. Le driver
correspondant ne compile pas sur ce snapshot précis (confirmé par plusieurs
commits de revert/refix dans l'historique git de `zephyr/` autour de ce fichier).
Comme on n'a pas besoin de la stack USB device pour ces firmwares, la solution est
de désactiver le nœud dans l'overlay de carte de l'application :

```dts
&usbhs {
	status = "disabled";
};

&usbhs_wrapper {
	status = "disabled";
};
```

(déjà inclus dans les deux projets, voir `boards/xiao_nrf54lm20a_nrf54lm20a_cpuapp.overlay`)

## Compiler (commande de référence)

Le `west` de la toolchain n'est pas sur le PATH par défaut — il faut positionner
l'environnement avant chaque session de build (PowerShell/Bash séparés ne
persistent pas les variables d'une commande à l'autre) :

```bash
export TCROOT="/c/ncs/toolchains/dcbdc366a1"
export PATH="$TCROOT/mingw64/bin:$TCROOT/bin:$TCROOT/opt/bin:$TCROOT/opt/bin/Scripts:$TCROOT/nrfutil/bin:$TCROOT/opt/zephyr-sdk/gnu/arm-zephyr-eabi/bin:$PATH"
export PYTHONPATH="C:/ncs/toolchains/dcbdc366a1/opt/bin;C:/ncs/toolchains/dcbdc366a1/opt/bin/Lib;C:/ncs/toolchains/dcbdc366a1/opt/bin/Lib/site-packages"
export NRFUTIL_HOME="C:/ncs/toolchains/dcbdc366a1/nrfutil/home"
export ZEPHYR_TOOLCHAIN_VARIANT="zephyr"
export ZEPHYR_SDK_INSTALL_DIR="C:/ncs/toolchains/dcbdc366a1/opt/zephyr-sdk"
export ZEPHYR_BASE="C:/ncs/v3.4.0/zephyr"

cd "C:/ncs/projects/xiao_ble_imu"   # ou xiao_imu_test
west build -b xiao_nrf54lm20a/nrf54lm20a/cpuapp -d build --pristine \
  -- -DBOARD_ROOT="C:/ncs/vendor/platform-seeedboards/zephyr"
```

⚠️ Sous Git Bash, le `PATH` doit utiliser des chemins style `/c/...` (pas
`C:\...`) car `:` sert de séparateur — un chemin Windows avec `C:` casse le
PATH. `PYTHONPATH`, lui, reste au format Windows (`;`) car c'est Python natif
qui le consomme.

## Firmware 1 — `xiao_imu_test` (validation lecture capteur)

But : confirmer que l'IMU embarqué (LSM6DS3TR-C, driver Zephyr `lsm6dsl`) se lit
correctement, avant de passer au BLE. Repris quasi tel quel de
`platform-seeedboards/examples/zephyr-imu/`.

- Alimente l'IMU via le PMIC nPM1300 (LDO1) + régulateur `power_en` (init différée,
  `zephyr,deferred-init` dans l'overlay)
- Log accel (m/s²) + gyro (rad/s) en polling 1 Hz sur la console UART

## Firmware 2 — `xiao_ble_imu` (diffusion BLE BTHome v2)

But : diffuser l'IMU en BLE au format **BTHome v2**, décodé nativement par
l'intégration BTHome de Home Assistant — aucune app tierce, aucun appairage,
aucune configuration côté HA (chaque XIAO apparaît comme device distinct via son
adresse BLE).

- `CONFIG_BT_BROADCASTER=y` uniquement (pas de rôle peripheral/connectable)
- Toutes les 5 s : lecture accel + gyro, calcul de la **magnitude** du vecteur
  3 axes, mise à jour de l'annonce via `bt_le_adv_update_data()` (pas de coupure
  d'annonce entre deux mesures)
- Payload BTHome (non chiffré, v2) :
  - `0x00` packet id (uint8, incrémenté à chaque cycle — permet de détecter les
    paquets manqués côté HA)
  - `0x51` acceleration magnitude (uint16, facteur 0.001, m/s²)
  - `0x52` gyroscope magnitude (uint16, facteur 0.001, °/s)

**Limite à connaître** : le format BTHome n'a pas d'objet "accélération signée par
axe" — `0x51`/`0x52` sont des magnitudes non signées (norme du vecteur). C'est
suffisant pour détecter du mouvement/de l'activité par pièce, mais pas pour de
l'orientation fine. Si besoin plus tard : service GATT custom (perd la simplicité
"proxy passif") ou objet BTHome `raw` + template de déchiffrement custom côté HA.

Référence format : [bthome.io/format](https://bthome.io/format/), table d'objets
vérifiée contre le décodeur officiel utilisé par HA
([Bluetooth-Devices/bthome-ble](https://github.com/Bluetooth-Devices/bthome-ble)).

## Flashage — état des lieux détaillé

### Pont SWD embarqué (SAMD11) — instable

La carte a un SAMD11 embarqué qui pontage USB↔SWD (comme les autres XIAO nRF54),
identifié par Windows comme composite USB `VID_2886:PID_0068` :
- MI_00 : HID (CMSIS-DAP v1)
- MI_01 : WinUSB, **"CMSIS-DAP v2 Adapter"** — confirmé fonctionnel une fois
  (chaîne d'identification lue : `Seeed Studio XIAO nRF54LM20A CMSIS-DAP`, FW
  2.0.0, série `C5F0E209`)
- MI_02 : `USB Serial Device (COM3)` — port série logs, **reste stable** pendant
  que MI_00/MI_01 disparaissent par intermittence

Diagnostic : l'interface de débogage (MI_01) apparaît/disparaît de façon aléatoire
(confirmé indépendamment de nos outils via polling `Get-PnpDevice` côté Windows,
aucune erreur USB dans les journaux d'événements). Pas résolu par : changement de
câble/port, désactivation de la mise en veille sélective USB, plusieurs
power-cycles complets. Conclusion : probable souci firmware/matériel du pont
SAMD11 sur cet exemplaire (carte sortie il y a ~2 mois, marge d'erreur de jeunesse
de production plausible).

### Outils installés côté PC

- **pyOCD 0.42.0** (fourni avec la toolchain NCS) : ne trouve aucune sonde — le
  backend libusb (`usb.core.NoBackendError`) ne charge pas correctement dans cet
  environnement Python, même après avoir pointé vers la DLL bundlée.
- **OpenOCD xPack v0.12.0-7** installé manuellement dans `C:\ncs\tools\` (pas fourni
  par la toolchain NCS). Fonctionne — a réussi une connexion SWD complète une fois :

```bash
export PATH="C:/ncs/tools/xpack-openocd-0.12.0-7/bin:$PATH"
BOARD_DIR="C:/ncs/vendor/platform-seeedboards/zephyr/boards/arm/xiao_nrf54lm20a"
HEX="C:/ncs/projects/xiao_ble_imu/build/xiao_ble_imu/zephyr/zephyr.hex"
openocd -s "$BOARD_DIR/support" -f "$BOARD_DIR/support/openocd.cfg" \
  -c "cmsis-dap vid_pid 0x2886 0x0068" \
  -c "adapter speed 500" \
  -c "init" -c "reset init" \
  -c "program \"$HEX\" verify reset exit"
```

Le `vid_pid 0x2886 0x0068` est nécessaire car le VID Seeed n'est pas dans la
liste par défaut d'OpenOCD pour son driver `cmsis-dap`. Le script
`support/openocd.cfg` fourni par Seeed gère le déverrouillage CTRL-AP spécifique
au nRF54L (utile si la puce se retrouve verrouillée après un flash raté).

### Cause racine du flash qui échouait tout le temps (et son correctif)

⚠️ **Ne pas utiliser la commande générique `program <fichier> verify reset exit`
sur ce chip.** `support/openocd.cfg` déclare la flash bank avec le driver
`nrf5` (`flash bank $_CHIPNAME.flash nrf5 ...`), écrit pour les anciennes
puces nRF51/52/53. Ce driver essaie de lire des registres FICR à des adresses
qui **n'existent pas** sur la carte mémoire de la nRF54LM20A. Résultat,
reproductible à 300/300 tentatives (deux vitesses SWD différentes testées) :
échec systématique à la même étape, mêmes adresses (`0x00ff0140`, `0x00ff020c`,
`0x10000100`, `0x1000005c`), juste après `** Programming Started **`. Ce
n'était **pas** un problème de connexion instable (le diagnostic initial était
faux) — la connexion SWD elle-même fonctionne bien à 500 kHz.

Seeed fournit dans le même `openocd.cfg` une procédure dédiée qui contourne
ce driver générique et écrit directement en RRAM via `load_image` :

```tcl
proc nrf54lm20a-load {file} {
    mww 0x5004e500 0x101
    load_image $file
    mww 0x5004e008 1
}
```

**Commande qui fonctionne, confirmée sur silicium réel** (remplace `program
... verify reset exit`) :

```bash
export PATH="C:/ncs/tools/xpack-openocd-0.12.0-7/bin:$PATH"
BOARD_DIR="C:/ncs/vendor/platform-seeedboards/zephyr/boards/arm/xiao_nrf54lm20a"
HEX="C:/ncs/projects/xiao_imu_test/build/xiao_imu_test/zephyr/zephyr.hex"
openocd -s "$BOARD_DIR/support" -f "$BOARD_DIR/support/openocd.cfg" \
  -c "cmsis-dap vid_pid 0x2886 0x0068" \
  -c "adapter speed 500" \
  -c "init" -c "reset halt" \
  -c "nrf54lm20a-load \"$HEX\"" \
  -c "reset" -c "exit"
```

Pas de sortie verbeuse en cas de succès (pas de "Verified OK" affiché) —
vérifier via le port série (COM3, 115200) que le firmware tourne réellement,
c'est la seule confirmation fiable.

## Pinout debug exact du XIAO nRF54LM20A (vérifié via schéma officiel)

Deux ports de débogage **physiquement séparés** existent sur la carte (confirmé
par le schéma officiel Seeed et par un guide de récupération officiel — voir
sources ci-dessous, ceci corrige/clarifie la doc textuelle ambiguë du wiki qui
laissait penser à un bus partagé) :

| Port | Cible | Où le trouver |
|---|---|---|
| `SWCLK` / `SWDIO` / `GND` / `RST` | **nRF54LM20A** (puce applicative) | Petit connecteur 6 pads au dos de la carte, juste au-dessus de l'USB-C (2 colonnes × 3 rangées : `SWCLK`/`SWDIO`, `GND`/`RST`, `SWCLK2`/`SWDIO2`) |
| `SWCLK2` / `SWDIO2` | **SAMD11** (pont USB↔SWD) | Même petit connecteur (rangée du bas) **ou**, plus facile d'accès, sur le bord général de la carte : pad **D11** (=SWCLK2) et **D13** (=SWDIO2), avec un `GND` juste à côté — pas besoin de souder sur le petit connecteur pour ce chemin-là |

Sources : [schéma officiel PDF](https://files.seeedstudio.com/wiki/XIAO_nRF54LM20A/getting_start/RES/XIAO_nRF54LM20A_Schematic.pdf),
[feuille de pinout (xlsx)](https://files.seeedstudio.com/wiki/XIAO_nRF54LM20A/getting_start/RES/XIAO_nRF54LM20A_Pin_definition.xlsx),
[guide de récupération SAMD11](https://forum.seeedstudio.com/t/how-to-unbrick-a-dead-xiao-nrf54l15-lm20a/295684)
(diagramme de connexion confirmant `D11→SWCLK2`, `D13→SWDIO2`).

Les deux ports sont indépendants électriquement — pas de risque de conflit entre
un flash applicatif (nRF54LM20A) et une éventuelle récupération du SAMD11, tant
qu'on branche sur le bon jeu de pads.

## Sonde SWD via ESP32

**Choix retenu : [`bkuschak/cmsis_dap_tcp_esp32`](https://github.com/bkuschak/cmsis_dap_tcp_esp32)**
plutôt que `windowsair/wireless-esp8266-dap` — ce dernier nécessite un pilote
USBIP côté Windows (fragile, contraintes de signature de pilote), alors que le
projet retenu utilise directement le backend `cmsis-dap-tcp` déjà présent dans
notre OpenOCD (aucun logiciel supplémentaire côté PC, juste pointer une IP:port).
Seul bémol : pas de préréglage officiel pour l'ESP32 classique (seulement
S3/C3/C6) — préréglage custom créé (le code est du bit-banging GPIO générique,
aucune dépendance matérielle spécifique à une variante).

- Repo cloné : `C:\ncs\vendor\cmsis_dap_tcp_esp32`
- Toolchain : ESP-IDF **v6.0.2** déjà installé sur la machine
  (`C:\esp\v6.0.2\esp-idf`, activation via
  `C:\Espressif\tools\Microsoft.v6.0.2.PowerShell_profile.ps1`) — versions
  exactement alignées avec ce que le projet a été testé.
- Préréglage ajouté : `board_esp32u_breadboard` (`IDF_TARGET=esp32`), fichier
  `sdkconfig.board_esp32u_breadboard`
- **Firmware compilé avec succès** (`cmsis_dap_tcp_esp32.bin`, WiFi `StarTh`
  intégré en dur)

### Câblage ESP32 ↔ XIAO (port nRF54LM20A, flash applicatif)

| Pad XIAO (petit connecteur, dos de carte) | GPIO ESP32-U |
|---|---|
| `SWCLK` | GPIO18 |
| `SWDIO` | GPIO19 |
| `GND` | GND |
| `RST` | GPIO21 |

### Flasher l'ESP32-U lui-même

⚠️ **Correction importante** : ce n'est *pas* un module nu — c'est un kit
devkit-clone (CP2102, boutons **Boot**/**EN** physiques, circuit auto-reset à
transistors visible). Confirmé par photo, silkscreen exact :
- Rangée basse (PCB noir, sous le module) : `3V3 EN VP VN 34 35 32 33 25 26 27 14 12 GND 13 D2 D3 CMD 5V`
- Rangée haute : `GND 23 22 TX RX 21 GND 19 18 5 17 16 4 0 2 15 D1 D0 CLK`

Port COM : **COM6** (Silicon Labs CP210x).

**Le circuit auto-reset de ce clone ne fonctionne pas correctement avec
`esptool --before default-reset`** (reproductible à 100% : "Wrong boot mode
detected (0x13)" — le DTR/RTS pilote GPIO0 dans le mauvais sens). Procédure
qui fonctionne, confirmée : boutons physiques **Boot**/**EN**, en désactivant
le reset automatique d'esptool :

1. Maintenir **Boot** enfoncé
2. Appuyer puis relâcher **EN**
3. Garder **Boot** enfoncé ~1s de plus, puis relâcher
4. Lancer le flash avec `--before no-reset` (sinon esptool réinitialise
   lui-même la carte et écrase l'état mis en place manuellement)

```powershell
cd C:\ncs\vendor\cmsis_dap_tcp_esp32\build\build_esp32u_breadboard
. "C:\Espressif\tools\Microsoft.v6.0.2.PowerShell_profile.ps1"
python -m esptool --chip esp32 -p COM6 -b 115200 --before no-reset --after no-reset --connect-attempts 60 write-flash `
  --flash-mode dio --flash-freq 40m --flash-size 4MB `
  0x1000 bootloader/bootloader.bin 0x8000 partition_table/partition-table.bin 0x10000 cmsis_dap_tcp_esp32.bin
```

(`--connect-attempts 60` donne une large fenêtre pour faire la manip Boot/EN
après avoir lancé la commande — pas besoin que ce soit parfaitement synchronisé.
**`-b 115200`, pas 460800** : à 460800 bauds la liaison sur breadboard produit
des erreurs "Serial data stream stopped: Possible serial noise or corruption"
de façon reproductible — 115200 est fiable.)

⚠️ **Piège vécu** : après avoir édité `sdkconfig.board_esp32u_breadboard`
(ex. pour changer les identifiants WiFi), un `idf.py build` incrémental ne
regénère PAS le `sdkconfig` déjà généré dans `build/build_esp32u_breadboard/` —
il faut supprimer ce fichier (`rm build/build_esp32u_breadboard/sdkconfig`)
puis rebuild pour que les nouveaux defaults soient repris. Sinon le firmware
flashé contient encore les anciennes valeurs (vécu : `myssid`/`mypassword`
placeholder au lieu du vrai SSID, alors que le fichier source était pourtant
correct).

Pour surveiller les logs de boot / relever l'IP DHCP ensuite :
```powershell
idf.py --preset board_esp32u_breadboard -p COM6 monitor
```
(⚠️ toujours passer `--preset board_esp32u_breadboard` explicitement — sans ça
`idf.py` prend le premier preset du fichier, `board_esp32s3_devkit_c1`, qui
n'est pas notre carte)

### Utiliser la sonde une fois en ligne

```bash
export PATH="C:/ncs/tools/xpack-openocd-0.12.0-7/bin:$PATH"
BOARD_DIR="C:/ncs/vendor/platform-seeedboards/zephyr/boards/arm/xiao_nrf54lm20a"
HEX="C:/ncs/projects/xiao_ble_imu/build/xiao_ble_imu/zephyr/zephyr.hex"
ESP32_IP="<ip relevée dans le moniteur série>"

openocd -f interface/cmsis-dap.cfg \
  -c "cmsis-dap backend tcp" -c "cmsis-dap tcp host $ESP32_IP" -c "cmsis-dap tcp port 4441" \
  -s "$BOARD_DIR/support" -f "$BOARD_DIR/support/openocd.cfg" \
  -c "adapter speed 500" \
  -c "init" -c "reset init" \
  -c "program \"$HEX\" verify reset exit"
```

## Piste B — reflasher le SAMD11 (bridge)

Seeed publie un kit de récupération officiel pour ce cas exact (pont SAMD11
instable/muet) : [guide + firmware](https://forum.seeedstudio.com/t/how-to-unbrick-a-dead-xiao-nrf54l15-lm20a/295684).
Récupéré et extrait dans `C:\ncs\vendor\SAMD11_RescueTool\`.

- Firmware factory pour notre carte : `SAMD11_LM20A_DAPLink\samd11_lm20a.bin`
- Cible OpenOCD : `at91samd11d14` (Cortex-M0), config fournie dans
  `XIAO_at91samdXX.cfg`
- Vitesse SWD faible obligatoire : le SAMD11 tourne à 1 MHz juste après reset
  (`adapter speed 400` dans le script d'origine)
- Un script de **backup** existe aussi (`backup.bat`/`.cfg`) — à lancer avant
  tout reflash pour sauvegarder le firmware actuel du pont, au cas où il ne
  serait pas réellement corrompu

### Câblage ESP32 ↔ XIAO (port SAMD11, recovery)

| Pad XIAO | GPIO ESP32-U |
|---|---|
| `SWCLK2` (petit connecteur, rangée du bas — ou pad `D11` sur le bord) | GPIO18 |
| `SWDIO2` (petit connecteur — ou pad `D13` sur le bord) | GPIO19 |
| `GND` | GND |

Pas de ligne RESET nécessaire pour ce chemin (le SAMD11 se réinitialise via sa
propre séquence DSU, gérée par le script OpenOCD).

Commande (une fois la sonde ESP32 en ligne, câblée sur `SWCLK2`/`SWDIO2`) :

```bash
export PATH="C:/ncs/tools/xpack-openocd-0.12.0-7/bin:$PATH"
RESCUE="C:/ncs/vendor/SAMD11_RescueTool/SAMD11_RescueTool/SAMD11_LM20A_DAPLink"
ESP32_IP="<ip relevée dans le moniteur série>"

openocd -f interface/cmsis-dap.cfg \
  -c "cmsis-dap backend tcp" -c "cmsis-dap tcp host $ESP32_IP" -c "cmsis-dap tcp port 4441" \
  -s "$RESCUE" -f "$RESCUE/flash_unprot.cfg"
```

(le fichier `.cfg` d'origine fait tout : init, reset halt, programme
`samd11_lm20a.bin`, reset — pas besoin d'ajouter `program`/`reset` en ligne de
commande, contrairement au flow nRF54LM20A)

## Architecture cible (rappel)

- Chaque XIAO nRF54LM20A diffuse en BLE (BTHome v2, non connectable)
- Un proxy BLE par étage (ESPHome Bluetooth Proxy) relaie les annonces captées
  vers Home Assistant
- HA (VM VMware Fusion, 192.168.1.10 sur le Mac Mini) reçoit via **passthrough
  Bluetooth** du Mac vers la VM (déjà en place/validé côté utilisateur)
- Aucun appairage, aucune connexion GATT — le broadcast passif scale bien à
  plusieurs XIAO sans gestion de connexions individuelles

## Prochaines étapes

1. Câbler le XIAO (en cours côté utilisateur) + flasher l'ESP32-U avec le firmware
   sonde (`idf.py --preset board_esp32u_breadboard -p COMxx flash monitor`),
   relever son IP
2. Tester la sonde sur le port SAMD11 (`SWCLK2`/`SWDIO2`) en premier — si le
   backup/reflash du SAMD11 stabilise le pont embarqué, on peut repasser sur le
   flash USB direct pour la suite et garder l'ESP32 en réserve
3. Sinon, flasher `xiao_imu_test` via l'ESP32 sur le port nRF54LM20A
   (`SWCLK`/`SWDIO`/`RST`) dès qu'une sonde SWD fiable est disponible (ESP32 ou
   ST-Link V2) → valider la lecture IMU sur silicium réel
4. Flasher `xiao_ble_imu` → vérifier la diffusion BTHome via nRF Connect
   (smartphone) ou `bluetoothctl`/un scanner BLE avant même de toucher à HA
5. Configurer l'intégration BTHome dans Home Assistant, vérifier la remontée des
   capteurs `acceleration`/`gyroscope`
6. Dupliquer le firmware sur les prochains XIAO (un par étage), en gardant à
   l'esprit que l'adresse BLE (pas de nom custom nécessaire) suffit à les
   distinguer dans HA
