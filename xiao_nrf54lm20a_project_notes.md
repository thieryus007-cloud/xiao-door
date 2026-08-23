# XIAO nRF54LM20A → BLE IMU → Home Assistant

Objectif : des XIAO nRF54LM20A (Sense), un par porte/étage, diffusent leurs
données IMU en BLE (format BTHome v2). Des proxys BLE (ESPHome, un par étage)
relaient les annonces vers Home Assistant, qui tourne dans une VM VMware
Fusion (192.168.1.10) sur le Mac Mini, via passthrough Bluetooth du Mac.

**État : 3 XIAO déployés, flashés, vérifiés et intégrés dans Home Assistant**
avec le firmware `xiao_door_sensor` — profil L (trames A/B/C, angles
pitch/roll, événements IMU, batterie), adresse BLE fixe et stable. Aucune
modification de code requise entre exemplaires — l'adresse BLE fixe est
dérivée automatiquement du hardware ID unique de chaque puce. **Les 3
unités tournent depuis le 2026-08-30 sur le firmware System OFF** (réveil
matériel + veille profonde entre les événements, voir § « Firmware
déployé » et § « Implémentation System OFF » plus bas) — validé sur
matériel réel sur les 3 exemplaires, mesure de consommation réelle
(PPK II) encore à faire. **10 unités supplémentaires attendues fin
septembre 2026** — ce document est la référence pour reproduire le flash
rapidement sur ce lot (voir « Flasher un nouveau lot » en fin de
document).

## Unités déployées

| # | Adresse BLE (fixe) | N° série pont USB↔SWD | Statut HA |
|---|---|---|---|
| 1 | `D2:3A:F7:B1:E8:18` | `C5F0E209` | ✅ Intégré, à jour, firmware System OFF |
| 2 | `DE:F6:A3:A9:0F:0F` | `9C4A557D` | ✅ Intégré, à jour, firmware System OFF |
| 3 | `E6:C9:11:CE:6E:C6` | `4587B5C1` | ✅ Intégré, à jour, firmware System OFF |

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

