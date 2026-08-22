# XIAO nRF54LM20A → BLE IMU → Home Assistant

Objectif : des XIAO nRF54LM20A (Sense), un par porte/étage, diffusent leurs
données IMU en BLE (format BTHome v2). Des proxys BLE (ESPHome, un par étage)
relaient les annonces vers Home Assistant, qui tourne dans une VM VMware
Fusion (192.168.1.10) sur le Mac Mini, via passthrough Bluetooth du Mac.

**État : 3 XIAO déployés, flashés, vérifiés et intégrés dans Home Assistant**
avec le firmware `xiao_door_sensor` — profil L (trames A/B/C, angles
pitch/roll, événements IMU, batterie), adresse BLE fixe et stable. Aucune
modification de code requise entre exemplaires — l'adresse BLE fixe est
dérivée automatiquement du hardware ID unique de chaque puce. **10 unités
supplémentaires attendues fin septembre 2026** — ce document est la
référence pour reproduire le flash rapidement sur ce lot (voir « Flasher un
nouveau lot » en fin de document).

## Unités déployées

| # | Adresse BLE (fixe) | N° série pont USB↔SWD | Statut HA |
|---|---|---|---|
| 1 | `D2:3A:F7:B1:E8:18` | `C5F0E209` | ✅ Intégré, à jour |
| 2 | `DE:F6:A3:A9:0F:0F` | `9C4A557D` | ✅ Intégré, à jour |
| 3 | `E6:C9:11:CE:6E:C6` | `4587B5C1` | ✅ Intégré, à jour |

Pour ajouter une nouvelle unité dans HA : découverte automatique BTHome
(non chiffré) — apparaît sous l'adresse ci-dessus dès la première annonce
BLE après flash.

## Environnement

| Composant | Emplacement |
|---|---|
| nRF Connect SDK v3.4.0 (workspace west) | `C:\ncs\v3.4.0` |
| Toolchain (compilateur, west, cmake, ninja) | `C:\ncs\toolchains\dcbdc366a1` |
| OpenOCD (xPack v0.12.0-7) | `C:\ncs\tools\xpack-openocd-0.12.0-7` |
| Board files Seeed (cloné, non inclus dans NCS 3.4.0) | `C:\ncs\vendor\platform-seeedboards` |
| Firmware final déployé | `C:\ncs\projects\xiao_door_sensor` |
| Autres firmwares (référence/étapes de validation) | `xiao_imu_test`, `xiao_ble_imu` |
| Proxy BLE temporaire (ESPHome) | `C:\ncs\vendor\esphome_proxy` |
| VS Code + extensions Nordic | nRF Connect, nRF DeviceTree, nRF Kconfig, nRF Terminal |

Cible de build : `xiao_nrf54lm20a/nrf54lm20a/cpuapp`

La carte XIAO nRF54LM20A n'est pas incluse nativement dans ce checkout NCS
3.4.0 (sortie trop récente) — les fichiers de carte viennent de
`git clone https://github.com/Seeed-Studio/platform-seeedboards.git`
(déjà cloné dans `vendor/`).

## Procédure — compiler

```bash
export TCROOT="/c/ncs/toolchains/dcbdc366a1"
export PATH="$TCROOT/mingw64/bin:$TCROOT/bin:$TCROOT/opt/bin:$TCROOT/opt/bin/Scripts:$TCROOT/nrfutil/bin:$TCROOT/opt/zephyr-sdk/gnu/arm-zephyr-eabi/bin:$PATH"
export PYTHONPATH="C:/ncs/toolchains/dcbdc366a1/opt/bin;C:/ncs/toolchains/dcbdc366a1/opt/bin/Lib;C:/ncs/toolchains/dcbdc366a1/opt/bin/Lib/site-packages"
export NRFUTIL_HOME="C:/ncs/toolchains/dcbdc366a1/nrfutil/home"
export ZEPHYR_TOOLCHAIN_VARIANT="zephyr"
export ZEPHYR_SDK_INSTALL_DIR="C:/ncs/toolchains/dcbdc366a1/opt/zephyr-sdk"
export ZEPHYR_BASE="C:/ncs/v3.4.0/zephyr"

cd "C:/ncs/projects/xiao_door_sensor"
west build -b xiao_nrf54lm20a/nrf54lm20a/cpuapp -d build --pristine \
  -- -DBOARD_ROOT="C:/ncs/vendor/platform-seeedboards/zephyr"
```

⚠️ Sous Git Bash, le `PATH` doit utiliser des chemins `/c/...` (pas `C:\...`)
— `:` sépare les entrées de PATH, un chemin Windows le casse. `PYTHONPATH`
reste au format Windows (`;`).

