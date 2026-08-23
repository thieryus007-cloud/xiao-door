# XIAO nRF54LM20A → BLE IMU → Home Assistant

Objectif : des XIAO nRF54LM20A (Sense), un par porte/étage, diffusent leurs
données IMU en BLE (format BTHome v2). Des proxys BLE (ESPHome, un par étage)
relaient les annonces vers Home Assistant, qui tourne dans une VM VMware
Fusion (192.168.1.10) sur le Mac Mini, via passthrough Bluetooth du Mac.

**État : 3 XIAO déployés, intégrés dans Home Assistant**, firmware
`xiao_door_sensor` — profil L (trames A/B/C BTHome v2, angles pitch/roll,
événements IMU, batterie), adresse BLE fixe dérivée du hardware ID de
chaque puce (aucune modification de code entre exemplaires). Le SoC
fonctionne en **System OFF** entre les événements (réveil matériel sur
mouvement via l'IMU, réveil périodique GRTC pour la trame santé) — voir
§ « Firmware déployé » pour l'architecture complète. Consommation réelle
non mesurée à ce jour (mesure PPK II prévue, voir
`PPK-Mesures-Transition.md`). **10 unités supplémentaires attendues fin
septembre 2026** — ce document est la référence pour reproduire le flash
sur ce lot (§ « Flasher un nouveau lot »).

## Unités déployées

| # | Adresse BLE (fixe) | N° série pont USB↔SWD | Statut HA |
|---|---|---|---|
| 1 | `D2:3A:F7:B1:E8:18` | `C5F0E209` | ✅ Intégré, à jour |
| 2 | `DE:F6:A3:A9:0F:0F` | `9C4A557D` | ✅ Intégré, à jour |
| 3 | `E6:C9:11:CE:6E:C6` | `4587B5C1` | ✅ Intégré, à jour |

Pour ajouter une nouvelle unité dans HA : découverte automatique BTHome
(non chiffré) — apparaît sous l'adresse ci-dessus dès la première annonce
BLE après flash.

## Déployer rapidement une nouvelle unité (résumé)

Pour reproduire sur les 10 XIAO attendus fin septembre 2026, avec le
firmware déjà validé (pas de modification de code entre exemplaires) —
détail de chaque étape dans les sections dédiées plus bas :

1. **Compiler une fois** (§ Procédure — compiler) — le même `.hex` sert
   pour tout le lot :
   `C:/ncs/projects/xiao_door_sensor/build/xiao_door_sensor/zephyr/zephyr.hex`.
2. Pour chaque carte : brancher en USB-C, identifier son numéro de série
   de pont et son port COM (§ Identifier une carte branchée), noter dans
   le tableau « Unités déployées » ci-dessus.
3. Flasher (§ Procédure — flasher) — jusqu'à 5 tentatives si
   `unable to find a matching CMSIS-DAP device` apparaît (pont SAMD11
   intermittent, pas un défaut de carte).
4. **Débrancher/rebrancher complètement l'USB-C** — étape obligatoire
   pour ce firmware (System OFF), sans quoi le test suivant est invalide.
5. Vérifier (§ Procédure — vérifier) — confirmer `Bluetooth initialized`,
   trame B puis trame A heartbeat, adresse BLE relevée et notée, puis
   silence radio jusqu'au prochain événement réel.
6. Ajouter dans HA (découverte automatique BTHome) et noter l'adresse
   dans le tableau « Unités déployées ».
7. Répéter pour la carte suivante — pas de rebuild entre les cartes.

## Environnement

| Composant | Emplacement |
|---|---|
| nRF Connect SDK v3.4.0 (workspace west) | `C:\ncs\v3.4.0` |
| Toolchain (compilateur, west, cmake, ninja) | `C:\ncs\toolchains\dcbdc366a1` |
| OpenOCD (xPack v0.12.0-7) | `C:\ncs\tools\xpack-openocd-0.12.0-7` |
| Board files Seeed (cloné, non inclus dans NCS 3.4.0) | `C:\ncs\vendor\platform-seeedboards` |
| Firmware déployé | `C:\ncs\projects\xiao_door_sensor` |
| Autres firmwares (référence/étapes de validation) | `xiao_imu_test`, `xiao_ble_imu` |
| Banc de test BLE Extended Advertising (ESP32-S3, console only) | `C:\ncs\projects\esp32_ext_scan_bench` |
| ESP-IDF v6.0.2 autonome (nécessaire pour le banc ci-dessus) | `C:\esp\v6.0.2\esp-idf` |
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
`Error: unable to find a matching CMSIS-DAP device` peut apparaître au
premier essai, jusqu'à 5 essais parfois nécessaires. Ce n'est pas un signe
de carte défectueuse : relancer la commande `openocd` ci-dessus à
l'identique résout systématiquement le problème. Pas besoin de
débrancher/rebrancher entre les essais.

⚠️ **Après le flash, débrancher puis rebrancher complètement le câble
USB-C avant de tester le comportement réel** — tant qu'une session OpenOCD
a touché la carte, le SoC reste en « Debug Interface mode » (datasheet
nRF54LM20A §9.3), qui *émule* le System OFF au lieu de l'appliquer
réellement (le firmware semble redémarrer en boucle toutes les 1-2s). Un
simple `reset`/`exit` OpenOCD ne suffit pas à en sortir, seul un cycle
d'alimentation complet le fait.

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

Doit afficher `Bluetooth initialized -- door sensor, profil L (System
OFF): cold_boot=1 gpio_wake=0 reset_cause=...` au boot, une `trame B`
(batterie/santé) quasi immédiate, puis une `trame A (heartbeat)` dans la
seconde qui suit, puis plus rien — le SoC repart en System OFF. **Ne pas
oublier le cycle d'alimentation USB-C après flash** (voir § « Procédure —
flasher ») avant de considérer ce test valide : sans ça, le port série
redémarre en boucle toutes les 1-2s au lieu de rester silencieux.