Doit afficher `Bluetooth initialized -- door sensor, profil L (System
OFF): cold_boot=1 gpio_wake=0 reset_cause=...` au boot, une `trame B`
(batterie/santé) quasi immédiate, puis une `trame A (heartbeat)` dans la
seconde qui suit (le premier rapport de chaque type part au démarrage,
pas besoin d'attendre les intervalles normaux), puis plus rien — le SoC
repart en System OFF.

⚠️ **Étape indispensable, propre au firmware System OFF (depuis le
2026-08-30)** : la commande de flash OpenOCD ci-dessus laisse le SoC en
**« Debug Interface mode »** (datasheet nRF54LM20A §9.3) — dans cet état,
le System OFF est *émulé* et le firmware redémarre en boucle toutes les
1-2s au lieu de dormir réellement (repérable dans les logs :
`cold_boot=1 reset_cause=0x00000020` à *chaque* redémarrage, au lieu
d'apparaître une seule fois puis de laisser place au silence). **Après
chaque flash, débrancher puis rebrancher complètement le câble USB-C**
avant de considérer le test valide — un simple `reset` ou `exit`
OpenOCD ne suffit pas à sortir de ce mode. Une fois rebranché sans
session OpenOCD active, le port série doit rester silencieux entre deux
événements réels (pas de nouveau `*** Booting` toutes les 1-2s).

Une fois hors du mode debug : bouger la carte doit produire un réveil
réel (`gpio_wake=1`, `reset_cause=0x00000080` soit `RESET_LOW_POWER_WAKE`)
suivi d'une `trame A (motion)`, puis ~15s après l'arrêt du mouvement
d'une `trame A (repos)` ou `(angle)` avec `motion=0 activity=0`, puis
retour au silence (System OFF réel).

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
| C | packet_id, magnitude accel, magnitude gyro, accélération signée 3 axes (X/Y/Z, ajouté le 2026-08-30) | envoyée avec chaque trame A |

*chute/choc, double-tap, bouton, yaw et charge batterie ne sont **pas
câblés** — le driver Zephyr `lsm6dsl` n'expose pas ces événements matériels
via l'API `sensor_trigger` standard (nécessiterait d'écrire
`WAKE_UP_THS`/`TAP_CFG`/`MD1_CFG` en I2C brut) ; le yaw nécessiterait une
intégration gyroscopique dans la durée. Envoyés à 0 en attendant.

**Comportement (depuis le 2026-08-30, architecture System OFF hybride —
voir § « Implémentation System OFF » pour le détail complet) :** le SoC
dort en System OFF entre les événements, réveillé soit par l'IMU
(interruption matérielle INT1 sur seuil de mouvement), soit par le GRTC
(échéance trame santé 15 min / heartbeat 60 min). Une fois réveillé, la
logique ci-dessous — **inchangée** depuis l'ancienne version toujours
active — tourne le temps d'une « fenêtre active » avant de repasser en
veille :
- Sonde l'accéléromètre toutes les 2s pendant la fenêtre active
  (l'accéléromètre lui-même reste alimenté en continu par le PMIC,
  indépendamment de l'état System ON/OFF du SoC — c'est lui qui détecte
  le mouvement matériellement pour déclencher le réveil)
- Trame A/C sur mouvement (delta > 0,3 m/s²) ou franchissement d'angle
  (>2° vs dernier envoi), minimum 4s entre deux trames A, max 10/min —
  la toute première trame après un réveil sur mouvement part sans
  attendre ce delta logiciel (le réveil matériel a déjà confirmé le
  mouvement)
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
~0,9 mA en continu contre ~9 µA pour l'accéléromètre seul en low-power
@12,5 Hz (`LA_IddLM`, datasheet ST DocID030071 Rev 3, Table 4 p. 24 —
**valeur corrigée le 2026-08-23** : le chiffre précédemment noté ici,
~1,25 µA, ne correspond pas à la datasheet vérifiée directement — voir
`Recherche-Reveil-Materiel-XIAO.md` § « Découverte annexe » pour le
détail et l'hypothèse sur l'origine de l'erreur ; le commentaire
correspondant dans `main.c:220-221` reste à corriger séparément, pas
touché par cette mise à jour de notes). Sur une LiPo 1500 mAh, la
différence reste de l'ordre de **plusieurs mois d'autonomie (gyro
toujours actif) contre 2 à 7 ans (gyro coupé hors lecture)** — un
facteur **~100x** (0,9 mA / 9 µA, pas ~700x comme précédemment estimé),
toujours largement dominant devant tout autre poste (radio, PMIC,
etc.) : la décision de garder le gyro coupé hors lecture reste
pleinement justifiée, seul le chiffre cité était faux. Le firmware
actuel active le gyro uniquement le temps d'une lecture
(`set_gyro_power()`/`read_gyro_burst()` dans `main.c`, **200ms** de
stabilisation + lecture — porté de 80ms à 200ms le 2026-08-30, voir
§ « Implémentation System OFF » pour le détail du bug corrigé), jamais
en continu.

**Points de vigilance déduplication BTHome (§2.3c de `Evolution-XIAO-BLE.md`)** :
deux trames différentes envoyées avec le **même** `packet_id` à moins de 4s
d'intervalle sont indiscernables d'une retransmission pour le parseur HA —
la seconde est silencieusement écartée, sans erreur visible. Chaque
fonction d'envoi (trame A, B, C) doit incrémenter le compteur partagé
elle-même ; ne jamais réutiliser le `packet_id` d'une trame précédente pour
une trame de contenu différent.

**Implémenté et validé sur les 3 unités le 2026-08-30** — voir
`Recherche-Reveil-Materiel-XIAO.md` pour l'étude de faisabilité initiale
(registres exacts, sources primaires dans `docs/`) et § « Implémentation
System OFF » ci-dessous pour le détail de la mise en œuvre, des bugs
trouvés et des correctifs. Gain énergétique réel toujours **non mesuré**
(PPK II en attente) — l'étude théorique estimait un delta modeste côté
SoC seul (~3-3,3 µA), le plancher restant dominé par l'IMU (9 µA en
continu, identique dans les deux architectures).

## Implémentation System OFF — validée sur les 3 unités (2026-08-30)

Approche **hybride** retenue (System OFF entre les événements, logique
logicielle 2s **inchangée** une fois réveillé — voir l'en-tête de
`xiao_door_sensor/src/main.c` pour le détail complet de l'architecture :
retained_mem pour l'état traversant les redémarrages, registres I2C bruts
`WAKE_UP_THS`/`WAKE_UP_DUR`/`MD1_CFG`/`TAP_CFG` pour le réveil matériel
IMU, `z_nrf_grtc_wakeup_prepare()` pour les échéances périodiques).
Développée et testée d'abord sur l'**unité #3**, puis déployée sur **#1
et #2** une fois validée — **les 3 unités tournent sur le même firmware
depuis le 2026-08-30**, retirées puis réintégrées dans HA chacune leur
tour pendant les tests.