## Procédure — flasher (XIAO branché en USB-C, pont SAMD11 embarqué)

```bash
export PATH="C:/ncs/tools/xpack-openocd-0.12.0-7/bin:$PATH"
BOARD_DIR="C:/ncs/vendor/platform-seeedboards/zephyr/boards/arm/xiao_nrf54lm20a"
HEX="C:/ncs/projects/xiao_door_sensor/build/xiao_door_sensor/zephyr/zephyr.hex"

openocd -s "$BOARD_DIR/support" -f "$BOARD_DIR/support/openocd.cfg" \
  -c "cmsis-dap vid_pid 0x2886 0x0068" \
  -c "adapter speed 500" \
  -c "init" -c "reset halt" \
  -c "nrf54lm20a-load \"$HEX\"" \
  -c "reset" -c "exit"
```

⚠️ **Ne pas utiliser la commande générique `program <fichier> verify reset
exit`** — le driver de flash générique qu'utilise OpenOCD pour ce chip lit
des registres qui n'existent pas sur la nRF54LM20A et échoue toujours. La
procédure `nrf54lm20a-load` ci-dessus (fournie par Seeed dans
`support/openocd.cfg`) écrit directement en RRAM et fonctionne de façon
fiable. Pas de message "Verified OK" en cas de succès — c'est normal, la
vraie confirmation vient de l'étape suivante.

