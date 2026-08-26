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
`PPK-Mesures-Transition.md`). **Environ 17 unités supplémentaires
attendues** (même firmware, mêmes fonctionnalités que les 3 actuelles ;
chiffre mis à jour le 2026-08-24, remplace l'estimation précédente de 10
pour fin septembre 2026) — ce document est la référence pour reproduire le
flash sur ce lot (§ « Flasher un nouveau lot »).

## Unités déployées

| # | Adresse BLE (fixe) | N° série pont USB↔SWD | Statut HA |
|---|---|---|---|
| 1 | `D2:3A:F7:B1:E8:18` | `C5F0E209` | ✅ Intégré, à jour |
| 2 | `DE:F6:A3:A9:0F:0F` | `9C4A557D` | ✅ Intégré, à jour |
| 3 | `E6:C9:11:CE:6E:C6` | `4587B5C1` | ✅ Intégré, à jour |

**Les trois unités tournent la même version du firmware depuis le
2026-08-25** (correctifs #1-7 de la session du 24, fix `packet_id`/RAM
retenue, plafond de blocage temps-réel `ACTIVE_WINDOW_MAX_MS`=2 min,
retransmission ×3 de la trame d'état final — voir § « Logique complète de
déclenchement des événements » — **+ bouton physique (lecture d'état
simple, en pause/non fonctionnel, voir § Préparation — bouton) + yaw
(intégré et validé, voir § Préparation — yaw)**).

Pour ajouter une nouvelle unité dans HA : découverte automatique BTHome
(non chiffré) — apparaît sous l'adresse ci-dessus dès la première annonce
BLE après flash.

## Déployer rapidement une nouvelle unité (résumé)

Pour reproduire sur les ~17 XIAO attendus, avec le
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
   intermittent, pas un défaut de carte). **Vérifier aussi la sortie pour
   la signature HardFault** (`pc: 0xeffffffe`, voir avertissement dans §
   Procédure — flasher) **avant** de demander le débranchement/
   rebranchement — reflasher immédiatement si elle apparaît, ça a
   toujours suffi jusqu'ici.
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

**Disposition physique des tests** : XIAO, proxy BLE ESPHome et VM Home
Assistant sont tous à moins d'un mètre les uns des autres. Toute cause
liée à la distance/portée radio est donc exclue pour le diagnostic d'un
bug — voir `C:\ncs\CLAUDE.md` § « Chercher la cause dans le code, jamais
dans l'environnement ».

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