**Confirmé fonctionnel sur matériel réel, sur les 3 unités** :
- Réveil matériel sur mouvement (`gpio_wake=1`, `RESET_LOW_POWER_WAKE`)
  et retour en System OFF entre les événements.
- Trames A/B/C reçues correctement par HA une fois les bugs ci-dessous
  corrigés.
- Trame de repos (retour à l'immobilité après 15s) fonctionnelle.
- Gyroscope et accélération 3 axes cohérents avec le mouvement réel
  (une fois le bug de délai ci-dessous corrigé).

**Piège matériel à connaître pour toute future intervention sur ce
firmware** : tant qu'une sonde de debug (le pont SAMD11 utilisé pour
flasher) reste "attachée" côté SoC, celui-ci reste en **"Debug
Interface mode"** (datasheet nRF54LM20A §9.3) — le System OFF y est
*émulé* et se réveille en boucle en ~1-2s au lieu de dormir réellement
(`reset_cause=0x00000020`, `RESET_DEBUG`, à chaque cycle). Un simple
`exit` d'OpenOCD après le flash ne suffit pas à en sortir. **Toujours
débrancher/rebrancher complètement l'USB-C après chaque flash avant de
tester le comportement réel** (voir § « Procédure — vérifier »
ci-dessus).

**Bugs préexistants trouvés et corrigés en testant** (aucun lien avec
le System OFF lui-même, présents dans le code source avant le début de
ce travail, révélés par les tests répétés — voir `main.c` pour les
commentaires précis à chaque correctif) :
1. **Trame B dépassait la limite BLE de 31 octets** (33 octets réels,
   `err -22` "Too big advertising data") — `OBJ_BATTERY_CHARGE` (0x16,
   jamais câblé) retiré pour repasser sous la limite. Bloquait
   totalement battery/temperature/voltage côté HA.
2. **Décalage d'index dans `frame_c`** : la valeur du gyroscope
   (`C_OFF_GYRO_MAG`) était écrite sur l'octet d'ID de l'objet
   lui-même au lieu de sa valeur — corrompait la trame C, empêchait
   l'entité Gyroscope d'apparaître et produisait une entité fantôme
   ("Weight") côté HA par réinterprétation de l'octet corrompu.
3. **Délai avant lecture du gyroscope insuffisant** (`GYRO_STARTUP_MS`,
   80ms → **200ms**) : le driver Zephyr `lsm6dsl` lit les registres de
   sortie directement sans attendre le drapeau "donnée prête" ; à
   12,5 Hz une période ODR = 80ms, exactement l'ancien délai — le
   premier échantillon lu était donc systématiquement antérieur à la
   première conversion réelle (`Ton` datasheet ST = 35ms + 1 période
   ODR ≈ 115ms minimum). Confirmé sur matériel réel : gyroscope figé à
   une valeur identique au bit près sur des dizaines de lectures,
   malgré un mouvement vigoureux et varié (accélération, elle,
   variant normalement). Résolu, 200ms testé et validé sur les 3
   unités (gyroscope varie maintenant correctement, jusqu'à saturer
   son plafond d'encodage — 65,535°/s max représentable — lors des
   secousses les plus fortes, comportement normal).

**Amélioration faite à la demande de l'utilisateur** (pas un bug, une
optimisation de latence) : la toute première trame après un réveil sur
mouvement force `moving=true` sans attendre le calcul de delta logiciel
(qui nécessite un deuxième échantillon, donc un cycle de sondage
supplémentaire ~2s) — le réveil matériel a déjà confirmé le mouvement,
inutile de le reconfirmer en logiciel. Réduit le délai entre un
mouvement réel et sa première trame BLE de ~2-6s (boot + attente du
2ᵉ échantillon + anti-rafale 4s) à ~0,5-1s (boot + init Bluetooth
seuls). Le délai de 4s entre trames A **consécutives** pendant un
mouvement soutenu (`MOTION_REPORT_MIN_GAP_MS`) reste inchangé — il
préexistait à ce travail, comportement anti-rafale volontaire.