`vid_pid 0x2886 0x0068` est nécessaire (VID Seeed absent de la liste par
défaut d'OpenOCD).

⚠️ **Le pont CMSIS-DAP (SAMD11) de ces cartes est intermittent** — l'erreur
`Error: unable to find a matching CMSIS-DAP device` apparaît régulièrement au
premier essai (vu sur les 3 unités flashées à ce jour, jusqu'à 5 essais
nécessaires sur l'une d'elles). Ce n'est pas un signe de carte défectueuse :
relancer la commande `openocd` ci-dessus à l'identique jusqu'à 5 fois résout
systématiquement le problème. Pas besoin de débrancher/rebrancher entre les
essais.

## Identifier une carte branchée (adresse BLE et/ou numéro d'unité)

Utile dès qu'on manipule plusieurs cartes dans la même session — le port
COM attribué change d'un branchement à l'autre, mais deux identifiants
matériels restent fixes et permettent de savoir quelle carte physique est
branchée, avant d'agir dessus :

```powershell
Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -like "*VID_2886*" } | Select-Object FriendlyName, InstanceId, Status
```
→ la ligne `Périphérique USB composite` donne le **numéro de série du pont
USB↔SWD (SAMD11)**, unique et fixe par carte (ex. `4587B5C1`) — comparer au
tableau « Unités déployées » ci-dessus pour les cartes déjà connues.
→ la ligne `USB Serial Device (COMx)` donne le port COM à utiliser pour la
lecture série.

Pour une carte pas encore répertoriée (nouvelle livraison), lire l'adresse
BLE réellement annoncée au boot (fiable à 100%, contrairement au numéro de
série qui ne dit qu'"une carte déjà vue ou non") :

```bash
BOARD_DIR="C:/ncs/vendor/platform-seeedboards/zephyr/boards/arm/xiao_nrf54lm20a"
openocd -s "$BOARD_DIR/support" -f "$BOARD_DIR/support/openocd.cfg" \
  -c "cmsis-dap vid_pid 0x2886 0x0068" -c "adapter speed 500" \
  -c "init" -c "reset" -c "exit"

python -c "
import serial, time
s = serial.Serial('COM7', 115200, timeout=2)  # adapter le port
end = time.time() + 15
buf = b''
while time.time() < end:
    chunk = s.read(4096)
    if chunk:
        buf += chunk
s.close()
data = buf.decode('utf-8', errors='replace')
for line in data.splitlines():
    if 'Identity' in line:
        print(line)
"
```
→ ligne `bt_hci_core: Identity: XX:XX:XX:XX:XX:XX (random)`.

⚠️ Cette ligne sort dans la **première seconde** du boot — la fenêtre de
capture rate régulièrement (le port se réouvre juste après le reset OpenOCD,
et le démarrage de l'appli Zephyr est souvent plus rapide que l'ouverture du
port série côté PC). Si elle manque, retenter 1-2 fois ; sinon le numéro de
série du pont USB↔SWD suffit largement pour distinguer les cartes entre
elles dans une même session.

## Procédure — vérifier le fonctionnement après flash

Même séquence reset-puis-lecture que ci-dessus, mais en gardant tout le
buffer (pas seulement la ligne `Identity`) :

```bash
python -c "
import serial, time, re
s = serial.Serial('COM7', 115200, timeout=2)  # adapter le port
end = time.time() + 15
buf = b''
while time.time() < end:
    chunk = s.read(4096)
    if chunk:
        buf += chunk
s.close()
data = buf.decode('utf-8', errors='replace')
clean = re.sub(r'\x1b\[[0-9;]*m', '', data)          # retire les codes couleur ANSI
clean = clean.encode('ascii', errors='replace').decode('ascii')  # évite UnicodeEncodeError sur les °C
print(clean)
"
```

⚠️ Sans le nettoyage ASCII ci-dessus, `print()` peut planter avec
`UnicodeEncodeError` sur le caractère `°` (température) dans la console
Windows par défaut (cp1252) — ce n'est pas un problème de la carte, juste
d'affichage.

Doit afficher `Bluetooth initialized -- door sensor, profil L: ...` au
boot, une `trame B` (batterie/santé) quasi immédiate, puis une `trame A
(heartbeat)` dans les 2-4s suivantes (le premier rapport de chaque type part
au démarrage, pas besoin d'attendre les intervalles normaux). Bouger la
carte doit produire des `trame A (motion)` suivies, ~15s après l'arrêt,
d'une `trame A (repos)` ou `(angle)` avec `motion=0 activity=0`.

## Firmware déployé — `xiao_door_sensor` (profil L)

Capteur de porte/fenêtre à bascule, alimentation LiPo, optimisé batterie.
Architecture détaillée dans `Evolution-XIAO-BLE.md` (§3, §4, §7) ; ce qui
suit résume l'état effectivement déployé.

**Trois trames BTHome v2 (non chiffrées), un compteur `packet_id` partagé
mais incrémenté séparément par chaque trame envoyée :**

| Trame | Contenu | Déclencheur |
|---|---|---|
| A | packet_id, activité, mouvement, chute/choc*, double-tap*, bouton*, pitch/roll/yaw* (`0x3F` ×3) | événementiel (mouvement, franchissement d'angle >2°) + heartbeat 60 min si rien ne se passe |
| B | packet_id, batterie %, température interne, tension, batterie faible, charge* + nom de l'appareil | toutes les 15 min ± jitter ±30s |
| C | packet_id, magnitude accel, magnitude gyro | envoyée avec chaque trame A |

*chute/choc, double-tap, bouton, yaw et charge batterie ne sont **pas
câblés** — le driver Zephyr `lsm6dsl` n'expose pas ces événements matériels
via l'API `sensor_trigger` standard (nécessiterait d'écrire
`WAKE_UP_THS`/`TAP_CFG`/`MD1_CFG` en I2C brut) ; le yaw nécessiterait une
intégration gyroscopique dans la durée. Envoyés à 0 en attendant.

**Comportement :**
- Sonde l'accéléromètre toutes les 2s (accéléromètre seul actif en continu)
- Trame A/C sur mouvement (delta > 0,3 m/s²) ou franchissement d'angle
  (>2° vs dernier envoi), minimum 4s entre deux trames A, max 10/min
- Trame de retour au repos (tous les binaires à 0) ~15s après l'arrêt du
  mouvement
- Rafales BLE courtes (700ms, `BT_LE_ADV_NCONN_IDENTITY`), pas de radio
  entre les rafales

**Adresse BLE fixe et stable** : dérivée du hardware ID unique de la puce
(`hwinfo_get_device_id` → `bt_id_create()` avant `bt_enable()`), donc
automatiquement unique par exemplaire sans rien à modifier dans le code.
⚠️ Publier avec `BT_LE_ADV_NCONN_IDENTITY`, pas `BT_LE_ADV_NCONN` — seule la
variante `_IDENTITY` (option `BT_LE_ADV_OPT_USE_IDENTITY`) fait effectivement
diffuser cette adresse fixe sur les ondes ; `BT_LE_ADV_NCONN` seul diffuse
sous une adresse privée aléatoire différente à chaque annonce, même avec une
identité fixée.

**Batterie** : tension lue via le PMIC nPM1300 (`SENSOR_CHAN_GAUGE_VOLTAGE`,
device `pmic_charger`), convertie en pourcentage approximatif via une courbe
LiPo standard non calibrée (fiable pour "batterie faible", pas pour une
capacité restante précise).

**LED de charge (PMIC)** : peut rester allumée en continu quand la carte est
alimentée en USB seul sans batterie connectée — c'est le comportement normal
du PMIC signalant l'absence de batterie à charger, pas un bug. S'éteint
normalement une fois une vraie batterie LiPo branchée. LED0 configurée en
mode `host` (logiciel) dans l'overlay au cas où, LED RGB GPIO forcée éteinte
au boot par précaution — voir `leds_off()` dans `main.c`.

### Autonomie — le gyroscope doit rester coupé hors lecture

Point de vigilance central pour toute évolution future de ce firmware :
**le gyroscope ne doit jamais être laissé actif en continu.** Régler son ODR
(`SENSOR_ATTR_SAMPLING_FREQUENCY` sur `SENSOR_CHAN_GYRO_XYZ`) l'active
en continu jusqu'à ce qu'on le repasse à 0 — sur le LSM6DS3TR-C, ça coûte
~0,9 mA en continu contre ~1,25 µA pour l'accéléromètre seul en low-power.
Sur une LiPo 1500 mAh, la différence est de l'ordre de **~2 mois d'autonomie
(gyro toujours actif) contre 2 à 7 ans (gyro coupé hors lecture)** — un
facteur ~700x, largement dominant devant tout autre poste (radio, PMIC,
etc.). Le firmware actuel active le gyro uniquement le temps d'une lecture
(`set_gyro_power()`/`read_gyro_burst()` dans `main.c`, ~80ms de
stabilisation + lecture), jamais en continu.

**Points de vigilance déduplication BTHome (§2.3c de `Evolution-XIAO-BLE.md`)** :
deux trames différentes envoyées avec le **même** `packet_id` à moins de 4s
d'intervalle sont indiscernables d'une retransmission pour le parseur HA —
la seconde est silencieusement écartée, sans erreur visible. Chaque
fonction d'envoi (trame A, B, C) doit incrémenter le compteur partagé
elle-même ; ne jamais réutiliser le `packet_id` d'une trame précédente pour
une trame de contenu différent.

**Non implémenté (optimisation possible plus tard)** : réveil matériel sur
interruption de l'IMU (le LSM6DS3TR-C le supporte nativement, mais le driver
Zephyr `lsm6dsl` ne l'expose pas via l'API standard — nécessiterait d'écrire
les registres `WAKE_UP_THS`/`WAKE_UP_DUR`/`MD1_CFG` en I2C brut) combiné à un
System OFF du nRF54 entre les réveils. Le design actuel (sondage logiciel
2s + rafale événementielle + gyro coupé hors lecture) est déjà une nette
amélioration par rapport à une diffusion continue, mais reste perfectible
pour une autonomie maximale.

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

Le pont USB↔SWD embarqué (SAMD11) suffit pour tout ce document — pas besoin
de sonde externe en usage normal. Si un jour nécessaire :

| Port | Cible | Où |
|---|---|---|
| `SWCLK`/`SWDIO`/`GND`/`RST` | nRF54LM20A (puce applicative) | Petit connecteur 6 pads au dos, au-dessus de l'USB-C |
| `SWCLK2`/`SWDIO2` | SAMD11 (pont USB↔SWD) | Même connecteur (rangée du bas), ou plus accessible : pads `D11`/`D13` sur le bord de la carte |

Les deux ports sont électriquement indépendants. Sources :
[schéma officiel](https://files.seeedstudio.com/wiki/XIAO_nRF54LM20A/getting_start/RES/XIAO_nRF54LM20A_Schematic.pdf),
[pinout xlsx](https://files.seeedstudio.com/wiki/XIAO_nRF54LM20A/getting_start/RES/XIAO_nRF54LM20A_Pin_definition.xlsx).

Firmware de récupération SAMD11 (si le pont embarqué venait à ne plus
répondre du tout) : récupéré chez Seeed, dans
`C:\ncs\vendor\SAMD11_RescueTool\SAMD11_LM20A_DAPLink\`.

## Proxy BLE temporaire (ESPHome, en attendant les ESP32 dédiés)

Un ESP32-U sert de proxy Bluetooth ESPHome vers HA, à décommissionner quand
les ESP32 dédiés (un par étage) arrivent.

- Config : `C:\ncs\vendor\esphome_proxy\ble-proxy-temp.yaml`
- IP statique : **192.168.1.20**, port API ESPHome 6053
- Board : `esp32dev` / variant `esp32`, framework `esp-idf`

**Compiler** (⚠️ toujours depuis PowerShell — l'installeur ESP-IDF d'ESPHome
refuse Git Bash/MSYS) :

```powershell
cd C:\ncs\vendor\esphome_proxy
python -m esphome compile ble-proxy-temp.yaml
```

**Flasher** — utiliser `esptool` directement sur le binaire "factory" généré
(la commande `esphome upload` intégrée échoue systématiquement sur ce
modèle de carte) :

```powershell
cd C:\ncs\vendor\esphome_proxy\.esphome\build\ble-proxy-temp\build
python -m esptool --chip esp32 -p COM6 -b 115200 --before no-reset --after hard-reset --connect-attempts 60 write-flash -z --flash-size detect 0x0 firmware.factory.bin
```

Procédure d'entrée en mode flash (auto-reset de ce clone non fiable) :
1. Maintenir **Boot** enfoncé
2. Appuyer puis relâcher **EN**
3. Garder **Boot** enfoncé ~1s de plus, puis relâcher
4. Lancer la commande ci-dessus pendant/juste après cette manip
   (`--connect-attempts 60` laisse une large fenêtre, le timing n'a pas
   besoin d'être parfait)

Surveiller les logs/relever l'IP :
```powershell
idf.py --preset board_esp32u_breadboard -p COM6 monitor
```

## Flasher un nouveau lot (checklist rapide)

Pour reproduire rapidement sur les 10 XIAO attendus fin septembre 2026,
avec le firmware déjà validé (pas de modification de code entre exemplaires) :

1. **Compiler une fois** (§ Procédure — compiler ci-dessus) — le même
   `.hex` sert pour tout le lot, `C:/ncs/projects/xiao_door_sensor/build/xiao_door_sensor/zephyr/zephyr.hex`.
2. Pour chaque carte : brancher en USB-C, puis
   `Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -like "*VID_2886*" }`
   pour relever son port COM et son numéro de série de pont (à consigner
   dans le tableau « Unités déployées » en tête de ce document).
3. Flasher (§ Procédure — flasher) — prévoir jusqu'à 5 tentatives à
   l'identique si `unable to find a matching CMSIS-DAP device` apparaît,
   comportement intermittent connu du pont SAMD11, pas un défaut de carte.
4. Vérifier (§ Procédure — vérifier) — confirmer `Bluetooth initialized`,
   trame B puis trame A heartbeat, adresse BLE relevée et notée.
5. Ajouter dans HA (découverte automatique BTHome) et noter l'adresse dans
   le tableau « Unités déployées ».
6. Répéter pour la carte suivante — pas besoin de rebuild entre les cartes,
   seulement entre deux versions différentes du firmware.

## Architecture cible

- Chaque XIAO diffuse en BLE (BTHome v2, non connectable, adresse fixe)
- Un proxy BLE par étage (ESPHome) relaie vers Home Assistant
- HA (VM VMware Fusion, 192.168.1.10) reçoit via passthrough Bluetooth du Mac
- Aucun appairage, aucune connexion GATT — scale bien à plusieurs XIAO

## Prochaines étapes

1. ✅ Lecture IMU validée sur silicium réel
2. ✅ Diffusion BTHome validée (scanner tiers + HA)
3. ✅ Intégration Home Assistant fonctionnelle (accel X/Y/Z, gyroscope, batterie)
4. ✅ 3 XIAO flashés, vérifiés et intégrés dans HA (voir tableau
   « Unités déployées » ci-dessus) — procédure identique et reproductible
   sans modification de code entre exemplaires
5. ✅ Refonte profil L (trames A/B/C, angles, événements) + gyroscope coupé
   hors lecture (autonomie ~2 mois → 2-7 ans théorique) + bugs de
   déduplication BTHome et de chronomètre de repos trouvés en revue et
   corrigés — voir `Evolution-XIAO-BLE.md` pour le détail
6. Mesure d'énergie réelle (PPK II ou nPM1300 EK) pour remplacer l'estimation
   théorique par une mesure sur silicium, avant de commander le lot de 10
7. Remplacer le proxy BLE ESPHome temporaire par les ESP32 dédiés une fois
   reçus (un par étage), puis décommissionner `ble-proxy-temp`
8. **Semaine du 24/08/2026** : démarrage nRF52840 — carte ciblée Seeed XIAO
   nRF52840 Sense (une nRF52840 DK également commandée en secours/débogage).
   Board nativement supporté par NCS 3.4.0, pas de vendor clone requis
   (`C:\ncs\v3.4.0\zephyr\boards\seeed\xiao_ble\`, target
   `xiao_ble/nrf52840/sense`) — voir détails de compatibilité dans la
   conversation, procédure de flash à valider sur le matériel réel
   (bootloader UF2 probable, à confirmer)
9. **Fin septembre 2026** : déploiement des 10 XIAO nRF54LM20A restants —
   voir « Flasher un nouveau lot » ci-dessus pour la checklist