Une fois hors du mode debug : bouger la carte doit produire un réveil réel
(`gpio_wake=1`, `reset_cause=0x00000080` soit `RESET_LOW_POWER_WAKE`)
suivi d'une `trame A (motion)`, puis ~15s après l'arrêt du mouvement d'une
`trame A (repos)` ou `(angle)` avec `motion=0 activity=0`, puis retour au
silence.

## Firmware déployé — `xiao_door_sensor` (profil L)

Capteur de porte/fenêtre à bascule, alimentation LiPo, optimisé batterie.
Architecture détaillée dans `Evolution-XIAO-BLE.md` (§3, §4, §7) ; ce qui
suit résume l'état effectivement déployé.

**Trois trames BTHome v2 (non chiffrées), un compteur `packet_id` partagé
mais incrémenté séparément par chaque trame envoyée :**

| Trame | Contenu | Déclencheur |
|---|---|---|
| A | packet_id, activité, mouvement, chute/choc*, double-tap*, bouton*, pitch/roll/yaw* (`0x3F` ×3) | événementiel (mouvement, franchissement d'angle >2°) + heartbeat 60 min si rien ne se passe |
| B | packet_id, batterie %, température interne, tension, batterie faible + nom de l'appareil | toutes les 15 min ± jitter ±30s |
| C | packet_id, magnitude accel, magnitude gyro, accélération signée 3 axes (X/Y/Z) | envoyée avec chaque trame A |

*chute/choc, double-tap, bouton et yaw ne sont **pas implémentés** — le
driver Zephyr `lsm6dsl` n'expose pas les événements matériels de chute
libre/tap de l'IMU via l'API `sensor_trigger` standard ; le bouton
physique de la carte (câblé au niveau matériel) n'est jamais lu par ce
firmware ; le yaw nécessiterait une intégration gyroscopique dans la
durée (le gyroscope lui-même est bien lu, voir trame C). Envoyés à 0.

**Architecture d'alimentation — System OFF hybride** : le SoC dort en
System OFF entre les événements, réveillé soit par l'IMU (interruption
matérielle INT1 sur seuil de mouvement, registres I2C bruts
`WAKE_UP_THS`/`WAKE_UP_DUR`/`MD1_CFG`/`TAP_CFG`, hors API `sensor_trigger`
standard), soit par le GRTC (`z_nrf_grtc_wakeup_prepare()`, échéance
trame santé 15 min ou heartbeat 60 min). L'état qui doit survivre à un
redémarrage complet (packet_id, derniers angles envoyés, échéances GRTC)
passe par `retained_mem`. Une fois réveillé, le SoC sonde l'accéléromètre
toutes les 2s (l'accéléromètre lui-même reste alimenté en continu par le
PMIC, indépendamment de l'état du SoC) pendant une « fenêtre active » :
- Trame A/C sur mouvement (delta > 0,3 m/s²) ou franchissement d'angle
  (>2° vs dernier envoi), minimum 4s entre deux trames A, max 10/min — la
  toute première trame après un réveil sur mouvement part immédiatement
  (le réveil matériel a déjà confirmé le mouvement, pas besoin d'attendre
  un delta logiciel)
- Trame de retour au repos (tous les binaires à 0) ~15s après l'arrêt du
  mouvement
- Rafales BLE courtes (700ms, `BT_LE_ADV_NCONN_IDENTITY`), pas de radio
  entre les rafales
- Retour en System OFF une fois la fenêtre active terminée

Voir l'en-tête de `xiao_door_sensor/src/main.c` pour le détail complet de
l'implémentation.

**Registres IMU (LSM6DS3TR-C) utilisés pour le réveil matériel** — accès
I2C direct sur le bus `i2c30` (adresse `0x6a`), en parallèle du driver
Zephyr `lsm6dsl` (API standard `i2c_dt_spec`, pas de conflit) :

| Registre | Adresse | Bits pertinents | Rôle |
|---|---|---|---|
| `WAKE_UP_THS` | `0x5B` | bit7 `SINGLE_DOUBLE_TAP`, bits5:0 `WK_THS[5:0]` | Seuil de réveil, 1 LSb = FS_XL/2⁶ |
| `WAKE_UP_DUR` | `0x5C` | bit7 `FF_DUR5`, bits6:5 `WAKE_DUR[1:0]`, bit4 `TIMER_HR`, bits3:0 `SLEEP_DUR[3:0]` | Durée de réveil, 1 LSb = 1×ODR_time |
| `MD1_CFG` | `0x5E` | bit5 `INT1_WU` | Routage de l'événement wake-up vers INT1 |
| `TAP_CFG` | `0x58` | bit7 `INTERRUPTS_ENABLE` | Porte globale des interruptions — obligatoire pour que le wake-up fonctionne |
| `CTRL6_C` | `0x15` | bit4 `XL_HM_MODE` | Mode low-power accéléromètre (déjà forcé par le driver `lsm6dsl_init()`) |

Sources : datasheet ST DocID030071 Rev 3 (`docs/LSM6DS3TR-C_datasheet_DocID030071_Rev3.pdf`,
pages 88/90/92/66) et définitions Zephyr `lsm6dsl.h:444-524` (les deux
concordent bit pour bit). Pin INT1 de l'IMU : `irq-gpios = <&gpio0 6
GPIO_ACTIVE_HIGH>` sur le nœud `lsm6ds3tr_c`
(`nrf54lm20a_cpuapp_common.dtsi:204`).

**Réveil périodique GRTC** : le GRTC (Global RTC) du nRF54L est dans un
domaine « always-on » qui bascule sur le quartz basse fréquence (LFXO,
32,768 kHz) et survit au System OFF — API Zephyr
`z_nrf_grtc_wakeup_prepare(wake_time_us)`. Déjà activé sur le devicetree
de la XIAO (`&grtc { status = "okay"; }`,
`nrf54lm20a_cpuapp_common.dtsi:57-62`). Source : datasheet Nordic
4539_001 v1.0 §5.2/§8.11 (`docs/nRF54LM20A_nRF54LM20B_Datasheet_v1.0.pdf`).

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
**le gyroscope ne doit jamais être laissé actif en continu.** Réglé son ODR
(`SENSOR_ATTR_SAMPLING_FREQUENCY` sur `SENSOR_CHAN_GYRO_XYZ`) l'active en
continu jusqu'à ce qu'on le repasse à 0 — sur le LSM6DS3TR-C, ça coûte
~0,9 mA en continu contre ~9 µA pour l'accéléromètre seul en low-power
@12,5 Hz (`LA_IddLM`, datasheet ST DocID030071 Rev 3, Table 4 p. 24) — un
facteur **~100x**, largement dominant devant tout autre poste (radio,
PMIC, etc.). Le firmware active le gyroscope uniquement le temps d'une
lecture (`set_gyro_power()`/`read_gyro_burst()` dans `main.c`, 200ms de
stabilisation avant lecture — nécessaire car le driver Zephyr `lsm6dsl` lit
les registres de sortie sans attendre le drapeau "donnée prête" ; à
12,5 Hz une période ODR complète = 80ms, insuffisant à lui seul), jamais
en continu.

**Points de vigilance déduplication BTHome (§2.3c de `Evolution-XIAO-BLE.md`)** :
deux trames différentes envoyées avec le **même** `packet_id` à moins de 4s
d'intervalle sont indiscernables d'une retransmission pour le parseur HA —
la seconde est silencieusement écartée, sans erreur visible. Chaque
fonction d'envoi (trame A, B, C) doit incrémenter le compteur partagé
elle-même ; ne jamais réutiliser le `packet_id` d'une trame précédente pour
une trame de contenu différent.

### Budget énergétique (calculé, non mesuré)

Consommation réelle jamais mesurée sur silicium (PPK II à venir, voir
`PPK-Mesures-Transition.md`) — ce qui suit est un budget calculé à partir
de datasheets et du comportement du firmware. Chaque ligne est marquée
**VÉRIFIÉ** (chiffre de datasheet lu directement) ou **ESTIMÉ** (calcul,
non mesuré) :

| Poste | Système actif (repos) | Statut | Source |
|---|---|---|---|
| IMU LSM6DS3TR-C, low-power @12,5 Hz | 9 µA (continu — génère l'interruption de réveil) | VÉRIFIÉ | ST DocID030071 Rev 3, Table 4 p.24 |
| SoC nRF54LM20A, System OFF + réveil GRTC | 1,0 µA (`IOFF1`) | VÉRIFIÉ | Nordic 4539_001 v1.0, §11.2.1.1 p.1253 |
| PMIC nPM1300, quiescent | ≥0,8 µA (`IQBAT`, plancher — LDO1/BUCK sous charge réelle non chiffrés séparément) | VÉRIFIÉ comme plancher, incomplet | Nordic 4490_483 v1.1, Table 4 p.16 |
| Boucle logicielle (sondage 2s, fenêtre active) | ~0 µA en moyenne (réveil uniquement sur IMU/GRTC réels) | ESTIMÉ | — |
| Radio BLE (rafales publicitaires, porte au repos) | 0,06 à 6 µA (plage large — temps d'antenne réel vs fenêtre de rafale complète) | ESTIMÉ | calcul ci-dessous |
| **Total (porte immobile)** | **~10,9 à 16,9 µA** (milieu ~13,9 µA) | mixte | — |

Détail du calcul radio (porte au repos) : trame B toutes les 15 min
(96/jour), heartbeat toutes les 60 min (24/jour) + trame C à chaque
heartbeat (24/jour) = 144 rafales/jour, chacune 700ms
(`ADV_BURST_MS`/`ADV_INT`, `main.c`) à ~7 événements/rafale sur 3 canaux.
Borne haute (courant TX `IRADIO_TX0`=5,0 mA appliqué à toute la fenêtre
700ms) ≈ 5,8 µA ; borne basse (temps d'antenne réel, formule BLE 1M PHY)
≈ 0,06 µA — l'écart illustre l'incertitude réelle, aucune des deux bornes
ne compte le surcoût CPU/contrôleur BLE entre événements.

**Autonomie calculée sur 600 mAh** (hypothèse optimiste, 100% de la
capacité nominale utilisable) : ~3,9 à 6,4 ans (milieu ~4,9 ans) —
plausible pour l'objectif 400-600 mAh / 3-4+ ans exprimé, mais reste un
calcul, pas une mesure : les postes ESTIMÉ (radio, boucle logicielle) et
le plancher PMIC incomplet sont les principales sources d'incertitude
que la mesure PPK II devra lever avant toute décision finale sur la
taille de batterie.

## Référence BTHome v2 — Object IDs utilisés

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

`0x16` (battery charging) n'est pas utilisé par ce firmware — le PMIC
nPM1300 expose un statut de charge, mais ce firmware ne le lit pas.

**Règles de décodage à respecter pour toute évolution du firmware :**
- Object ID inconnu → le décodage s'arrête net à cet ID (les mesures
  suivantes dans la trame sont perdues). N'utiliser que des IDs de cette
  table.
- Les IDs doivent apparaître en **ordre croissant** dans une même trame —
  sinon avertissement côté HA, décodage qui continue quand même.
- **Déduplication par `packet_id`** : deux trames au même `packet_id` reçues
  à moins de 4s d'intervalle → la seconde est silencieusement écartée. Au
  delà de 4s, ou avec un `packet_id` différent, toujours acceptée (voir
  aussi la note dans « Autonomie » ci-dessus). Chaque jeu de données
  distinct doit incrémenter le compteur.
- **Mesures multiples du même ID** dans une trame (ex. `0x3F` ×3 pour
  pitch/roll/yaw, `0x63` ×3 pour X/Y/Z) : Home Assistant les nomme `X`,
  `X_2`, `X_3` dans l'ordre d'apparition — garder cet ordre identique à
  chaque trame.
- `device_info` doit valoir `0x44` (v2, clair, **trigger-based**) sur
  toutes les trames — le bit trigger-based indique à HA de garder l'appareil
  "disponible" entre deux émissions espacées, au lieu de le marquer hors
  ligne après quelques minutes sans nouvelle donnée.
- **Budget de taille legacy BLE = 31 octets** (`BT_GAP_ADV_MAX_ADV_DATA_LEN`,
  `zephyr/subsys/bluetooth/host/adv.c`) par trame — la trame C est
  actuellement à exactement 31 octets, **aucune marge** pour un ajout futur
  sans retirer autre chose.

## Entités Home Assistant générées

| Object ID | Entité HA | Nom par défaut | Renommage suggéré |
|---|---|---|---|
| `0x0F` | `binary_sensor` | Generic | Activité |
| `0x21` | `binary_sensor` | Motion | Mouvement |
| `0x2B` | `binary_sensor` | Tamper | Chute / Choc *(non implémenté, toujours 0)* |
| `0x2C` | `binary_sensor` | Vibration | Double-tap *(non implémenté, toujours 0)* |
| `0x3A` | `event` | Button | Bouton *(bouton physique câblé sur la carte, non lu par le firmware, toujours 0)* |
| `0x3F` #1/#2/#3 | `sensor` | Rotation / Rotation 2 / Rotation 3 | Pitch / Roll / Yaw *(yaw non calculé, toujours 0)* |
| `0x01` | `sensor` | Battery | — |
| `0x02` | `sensor` | Temperature | Température interne *(diagnostic)* |
| `0x0C` | `sensor` | Voltage | — |
| `0x15` | `binary_sensor` | Battery | Batterie faible |
| `0x51` | `sensor` | Acceleration | magnitude |
| `0x52` | `sensor` | Gyroscope | Gyroscope |
| `0x63` ×3 (X, Y, Z) | `sensor` | Acceleration / Acceleration 2 / Acceleration 3 | axe X / axe Y / axe Z |

Ordre d'apparition des objets `0x63` dans la trame : X, Y, Z, dans cet
ordre — correspond à « Acceleration » / « Acceleration 2 » /
« Acceleration 3 » dans HA, dans le même ordre (vérifiable par recoupement
indépendant : √(axe X² + axe Y² + axe Z²) ≈ valeur de « magnitude »
affichée séparément au même instant).

Les entités **persistent entre les trames** — HA fusionne les mises à jour
partielles (trame A, B, C reçues séparément) et une entité garde sa
dernière valeur jusqu'à la suivante. Découpage en plusieurs trames
transparent côté interface.

**Calibration d'angle (à faire une fois la position de montage connue)** :
pas encore possible aujourd'hui — la position finale du XIAO sur chaque
porte/fenêtre n'est pas encore déterminée. Une fois montée : définir
l'offset "position fermée = 0°" par device dans un template HA à partir de
`sensor.<nom>_pitch` ou `_roll` (selon l'axe de rotation de l'ouvrant),
et dériver un `binary_sensor` "ouvert/fermé" par seuil d'angle. Monter le
capteur avec l'axe de rotation de l'ouvrant perpendiculaire à la gravité
rend l'angle d'ouverture directement lisible sur pitch ou roll — mesure
absolue, stable, sans dérive.

## Diagnostic

| Symptôme | Cause probable | Action |
|---|---|---|
| Device non découvert dans HA | Flags AD manquants | Vérifier `02 01 06` en tête d'advertising |
| Device dupliqué à chaque reboot | Adresse non fixée | `bt_id_create()` + `BT_LE_ADV_NCONN_IDENTITY`, pas `BT_LE_ADV_NCONN` seul |
| Device `unavailable` entre deux trames | `device_info` à `0x40` au lieu de `0x44` | Bit trigger-based à 1 sur toutes les trames |
| Mesures partielles/manquantes | Object ID inconnu dans la trame | Journal `debug` de `bthome_ble` côté HA : `Invalid Object ID found in payload` |
| Une mise à jour sur deux semble ignorée | Deux trames différentes avec le même `packet_id` à moins de 4s | Chaque fonction d'envoi doit incrémenter le compteur elle-même |
| Pitch/Roll/Yaw permutés | Ordre d'insertion des `0x3F` non constant | Toujours insérer dans le même ordre (pitch, roll, yaw) |
| Accélération toujours positive | `0x51` porte une magnitude, pas une valeur signée | Utiliser `0x63` pour des composantes signées par axe |
| IMU non détectée / lecture invalide | LDO1 du PMIC à 1,8V au lieu de 3,3V, ou driver non activé | Overlay carte (`&pmic { regulators { LDO1 {...} } }`), `CONFIG_LSM6DSL=y` |
| Angles bruités | Pas de moyennage | Moyenner 8-16 échantillons, attendre une stabilisation (‖a‖≈9,81±0,3 m/s² pendant ≥200ms) avant de calculer l'angle |
| Redémarrage en boucle toutes les 1-2s après un flash | Sonde de debug encore "attachée" (Debug Interface mode), System OFF émulé | Débrancher/rebrancher complètement l'USB-C (§ « Procédure — flasher ») |
| Trame BLE ne part pas, `err -22` "Too big advertising data" dans les logs | Payload > 31 octets (limite legacy BLE) | Vérifier `BT_GAP_ADV_MAX_ADV_DATA_LEN` (31) contre la taille réelle de la trame ; retirer/raccourcir un champ |

## Banc de test BLE Extended Advertising (Publicité Étendue)

XIAO nRF54LM20A émettant en advertising étendu (`CONFIG_BT_EXT_ADV=y`,
trame unique BTHome jusqu'à 255 octets au lieu de 3 trames de 31 octets)
reçu par un ESP32-S3 (Autosport Labs ESP32-CAN-X2, firmware ESP-IDF natif
`esp_ble_gap_start_ext_scan()` — `C:\ncs\projects\esp32_ext_scan_bench`,
sortie console uniquement, pas de Wi-Fi/ESPHome). Réception confirmée et
stable : `type ETENDU`, `74 octets`, `UUID 0xFCD2, device info 0x44,
57 octets de mesures`.

**Pourquoi c'est intéressant** : une seule trame de 74 octets au lieu de 3
trames de 31 octets porte l'intégralité des données (mouvement, angles,
batterie, IMU brut) en un seul paquet auto-suffisant — plus besoin de faire
coïncider les compteurs entre plusieurs trames pour reconstituer l'état
complet d'un capteur.

**Ce qui manque encore avant un déploiement en production :**
- Un récepteur qui remonte réellement vers HA (le banc actuel ne fait que
  logger sur console série) — deux voies possibles : composant externe
  ESPHome (scan étendu + `bluetooth_proxy`), ou passerelle MQTT (le firmware
  décode lui-même `0xFCD2` et publie en MQTT Discovery). **MQTT étant déjà
  configuré côté HA, c'est la voie retenue.**
- Seules les familles ESP32-**S3/C3/C6** supportent l'extended scan — pas
  l'ESP32 classique (celui du proxy ESPHome de production actuel).
- **En attente des Seeed XIAO ESP32-S3 dédiés** (un par étage, commandés)
  avant de démarrer ce travail — la carte Autosport Labs CAN-X2 reste un
  outil de banc de test, pas un proxy de production.
- Comparaison portée/consommation legacy vs étendu non encore mesurée
  (nécessite éloignement physique du capteur + PPK II).

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

## Architecture cible

- Chaque XIAO diffuse en BLE (BTHome v2, non connectable, adresse fixe)
- Un proxy BLE par étage (ESPHome) relaie vers Home Assistant
- HA (VM VMware Fusion, 192.168.1.10) reçoit via passthrough Bluetooth du Mac
- Aucun appairage, aucune connexion GATT — scale bien à plusieurs XIAO

## Prochaines étapes

1. Mesure d'énergie réelle (PPK II ou nPM1300 EK) pour remplacer
   l'estimation théorique par une mesure sur silicium, avant de commander
   le lot de 10 — voir `PPK-Mesures-Transition.md`. Objectif associé :
   passer à une batterie 400-600 mAh (au lieu de 1500 mAh) pour 3-4+ ans
   d'autonomie, décision à prendre une fois la mesure disponible (budget
   calculé, pas mesuré, § « Budget énergétique (calculé, non mesuré) »
   plus haut).
2. Remplacer le proxy BLE ESPHome temporaire par les ESP32 dédiés une fois
   reçus (un par étage), puis décommissionner `ble-proxy-temp`
3. Démarrage nRF52840 — carte ciblée Seeed XIAO nRF52840 Sense (une
   nRF52840 DK également commandée en secours/débogage). Board nativement
   supporté par NCS 3.4.0, pas de vendor clone requis
   (`C:\ncs\v3.4.0\zephyr\boards\seeed\xiao_ble\`, target
   `xiao_ble/nrf52840/sense`) — procédure de flash à valider sur le
   matériel réel (bootloader UF2 probable, à confirmer)
4. **Fin septembre 2026** : déploiement des 10 XIAO nRF54LM20A restants —
   voir « Flasher un nouveau lot » ci-dessus pour la checklist


# nouvelle journee:
implémentation des fonctions manquantes (chute/choc, double-tap, lecture du bouton physique, calcul du yaw) — on reprendra ça à la prochaine session:
Tamper/Chute-Choc (0x2B) et Vibration/Double-tap (0x2C) : l'IMU est bien câblée (bus I2C), et ces fonctions existent matériellement dans la puce (registres FREE_FALL, TAP_CFG). Ce qui manque, c'est la configuration logicielle de ces registres — donc "non implémenté" ou "non configuré", pas "non câblé".
Bouton (0x3A) : j'ai vérifié le devicetree de la carte — un bouton physique existe bel et bien (button0/sw0, déjà câblé au niveau matériel). Le firmware ne le lit simplement jamais. Donc "non câblé" est carrément faux ici : c'est "non lu par le firmware" / "non implémenté".
Yaw (0x3F #3) : l'IMU est câblée, le gyroscope est lu — c'est le calcul d'intégration dans le temps qui manque. Donc "non calculé" est le terme juste, pas "non câblé".