**Ajout fait à la demande de l'utilisateur** : 3 axes signés
d'accélération (`0x63` ×3, X/Y/Z) réintégrés dans la trame C pour
retrouver les 15 entités que les unités #1/#2 affichaient avant ce
travail (ancien firmware — la version de code trouvée dans ce dépôt au
début de la session ne les envoyait plus). Ordre de renommage HA vérifié
par recoupement indépendant (√(X²+Y²+Z²) ≈ magnitude affichée
séparément) — voir § « Entités Home Assistant générées ». ⚠️ Trame C
fait maintenant exactement 31 octets (calcul vérifié contre
`zephyr/subsys/bluetooth/host/adv.c:495`) — plus aucune marge, toute
mesure supplémentaire y échouera comme la trame B avant correction.

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
| `0x16` | battery charging (binaire) | non signé | 1 | — | 0/1 |
| `0x21` | motion (binaire) | non signé | 1 | — | 0/1 |
| `0x2B` | tamper/free-fall (binaire) | non signé | 1 | — | 0/1 |
| `0x2C` | vibration/double-tap (binaire) | non signé | 1 | — | 0/1 |
| `0x3A` | button (événement) | non signé | 1 | — | code événement |
| `0x3F` | rotation | **signé** | 2 | 0.1 | ° |
| `0x51` | acceleration | non signé | 2 | 0.001 | m/s² |
| `0x52` | gyroscope | non signé | 2 | 0.001 | °/s |
| `0x63` | acceleration (signée, par axe) | signé | 4 | 0.000001 | m/s² |

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
  pitch/roll/yaw) : Home Assistant les nomme `X`, `X_2`, `X_3` dans l'ordre
  d'apparition — garder cet ordre identique à chaque trame.
- `device_info` doit valoir `0x44` (v2, clair, **trigger-based**) sur
  toutes les trames — le bit trigger-based indique à HA de garder l'appareil
  "disponible" entre deux émissions espacées, au lieu de le marquer hors
  ligne après quelques minutes sans nouvelle donnée.

## Entités Home Assistant générées

| Object ID | Entité HA | Nom par défaut | Renommage suggéré |
|---|---|---|---|
| `0x0F` | `binary_sensor` | Generic | Activité |
| `0x21` | `binary_sensor` | Motion | Mouvement |
| `0x2B` | `binary_sensor` | Tamper | Chute / Choc *(non câblé, toujours 0)* |
| `0x2C` | `binary_sensor` | Vibration | Double-tap *(non câblé, toujours 0)* |
| `0x3A` | `event` | Button | Bouton *(non câblé, toujours 0)* |
| `0x3F` #1/#2/#3 | `sensor` | Rotation / Rotation 2 / Rotation 3 | Pitch / Roll / Yaw *(yaw non câblé, toujours 0)* |
| `0x01` | `sensor` | Battery | — |
| `0x02` | `sensor` | Temperature | Température interne *(diagnostic)* |
| `0x0C` | `sensor` | Voltage | — |
| `0x15` | `binary_sensor` | Battery | Batterie faible |
| `0x51` | `sensor` | Acceleration | magnitude |
| `0x52` | `sensor` | Gyroscope | Gyroscope |
| `0x63` ×3 (X, Y, Z) | `sensor` | Acceleration / Acceleration 2 / Acceleration 3 | axe X / axe Y / axe Z |

`0x16` (Battery charging) retiré le 2026-08-30 (voir « Bug de taille
advertising » plus bas) — n'a jamais été câblé de toute façon (toujours 0).

Ordre de renommage vérifié le 2026-08-30 sur l'unité #3 par recoupement
indépendant : √(axe X² + axe Y² + axe Z²) ≈ valeur de « magnitude »
affichée dans HA au même instant — confirme que l'ordre d'apparition des
objets `0x63` dans la trame (X, Y, Z, dans cet ordre d'écriture par
`send_frame_c()`) correspond bien à « Acceleration » / « Acceleration 2 »
/ « Acceleration 3 » dans l'interface HA, dans cet ordre.

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
| Une mise à jour sur deux semble ignorée | Deux trames différentes avec le même `packet_id` à moins de 4s | Chaque fonction d'envoi doit incrémenter le compteur elle-même (voir « Autonomie » ci-dessus) |
| `Motion`/`Generic` restent bloqués à "Détecté" | Trame de retour au repos jamais envoyée ou envoyée avec un `packet_id` dupliqué | Voir le bug corrigé du chronomètre `rest_since` (§ Firmware déployé) |
| Pitch/Roll/Yaw permutés | Ordre d'insertion des `0x3F` non constant | Toujours insérer dans le même ordre (pitch, roll, yaw) |
| Accélération toujours positive | `0x51` porte une magnitude, pas une valeur signée | Utiliser `0x63` pour des composantes signées par axe |
| IMU non détectée / lecture invalide | LDO1 du PMIC à 1,8V au lieu de 3,3V, ou driver non activé | Overlay carte (`&pmic { regulators { LDO1 {...} } }`), `CONFIG_LSM6DSL=y` |
| Angles bruités | Pas de moyennage | Moyenner 8-16 échantillons, attendre une stabilisation (‖a‖≈9,81±0,3 m/s² pendant ≥200ms) avant de calculer l'angle |
| Redémarrage en boucle toutes les 1-2s après un flash (firmware System OFF) | Sonde de debug encore "attachée" (Debug Interface mode), System OFF émulé | Débrancher/rebrancher complètement l'USB-C (voir § « Procédure — vérifier ») |
| Gyroscope figé à une valeur identique malgré un mouvement réel | Lecture trop tôt après mise sous tension (`GYRO_STARTUP_MS` insuffisant vs période ODR) | Corrigé le 2026-08-30, `GYRO_STARTUP_MS` porté à 200ms (voir § « Implémentation System OFF ») |
| Trame BLE ne part pas, `err -22` "Too big advertising data" dans les logs | Payload > 31 octets (limite legacy BLE) | Vérifier `BT_GAP_ADV_MAX_ADV_DATA_LEN` (31) contre la taille réelle de la trame ; retirer/raccourcir un champ |