**Plusieurs cartes branchées en même temps** (utile pour le lot de ~17 à
venir) : ajouter `-c "adapter serial <numéro-de-série-pont>"` (une commande
séparée, PAS un argument de `cmsis-dap vid_pid` ni de `cmsis-dap serial`,
les deux échouent avec "Invalid command argument"/"unknown command" sur
cette version d'OpenOCD) pour cibler une carte précise sans ambiguïté.
Vérifié le 2026-08-24 avec #01 et #03 branchées simultanément.

⚠️ **HardFault possible au tout premier flash d'une carte donnée avec une
nouvelle version de firmware** (observé le 2026-08-25 sur #02 et #03,
jamais sur #01 qui avait déjà cette lignée de firmware) : signature
systématique `Error: clearing lockup after double fault`,
`pc: 0xeffffffe`, `msp: 0x00005540` (valeur identique observée deux fois).
Cause exacte non identifiée. **Solution qui a fonctionné à chaque fois :
relancer exactement la même commande de flash une seconde fois** — le
second essai passe sans erreur (`msp` revient dans la plage RAM normale
`0x2000...`), rebrancher normalement ensuite. Donc : toujours vérifier le
retour de la commande de flash avant de rebrancher — si la signature
ci-dessus apparaît, reflasher immédiatement avant de demander le
débranchement/rebranchement à l'utilisateur. Pas encore vu sur un
troisième essai consécutif à date.

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
- Trame rapportant l'état actuel (motion=0, activity=0 — ce n'est **pas**
  une trame "repos" à part, c'est la même trame d'événement standard qui
  reflète simplement l'état réel du capteur au moment où elle part), 10s
  après le dernier mouvement réel, ou 30s après le tout premier mouvement
  de la fenêtre au plus tard (filet de sécurité) — jamais bloquée par la
  limite de 10 trames/min
- Rafales BLE courtes (700ms, `BT_LE_ADV_NCONN_IDENTITY`), pas de radio
  entre les rafales
- Retour en System OFF une fois la fenêtre active terminée

Voir l'en-tête de `xiao_door_sensor/src/main.c` pour le détail complet de
l'implémentation.

### Logique complète de déclenchement des événements (état au 2026-08-24)

Référence : `run_active_window()` et `main()` dans `main.c`. Trois sources de
réveil possibles, une seule fenêtre active par réveil (fonction bloquante,
tourne jusqu'à ce que tout redevienne calme, puis rend la main).

**1. Ce qui déclenche un boot**

| Source | Détectée via | Effet sur l'état retenu |
|---|---|---|
| Cold boot (alim coupée/rebranchée, reset pin/software/debug, reflash) | `reset_cause & (RESET_PIN\|RESET_SOFTWARE\|RESET_POR\|RESET_DEBUG)` | **Tout remis à zéro** (`packet_id`, derniers angles envoyés, échéances GRTC, historique diag) — `fresh_session=true` |
| Réveil GPIO (IMU) | `reset_cause & RESET_LOW_POWER_WAKE` | État retenu conservé |
| Réveil GRTC (périodique) | ni l'un ni l'autre des deux ci-dessus, ou échéance GRTC déjà dépassée au boot | État retenu conservé |

Le réveil GPIO est déclenché par la broche INT1 de l'IMU, configurée en
**interruption de niveau actif** (`GPIO_INT_LEVEL_ACTIVE`, pas un front) et
**non latchée** (`LIR`=0, valeur par défaut) : elle redescend toute seule dès
que l'accélération repasse sous le seuil (`WK_THS=0x01` ≈31 mg,
`WAKE_UP_DUR=0x00`). Réarmée à chaque entrée en System OFF (l'état logiciel
GPIOTE ne survit pas au System OFF, contrairement à la configuration
matérielle de la broche).

**2. Déroulement d'un boot (`main()`)**

1. Lit la cause de reset, en déduit `cold_boot`/`gpio_wake`, charge l'état
   retenu (remis à zéro si `fresh_session`).
2. Initialise l'IMU ; reconfigure le seuil de réveil matériel **seulement**
   si `fresh_session` (sinon déjà configuré, pas besoin de réécrire les
   registres I2C à chaque réveil).
3. Fixe l'identité BLE, active le Bluetooth.
4. Affiche l'historique de diagnostic retenu (20 derniers cycles).
5. Envoie la **trame B** immédiatement si `fresh_session` ou si l'échéance
   santé (15 min ± jitter 30s) est dépassée.
6. Si `gpio_wake` **ou** échéance heartbeat (60 min) dépassée → entre dans
   la **fenêtre active** (étape 3 ci-dessous), sinon saute directement à
   l'étape 7.
7. Sauvegarde l'état retenu, réarme l'interruption GPIO, programme le
   prochain réveil GRTC (au plus tôt entre l'échéance santé et heartbeat,
   plancher 1s), enregistre une entrée dans l'historique de diagnostic
   (cause de reset + résultat de l'armement + raison de sortie de la
   fenêtre active + nombre d'itérations, voir § suivant), repart en
   System OFF.

**3. Dans la fenêtre active (sondage logiciel toutes les 2s)**

À chaque échantillon accéléromètre :
- Mouvement réel = delta d'accélération > 0,3 m/s² vs l'échantillon
  précédent. **Exception** : au tout premier échantillon d'un réveil GPIO,
  le mouvement est forcé à vrai immédiatement (le réveil matériel l'a déjà
  confirmé, inutile d'attendre un deuxième échantillon logiciel).
- Franchissement d'angle = pitch ou roll a varié de plus de 2,0° depuis la
  dernière trame envoyée.
- Une **trame A + trame C** part si (heartbeat en attente) OU ((mouvement OU
  franchissement d'angle) ET ≥4s depuis la dernière trame A) — sous réserve
  de ne pas dépasser 10 trames A/minute (fenêtre glissante 60s, s'applique
  uniquement à ces trames d'événement).
- Le firmware suit depuis quand il n'y a plus de mouvement réel et depuis
  quand le tout premier mouvement de la fenêtre a commencé. Une trame
  rapportant l'**état actuel** (motion=0, activity=0 — pas un type de trame
  à part, la même trame A standard qui reflète simplement qu'il n'y a plus
  de mouvement) part dès que 10s se sont écoulées depuis le dernier
  mouvement réel, ou 30s depuis le tout premier mouvement au plus tard
  (filet de sécurité, pour ne pas être repoussé indéfiniment par de petites
  vibrations résiduelles qui redémarreraient sans cesse le délai de 10s).
  **Cette trame n'est jamais bloquée par la limite de 10/min** — la
  bloquer laisserait HA figé sur "Détecté" si beaucoup de trames étaient
  déjà parties dans la minute précédente.
- La fenêtre se termine dès qu'il n'y a plus de mouvement en cours, plus
  d'historique de mouvement en attente de clôture, et plus de heartbeat en
  attente.
- **Garde-fou** : si la fenêtre tourne en continu plus de 2 minutes
  (`ACTIVE_WINDOW_MAX_MS`, basé sur le temps réel écoulé, pas un nombre
  d'itérations — voir plus bas), sortie forcée avec envoi d'une dernière
  trame d'état actuel si un mouvement était en cours. **Historique de ce
  garde-fou (2026-08-24)** : initialement 900 itérations (~30 min,
  supposant ~2s/itération), sortait sans jamais envoyer de trame corrective
  — corrigé une première fois (envoi ajouté) puis réduit à 60 itérations
  (~2 min) après plusieurs blocages réels de plusieurs minutes ; des
  blocages de 14 à 27 minutes ont malgré tout persisté, menant à
  l'hypothèse que le plafond en *itérations* n'est pas fiable si un cycle
  dure plus longtemps que les ~2s nominaux (ex. lecture I2C qui bloque) —
  remplacé par un plafond en *temps réel écoulé* (`k_uptime_get()`),
  robuste indépendamment de la durée de chaque cycle.

**Cause probable principale du blocage prolongé, identifiée le
2026-08-24** : ni le plafond ni la fenêtre active elle-même n'étaient en
cause dans la plupart des cas observés — attendre seul (14+ minutes) ne
corrigeait jamais l'état, alors qu'un nouveau mouvement réel le corrigeait
immédiatement. Ça exclut une fenêtre active bloquée en boucle (qui
finirait par se corriger seule avec le temps). L'explication retenue :
**la fenêtre se termine normalement et rapidement, envoie bien sa trame
d'état final, mais cette trame radio spécifique est occasionnellement
perdue** (BLE en advertising, pas d'accusé de réception ni de
retransmission automatique, une seule rafale de 700ms) — sans conséquence
pour une trame d'événement (juste un léger retard), mais laissant HA figé
sur "Détecté" jusqu'au prochain mouvement réel ou jusqu'au prochain
heartbeat périodique (60 min, pas 15 — la trame B/15min ne contient pas
l'objet mouvement). **Correctif** : la trame d'état final est désormais
envoyée 3 fois de suite (`FINAL_STATE_REPEATS`), espacées de 3s
(`FINAL_STATE_REPEAT_GAP_MS`), chacune avec un `packet_id` différent —
voir `send_final_state_frame()` dans `main.c`. **Validé le 2026-08-25** :
les trois unités ont fonctionné normalement toute la nuit et ont réagi
correctement le matin suivant — aucun blocage "Détecté" prolongé observé
depuis le flash de ce correctif.

**Exemple de capture réelle confirmant le mécanisme (2026-08-25, #01,
`D2:3A:F7:B1:E8:18`)** — journalisation brute côté proxy BLE (§ « Proxy
BLE temporaire », `on_ble_advertise`), décodée octet par octet :

```
12:28:53  pid=0x87  trame A "motion"  activity=1 motion=1  pitch=-5.7° roll=-131.0°
12:28:53  pid=0x88  trame C (magnitudes + accel signée XYZ, suit chaque trame A)
12:29:11  pid=0x8B  trame A "état final"  activity=0 motion=0  pitch=10.7° roll=-87.7°
                     -- 16s après le dernier mouvement réel (délai 10s + granularité sondage 2s)
12:29:14  pid=0x8C  MEME contenu (motion=0, mêmes angles), 3s plus tard
                     -- 2e des 3 retransmissions (FINAL_STATE_REPEAT_GAP_MS), packet_id
                        différent -> aucun risque de déduplication BTHome côté HA
```

Format brut décodé (`service_data raw`, sans le préfixe UUID `d2 fc`) :
`44`=info/trigger ; `00 <val>`=packet_id ; `0F <val>`=activité ;
`21 <val>`=mouvement ; `2B/2C/3A <val>`=chute/tap/bouton (à 0, non
implémentés) ; trois blocs `3F <lo> <hi>`=pitch/roll/yaw (int16 LE,
dixièmes de degré) pour la trame A ; pour la trame C : `51 <lo><hi>`=accel
magnitude, `52 <lo><hi>`=gyro magnitude, trois blocs `63 <4 octets
LE>`=accel signée X/Y/Z (int32, facteur 0.000001 m/s²).

Confirme que toute la chaîne fonctionne de bout en bout : détection
mouvement → trame A+C → délai → trame d'état final → retransmissions à
`packet_id` distincts → réception proxy vérifiable indépendamment de HA.

**Instrumentation de diagnostic (2026-08-24)** : chaque entrée de
l'historique retenu (`diag_log_*`, 20 derniers cycles, survit au System OFF
mais pas à une coupure d'alimentation complète, affiché en entier à chaque
boot) enregistre désormais, en plus de la cause de reset et des codes retour
d'armement des réveils : la raison de sortie de la fenêtre active
(`aw_exit_reason` — jamais de mouvement / état actuel envoyé normalement /
état actuel forcé par le plafond 30 min / plafond atteint sans mouvement en
cours) et le nombre d'itérations effectuées (`aw_iters`). Objectif : si un
nouvel épisode de blocage survient, laisser une preuve exploitable dans le
code plutôt que de devoir spéculer. Dump des octets bruts de la trame A
avant l'envoi radio conservé (`LOG_HEXDUMP_INF`).

⚠️ Un reset via sonde SWD sans cycle d'alimentation complet ensuite fait
tourner la carte en boucle de reboot toutes les 1-2s (mode "Debug Interface",
voir § Procédure — flasher) — chaque itération de cette boucle ajoute une
entrée artefact dans cet historique et peut écraser les entrées utiles d'un
épisode réel avant qu'elles aient pu être lues. Toujours faire le cycle
d'alimentation complet **avant** de capturer, pas après.

Anomalie connue, non bloquante : un second réveil GRTC (`reset_cause=0x800`)
survient systématiquement ~2s après chaque réveil GRTC normal, sans nouvelle
trame ni erreur — cause non identifiée, à investiguer.

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

**Sensibilité du réveil — deux tests réels avec valeurs d'angle précises
(2026-08-24, unité #1)**, pour évaluer si un mouvement lent/doux déclenche
bien le réveil GPIO (seuil `WK_THS=0x01` ≈ 31 mg, un seul échantillon,
voir § « Séquence de transmission par phase ») :

1. **Mouvement lent et continu, sans à-coup, petit angle** : réveil GPIO
   déclenché. Pitch relevé sur toute la fenêtre active : -4,3° → 1,5° →
   9,6° → 8,9° → 2,7° → 0,9° → -1,4° (amplitude ~13,9°, roll -96,6° à
   -89,8°, amplitude ~6,8°, sur ~40s).
2. **Mouvement plus court, angle différent** : réveil GPIO déclenché.
   Roll -88,3° → -91,0° → -90,9° (amplitude ~2,7°), pitch quasi constant
   (~-2°).

Dans les deux cas, réveil confirmé (`gpio_wake=1`) et remontée correcte
dans HA. Le seuil s'est montré sensible même à un démarrage de mouvement
volontairement doux — pas encore trouvé la limite en dessous de laquelle
un mouvement ne déclenche plus rien.

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

**Logs en direct par le réseau** (utile pour vérifier si une trame BLE
arrive réellement au proxy, indépendamment de ce que HA/BTHome en fait
ensuite — diagnostic du 2026-08-24, blocage "Détecté" persistant) :

```powershell
cd C:\ncs\vendor\esphome_proxy
python -m esphome logs ble-proxy-temp.yaml --device 192.168.1.20
```

⚠️ Sans `--device 192.168.1.20`, la commande demande un choix interactif
(port série vs OTA) et échoue silencieusement en usage non-interactif
(`EOFError`). Pas besoin de rebrancher le proxy en USB — passe par l'API
ESPHome (`api:` activé dans la config) sur le réseau local.

**Interface web** : `web_server:` ajouté le 2026-08-25 —
accessible à `http://192.168.1.20`. **`version: 1`** (page HTML classique
générée par la carte) — la version 2 par défaut (`<esp-app>`, composant
web isolé en Shadow DOM) empêchait tout CSS injecté d'atteindre le tableau
des entités, contrairement à la v1 où `css_include: web_custom.css` (à la
racine du dossier proxy) masque fiablement `table#states` (vide, aucune
entité HA configurée ici) pour ne garder que le panneau `Debug Log` —
confirmé fonctionnel le 2026-08-25. Pour voir les publicités BLE reçues,
utiliser aussi l'intégration Bluetooth native de HA (Paramètres →
Appareils → Bluetooth), qui affiche tout ce que ce proxy relaie.
Mise à jour poussée par OTA (`python -m esphome upload ble-proxy-temp.yaml
--device 192.168.1.20`) — fonctionne bien contrairement à l'upload
série documenté plus haut (limitation propre au port série de ce clone,
pas à l'OTA).

**Journalisation brute des publicités BLE des 3 XIAO (2026-08-25)** :
`on_ble_advertise` (dans `esp32_ble_tracker`, `ble-proxy-temp.yaml`) filtré
sur les 3 adresses BLE fixes, journalise MAC + RSSI + octets bruts de
chaque `service_data`/`manufacturer_data` (tag `xiao_ble`). Visible dans le
flux `esphome logs` et dans le panneau web `Debug Log` — permet de
vérifier si une trame arrive réellement au proxy, indépendamment de ce que
HA/BTHome en fait ensuite. Ne montre rien tant qu'aucun XIAO n'a émis
(attendre un mouvement réel ou la prochaine échéance trame B/heartbeat).

⚠️ L'horodatage `[HH:MM:SS]` vu dans `esphome logs` est ajouté côté PC par
l'outil CLI, **pas par la carte** — invisible dans le panneau web (flux
brut) sans correctif. Ajouté : composant `time:` (SNTP, `Europe/Rome`),
horodatage réel intégré directement dans la ligne de log (`id(sntp_time).
now().strftime("%H:%M:%S")`), visible partout désormais.

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

1. Mesure d'énergie réelle (PPK II, à recevoir — un nRF54LM20-DK également
   prévu, utile comme point de mesure dédié et comme sonde SWD externe plus
   fiable que le pont SAMD11 intermittent des XIAO) pour remplacer
   l'estimation théorique par une mesure sur silicium — voir
   `PPK-Mesures-Transition.md`. **Décision (2026-08-24) : reportée à un
   projet séparé**, une fois le PPK II reçu ET la configuration cible
   (firmware, fonctions bouton/yaw/chute/tap) définitivement finalisée —
   pas de mesure sur une config encore mouvante. Objectif associé : passer
   à une batterie 400-600 mAh (au lieu de 1500 mAh) pour 3-4+ ans
   d'autonomie, décision à prendre une fois la mesure disponible (budget
   calculé, pas mesuré, § « Budget énergétique (calculé, non mesuré) »
   plus haut).
2. Remplacer le proxy BLE ESPHome temporaire par les ESP32 dédiés une fois
   reçus (un par étage), puis décommissionner `ble-proxy-temp`
3. Démarrage nRF52840 — **document de transition dédié préparé le
   2026-08-25** : `Transition-nRF52840-Sense-Demarrage.md` (à ouvrir en
   premier dans une nouvelle conversation). 3 unités en test, démarrage
   prévu sur une seule en BLE d'abord.
4. Déploiement des ~17 XIAO nRF54LM20A restants (date à préciser) — voir
   « Flasher un nouveau lot » ci-dessus pour la checklist
5. **Implémentation des fonctions actuellement envoyées à 0** (chute/choc,
   double-tap, bouton, yaw) — prévu pour la prochaine session, préparé
   ci-dessous. Rappel de vocabulaire (voir aussi `CLAUDE.md`) : dans les
   quatre cas, le matériel est bien câblé — c'est le firmware qui ne
   configure/lit/calcule pas encore la donnée, jamais "non câblé".

### Préparation — chute/choc (0x2B) et double-tap (0x2C)

Registres LSM6DS3TR-C déjà partiellement en jeu pour le réveil GPIO
(`configure_imu_wakeup()`, `main.c`) :
- `WAKE_UP_THS` (0x5B) bit7 `SINGLE_DOUBLE_TAP` — actuellement à 0
  (`IMU_WAKE_UP_THS_VALUE=0x01`, bit7 non positionné) : c'est ce bit qui
  active la détection tap/double-tap sur la puce, à mettre à 1.
- Registres tap dédiés à configurer en plus (non touchés à ce jour) :
  `TAP_CFG` (0x58, déjà écrit pour `INTERRUPTS_ENABLE` bit7 — les autres
  bits de ce registre gèrent les axes tap actifs), `TAP_THS_6D` (0x59,
  seuil), `INT_DUR2` (0x5A, durée/latence/quiet du double-tap).
- Chute libre : registre `FREE_FALL` (0x5D, `FF_THS[2:0]`/`FF_DUR`, ce
  dernier partagé avec `WAKE_UP_DUR` bit7 `FF_DUR5`).
- Sources déjà présentes dans le dépôt : datasheet ST DocID030071 Rev 3
  (`docs/LSM6DS3TR-C_datasheet_DocID030071_Rev3.pdf`) — chercher les
  sections tap-recognition et free-fall pour la séquence d'écriture
  exacte et l'ordre des registres.
- Routage vers une broche INT (probablement INT1, déjà utilisée pour le
  wake GPIO — vérifier si un second GPIO IMU est disponible ou s'il faut
  partager/distinguer les sources d'interruption) à étudier avant de
  coder.

### Préparation — bouton physique (0x3A)

Devicetree déjà vérifié : `sw0 = &button0` → `gpio0 9`
(`GPIO_PULL_UP | GPIO_ACTIVE_LOW`), `compatible = "gpio-keys"`
(`xiao_nrf54lm20a_nrf54lm20a-common.dtsi:43-50,105`). Lecture simple via
`gpio_pin_get_dt()`/`gpio_pin_interrupt_configure_dt()`, même pattern que
`imu_int1` dans `main.c`. Question de design à trancher avant de coder :
le bouton doit-il aussi être une source de réveil System OFF (comme
l'IMU), ou seulement lu pendant une fenêtre déjà active ?

### État — bouton physique (2026-08-25) : implémenté, non validé, en pause

**Implémenté sur #01** : `init_button()`/`read_button_state()` (lecture
d'état simple, pas de réveil dédié — voir `main.c`), lit `frame_a[A_OFF_
BUTTON]` à chaque trame A. Brochage revérifié et confirmé correct par
deux sources indépendantes (datasheet Nordic local, pin P0.09 documenté
comme "General Purpose I/O" sans fonction concurrente — l'hypothèse
initiale d'un conflit NFC était fausse, NFC1/NFC2 sont sur P1.01/P1.02 sur
cette puce ; et wiki Seeed officiel, qui confirme P0.09 = User Button,
distinct du bouton Reset).

**Symptôme non résolu** : le bouton (utilisateur, confirmé distinct du
Reset) reste systématiquement à 0 dans les trames transmises, y compris
avec appui maintenu pendant un mouvement provoqué. Diagnostic ajouté
(log de l'état brut à chaque cycle de sondage, 2s) pour isoler un
problème de timing d'une lecture réellement bloquée — **mais #01 a cessé
de produire toute sortie série après le flash de cette version de
diagnostic, y compris après reset SWD et cycle d'alimentation complet**.
**Cause confirmée** : la ligne de log ajoutée (`LOG_INF("diag bouton: raw=%d", ...)`
à chaque cycle de sondage) provoquait bien le silence total — retirée,
transmissions immédiatement rétablies (vérifié par reset SWD direct après
flash). Mécanisme exact non investigué plus avant (pourrait être un
volume de logs trop élevé pour le buffer de la tâche de log, `Task Log
Buffer Size` limité — non confirmé). **Bouton lui-même (lecture d'état,
sans ce log) : toujours à 0 dans tous les tests, cause non résolue,
en pause** — priorité donnée au yaw, à reprendre plus tard.

### Préparation — yaw (0x3F #3)

Le gyroscope est déjà lu (`read_gyro_burst()`, trame C) mais seulement
ponctuellement (200ms toutes les fois où une lecture est nécessaire, pas
en continu — voir § « Autonomie » : le gyro ne doit jamais tourner en
continu, ~0,9 mA vs ~9 µA accéléromètre). Le yaw nécessite une intégration
temporelle (Δyaw = gz × Δt à chaque lecture), donc : garder un yaw courant
dans `retained_state` (survit au System OFF), l'incrémenter à chaque
`read_gyro_burst()` avec le Δt réel écoulé, et prévoir un recalage
périodique (dérive gyroscopique inévitable sur intégration pure) —
probablement à l'inactivité prolongée, en re-zérotant ou en utilisant
l'accéléromètre pour une référence absolue quand c'est possible (le yaw
n'est pas observable par l'accéléromètre seul, contrairement à
pitch/roll — seul un recalage manuel ou une hypothèse de repos permet de
limiter la dérive).

### État — yaw (2026-08-25) : implémenté et validé sur #01

Implémenté (`retained.yaw_dd`, `read_gyro_and_integrate_yaw()`,
`main.c`) avec le bug d'ordre de la tentative précédente corrigé :
l'intégration se fait désormais **avant** `send_frame_a()` (qui lit
`retained.yaw_dd`), pas après — sinon la trame envoyée porte toujours la
valeur du cycle précédent. `send_frame_c()` reçoit les valeurs gyro déjà
lues plutôt que de relire le capteur une seconde fois (coût énergétique).
`struct retained_state` déplacée plus tôt dans le fichier (avant
`send_frame_a`/`send_frame_c`, qui lisent/écrivent `yaw_dd`) pour
respecter l'ordre de déclaration C.

**Validé par test réel (2026-08-25, #01)** : rotation provoquée par
l'utilisateur, yaw décodé dans les trames brutes captées par le proxy
BLE : -14,8° pendant la rotation, stable à -17,4° une fois arrêtée (pas
de dérive visible sur cette courte fenêtre). Toujours pas de recalage
anti-dérive (dérive lente attendue sur une session longue, §
préparation ci-dessus) — à surveiller/implémenter dans une session
ultérieure si la dérive s'avère gênante en usage réel.