## Banc de test BLE Extended Advertising (Publicité Étendue)

Validé le 2026-08-22 : XIAO nRF54LM20A émettant en advertising étendu
(`CONFIG_BT_EXT_ADV=y`, trame unique BTHome jusqu'à 255 octets au lieu de 3
trames de 31 octets) reçu par un ESP32-S3 (Autosport Labs ESP32-CAN-X2,
firmware ESP-IDF natif `esp_ble_gap_start_ext_scan()` —
`C:\ncs\projects\esp32_ext_scan_bench`, sortie console uniquement, pas de
Wi-Fi/ESPHome). Réception confirmée et stable : `type ETENDU`, `74 octets`,
`UUID 0xFCD2, device info 0x44, 57 octets de mesures`.

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
   **Ne pas oublier l'étape débrancher/rebrancher l'USB-C après le
   flash** (firmware System OFF, sonde de debug sinon toujours active —
   voir § « Procédure — vérifier ») avant de considérer le test concluant.
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
6. ✅ **Réveil matériel + System OFF implémenté et validé sur les 3
   unités** (2026-08-30) — voir § « Implémentation System OFF » pour le
   détail complet (architecture, bugs trouvés/corrigés en testant :
   dépassement de taille trame B, décalage d'index trame C, délai
   gyroscope insuffisant). Gain énergétique réel encore non mesuré.
7. Mesure d'énergie réelle (PPK II ou nPM1300 EK) pour remplacer l'estimation
   théorique par une mesure sur silicium, avant de commander le lot de 10.
   **Devenue plus prioritaire depuis le 2026-08-23** : objectif exprimé de
   passer à une batterie 400-600 mAh (au lieu de 1500 mAh) pour 3-4+ ans
   d'autonomie — le budget calculé (pas mesuré) dans
   `Recherche-Reveil-Materiel-XIAO.md` § « Budget énergétique complet »
   donne ~3,5 ans (soft actuel) à ~4,9 ans (soft optimisé System OFF) sur
   600 mAh, mais deux postes du calcul (radio BLE, boucle de sondage) sont
   estimés, pas mesurés — une mesure réelle validerait ou invaliderait ce
   calcul directement. **Comparaison à faire une fois le PPK II disponible :
   consommation mesurée du firmware System OFF (les 3 unités actuelles)
   vs firmware précédent** (garder un exemplaire flashable avec l'ancien
   firmware si une comparaison directe est souhaitée — non conservé
   séparément à ce jour, seul le nouveau firmware est dans
   `xiao_door_sensor/`).
8. Remplacer le proxy BLE ESPHome temporaire par les ESP32 dédiés une fois
   reçus (un par étage), puis décommissionner `ble-proxy-temp`
9. **Semaine du 24/08/2026** : démarrage nRF52840 — carte ciblée Seeed XIAO
   nRF52840 Sense (une nRF52840 DK également commandée en secours/débogage).
   Board nativement supporté par NCS 3.4.0, pas de vendor clone requis
   (`C:\ncs\v3.4.0\zephyr\boards\seeed\xiao_ble\`, target
   `xiao_ble/nrf52840/sense`) — voir détails de compatibilité dans la
   conversation, procédure de flash à valider sur le matériel réel
   (bootloader UF2 probable, à confirmer)
10. **Fin septembre 2026** : déploiement des 10 XIAO nRF54LM20A restants —
    voir « Flasher un nouveau lot » ci-dessus pour la checklist
