# État + plan -- System OFF + GRTC + RAM retenue + BLE (sans IMU)

**Document de reprise pour une nouvelle conversation — à ouvrir en
premier.** Mis à jour le 2026-08-27. Ne contient que l'état actuel
vérifié et un plan en attente d'approbation -- pas de narration
d'essais/erreurs (l'historique complet de diagnostic reste dans
`Transition-nRF54LM20A-Optimisation-Consommation.md`, non dupliqué ici).

## 🔴 RÈGLE ABSOLUE -- CONFIG_SERIAL, à ne plus jamais oublier

**DÈS QU'UN PÉRIPHÉRIQUE SUPPLÉMENTAIRE (BLE, IMU + régulateur,
`CONFIG_PM_DEVICE_RUNTIME`) EST AJOUTÉ À LA BASE SYSTEM OFF :
`CONFIG_SERIAL=n` / `CONFIG_CONSOLE=n` / `CONFIG_UART_CONSOLE=n` EN DUR
DANS `prj.conf`. NE JAMAIS LAISSER `CONFIG_SERIAL=y` AVEC UNE SUSPENSION
À L'EXÉCUTION (`pm_device_action_run(cons, PM_DEVICE_ACTION_SUSPEND)`).**
Bug driver UARTE documenté (fuite de référence PM runtime) : la
suspension à l'exécution ne fonctionne QUE sur la base System OFF
minimale et nue (test #13). Dès qu'un autre périphérique tourne en même
temps, elle échoue systématiquement (`-120`) et le serial reste actif en
System OFF -- ~260 à ~470 µA au lieu de ~3 µA. **Cette erreur a été
refaite deux fois dans une même session avant d'être corrigée** -- voir
mémoire `feedback_xiao_serial_never_runtime_suspend`. Toujours vérifier
`prj.conf` avant tout flash destiné à une mesure PPK2.

## 🔴 Objectif final du projet -- non négociable

**5-6 µA en continu**, pour le comportement COMPLET de l'appareil (trames
BTHome santé/heartbeat périodiques + réactivité détection de mouvement
~1 s), pas un seul volet mesuré isolément. Comparaison : le projet frère
XIAO nRF52840 Sense atteint déjà ~10 µA avec la détection de mouvement
fonctionnelle -- le nRF54LM20A doit faire mieux.

## 🔴 RÈGLE ABSOLUE -- le PMIC nPM1300 garde son état d'un flash à l'autre

**Le nPM1300 reste alimenté en continu par la batterie, indépendamment
du System OFF du SoC -- ses registres (mode régulateur, etc.) NE SE
RÉINITIALISENT PAS entre deux flashs/reboots du SoC.** Retirer une
propriété devicetree (ex. `regulator-initial-mode`) arrête juste de la
*réécrire* au prochain boot, ça ne *réinitialise* pas le registre déjà
écrit par un test antérieur -- **piège déjà tombé dedans une fois cette
session** (un test "mode par défaut" a semblé sans effet alors qu'il ne
testait en réalité rien de nouveau, le registre étant resté sur la
valeur d'un test précédent). **Toujours vérifier l'état réel par lecture
registre (`mfd_npm13xx_reg_read`) avant de conclure qu'un changement de
configuration devicetree du PMIC a été sans effet.**

## Reconfirmation base connue (test #13, 2026-08-27)

Après les tests #11 (fenêtre TWI étendue) et #12 (broche PDM_CLK
pilotée), retour exact à la base validée (régulateur `imu_vdd`/LDO1
jamais activé, broches PDM non touchées) : **3,3 µA reconfirmés au
PPK2** -- aucune dérive. Correctif `CONFIG_NCS_BOOT_BANNER=n` (voir plus
bas) inclus dans ce build sans incidence sur la mesure. Unité #01
actuellement dans cet état (régulateur inactif, code des tests #11/#12
conservé dans `main.c` mais non appelé depuis `main()`).

## État actuel (vérifié le 2026-08-27, dernière action de la session)

**Unité #01** (`D2:3A:F7:B1:E8:18`, pont USB↔SWD `C5F0E209`) est
flashée avec le firmware décrit ci-dessous (§ Fichiers exacts), flash
vérifié octet-à-octet (`verify_image`, 116296 octets, aucune erreur).
Code de diagnostic `LDSWCONFIG` retiré après lecture (registre confirmé
à sa valeur de reset, voir § Pistes écartées) et `CONFIG_SERIAL`
repassé à `n` en dur -- **aucune mesure PPK2 encore reprise sur cet
exact binaire, mais aucun changement fonctionnel depuis la dernière
mesure confirmée (3,28 µA)**, seul du code de lecture diagnostique a été
ajouté puis retiré. `imu_vdd`/LDO1 est déclaré dans l'overlay (mode LDO
forcé par sécurité, jamais activé) mais **`regulator_enable()` n'est
appelé nulle part dans `main.c`** -- IMU non alimenté, aucun risque de
tension.

**Dépôt git** (`C:\ncs\projects`, dépôt séparé du checkout NCS) :
modifications non committées sur `Transition-nRF54LM20A-Optimisation-Consommation.md`,
`xiao_door_sensor/{boards/*.overlay,prj.conf,src/main.c}`,
`xiao_nrf54lm20a_project_notes.md` (ce dernier modifié par une session
antérieure, pas par celle-ci). Fichiers non trackés : ce document, un
rapport PDF (`nrf54lm20a-poweroff-report.pdf`), plusieurs dossiers
`build_test*`/`.bak` -- artefacts de sessions précédentes, sans rapport
avec les travaux de cette session. **Rien n'a été commité** -- à faire
sur demande explicite uniquement.

## Ce qui EST dans le firmware actuellement flashé

- Base System OFF validée (test #13, 3,01-3,02 µA) : LED déconnectées,
  flash SPI externe suspendu (driver + bus + broches GPIO brutes),
  régulateurs `power_en`/`LDO1` sans `regulator-boot-on`.
- **GRTC** : réveil périodique (`z_nrf_grtc_wakeup_prepare()`).
- **RAM retenue** : `packet_id` + échéance de la prochaine trame B,
  CRC-validée.
- **BLE/BTHome, trame B uniquement** (batterie %, tension, température à
  0) : identité BLE fixe, advertising non connectable.
- Console désactivée en dur (`CONFIG_SERIAL=n`).
- Intervalle trame B : **15 min** (valeur de production).

## Ce qui N'est PAS dans le firmware actuel

- **Réveil IMU** (détection de mouvement) -- bloqué, voir § Résultat de
  l'investigation IMU ci-dessous.
- Trame A (mouvement, pitch/roll/yaw) et trame C (accel/gyro brut) --
  dépendent toutes les deux de données IMU.
- Réveil bouton -- explicitement retiré des objectifs (décision
  utilisateur, 2026-08-27).
- Chute/choc, double-tap, yaw -- jamais implémentés dans cette lignée de
  firmware (implémentés dans l'ancien firmware de production, git commit
  `e9828ca`, à réutiliser une fois le réveil IMU résolu).

## Résultat de l'investigation IMU -- cause isolée au régulateur, PAS résolue

**🔴 L'objectif du projet reste 5-6 µA en continu, comportement complet
-- non négociable. Le fait d'avoir isolé la cause au régulateur
`imu_vdd`/LDO1 ne veut PAS dire que ~250 µA est accepté comme coût
inévitable : c'est une configuration à corriger, pas une limite
physique (la référence Seeed officielle prouve qu'une carte identique
avec l'IMU actif descend beaucoup plus bas -- voir plus loin).**

Objectif : ajouter le réveil IMU (INT1, seuil 31 mg, **jamais modifié**
sur consigne explicite) à la base System OFF. Résultat mesuré
**systématiquement ~250-260 µA** (attendu ~12 µA = 3 µA base + 9 µA IMU
datasheet), à travers **9 variantes indépendantes** :

| # | Variable testée | Résultat |
|---|---|---|
| 1 | Attente que INT1 redescende avant `sys_poweroff()` (jusqu'à 5 s) | Aucun effet |
| 2 | `WAKE_UP_DUR` 0x00→0x01 (debounce, registre distinct du seuil 31 mg) | Aucun effet |
| 3 | Délai de stabilisation 200 ms avant armement de l'interruption | Aucun effet |
| 4 | Bug UARTE (console non suspendue) corrigé (`CONFIG_SERIAL=n`) | Aucun effet |
| 5 | Suspension explicite du bus I2C (`PM_DEVICE_ACTION_SUSPEND`) | Aucun effet |
| 6 | Mode bas-consommation accéléromètre (`XL_HM_MODE`) | **Vérifié correct par lecture directe du registre** (`CTRL6_C=0x10`, bit posé) -- pas la cause |
| 7 | Aucune source de réveil armée du tout (IMU alimentée/initialisée seulement) | **Toujours ~260 µA** -- élimine tout mécanisme de réveil comme cause |
| 8 | Régulateur `power_en` désactivé (jamais activé, seul `imu_vdd`/LDO1 actif) | **Toujours ~260 µA** |
| 9 | **`imu_vdd`/LDO1 SEUL** (un seul `regulator_enable()`, aucun I2C vers l'IMU, aucun capteur, base = GRTC+RAM+BLE 3,3 µA) | **~253 µA -- cause isolée, reproductible, réversible (retrait -> retour exact à 3,3 µA)** |
| 10 | Mode Load Switch forcé explicitement (confirmé par lecture registre `LDSW1LDOSEL=0x00`, risque tension accepté par l'utilisateur, IMU exposé à ~4,2 V) | **275,67 µA -- PIRE qu'en LDO. Écarté : ni plus économe, ni sûr.** |

**Cause isolée : l'appel `regulator_enable()` sur `imu_vdd`/LDO1 lui-même
suffit** -- indépendamment de tout ce qui est connecté derrière (I2C,
capteur, réveil). Code vérifié ligne par ligne, aucun bug de
séquencement.

**Recherche externe menée** (errata Nordic, datasheet nPM1300, pinctrl du
board, DevZone, ST community, GitHub Zephyr, driver Zephyr local) :
détail complet dans `Transition-nRF54LM20A-Optimisation-Consommation.md`
§ « Isolation régulateur imu_vdd/LDO1 ». Points clés :

- **Datasheet nPM1300 (§6.4 LOADSW/LDO)** : aucune spécification de
  courant de repos documentée pour ce bloc, ni en LDO ni en Load Switch.
- **🔴 Divergence trouvée avec la référence officielle Seeed**
  (`wiki.seeedstudio.com/xiao_nrf54lm20a_with_onboard/`, IMU via LDO1) :
  leur overlay ne fixe **jamais** `regulator-initial-mode =
  <NPM13XX_LDSW_MODE_LDO>` -- ligne présente dans notre overlay (héritée
  du firmware de production, jamais remise en question avant cette
  session). Le driver Zephyr local
  (`regulator_npm13xx.c`) sépare la sélection de tension (`VOUTSEL`,
  écrite dès que min/max-microvolt sont présents) de la sélection de
  mode (`LDOSEL`, écrite uniquement par `regulator-initial-mode`) --
  forcer le mode LDO pourrait activer une boucle de régulation active là
  où le mode par défaut (probablement Load Switch, RDSON ~200 mΩ
  typique) suffirait. Pages Seeed : ~4,76-4,93 µA System OFF documentés
  pour la carte (à confirmer si IMU inclus).

**Mode LDO vs Load Switch tranché** : LDO (253 µA) reste meilleur que
Load Switch (275,67 µA, en plus dangereux pour l'IMU) -- ni l'un ni
l'autre n'atteint l'objectif. Le mode du régulateur n'est donc pas la
cause principale du ~250 µA -- cause toujours non élucidée après cette
double vérification par lecture registre réelle.

## 🔵 Pistes écartées le 2026-08-27 (recherche approfondie)

- **Registre `LDSWCONFIG`** (soft-start, active discharge) : lu par I2C
  direct, confirmé à `0x00` (valeur de reset) sur le build actuel où
  `regulator_enable()` n'est jamais appelé. Rien dans le code ne l'écrit
  -- écarté.
- **Errata [40]/[41] nPM1300** (chute VSYS/reset au démarrage LDO) :
  décrivent des transitoires de tension (limite courant VBUS, mode BUCK
  forcé PFM) et des resets -- pas notre symptôme (pas de reset, pas
  d'alimentation VBUS, carte sur batterie). Écartées.

## 🔵 Piste actuelle -- errata Nordic nPM1300 [38], EN ATTENTE D'APPROBATION AVANT TEST

**Recherche menée le 2026-08-27** : l'errata officiel du **nPM1300
lui-même** (jamais consulté avant -- seul celui du SoC nRF54LM20A
l'avait été) contient l'anomalie **[38] LOADSW/LDO : LDO startup time
exceeds specification**
(`docs.nordicsemi.com/bundle/errata_nPM1300_Rev1`) :

- **Condition documentée** : *« quand les BUCK sont désactivés ou sans
  charge et qu'il n'y a pas de communication TWI active »* -- correspond
  exactement aux conditions du test #9 (LDO1 seul, aucun trafic I2C
  après activation, BUCKs sans charge réelle en System OFF).
- **Conséquence documentée** : la tension de sortie du LDO monte très
  lentement au lieu d'atteindre la tension configurée dans le temps
  typique.
- **Workaround Nordic** : *« After enabling the LDO, trigger any TWI
  command »*.
- Le driver Zephyr (`regulator_npm13xx_enable()`, PR #83790, mergée
  2025-01-29) applique déjà CE workaround par défaut dans notre
  configuration : `k_msleep(2)` + une seule lecture du registre
  `LDSWSTATUS` juste après l'activation.

**Ce qui n'est PAS vérifié** : si une seule lecture, 2 ms après
l'activation, suffit réellement à faire sortir le régulateur de l'état
de démarrage lent dans notre cas précis, ou si une fenêtre de
communication I2C plus longue/répétée est nécessaire. Le symptôme
mesuré (~250-275 µA parfaitement stable sur des fenêtres de plusieurs
minutes, pas de décroissance observée) est cohérent avec un régulateur
resté bloqué indéfiniment dans son état de démarrage lent plutôt qu'une
rampe qui finirait par se stabiliser d'elle-même. Analogie de
renforcement : les errata BUCK apparentées ([27] +1 mA, [31] +300 µA)
suivent le même mécanisme (absence de TWI = régulateur bloqué dans un
mode transitoire plus gourmand), réglées par une simple lecture/écriture
I2C.

**Test proposé** (aucun nouveau risque matériel -- même tension 3,3 V,
même mode LDO déjà validé sûr ; seul changement : plus de transactions
I2C juste après l'activation) :

1. Reprendre exactement le test #9 (`imu_vdd`/LDO1 seul activé, base
   GRTC+RAM+BLE, aucun capteur/IMU).
2. Juste après `regulator_enable()`, ajouter une boucle de lectures I2C
   répétées du registre `LDSWSTATUS` (au-delà de l'unique lecture déjà
   intégrée par le driver à 2 ms) -- proposition : 20 lectures espacées
   de 5 ms (fenêtre de 100 ms).
3. Mesurer le courant au repos au PPK2 dans cet état (identique au
   protocole des tests précédents).

**Test #11 -- résultat : 253 µA, identique au test #9, aucun effet.**
Errata [38] écartée comme explication (voir
`Transition-nRF54LM20A-Optimisation-Consommation.md` pour le détail).

## 🔵 Piste actuelle -- microphone PDM partage le rail imu_vdd, EN COURS DE TEST

**Découverte (2026-08-27)** : `imu_vdd` et `dmic_vdd` sont le **même
nœud devicetree** (`imu_vdd: dmic_vdd: LDO1`, vendor Seeed) -- LDO1
alimente donc AUSSI le microphone PDM embarqué (`MSM261D3526H1CPM`,
confirmé via le datasheet officiel Seeed), jamais touché par ce firmware
(`pdm20` reste `status = "disabled"`, hors périmètre du projet).
`PDM_CLK` (P1.13) n'est donc jamais piloté -- broche flottante dès que
`imu_vdd` est activé.

Datasheet MSM261D3526H1CPM, diagramme d'états : seul `VDD = 0 V`
garantit un courant bas ("Powered Down"). Une fois VDD appliqué, le
courant dépend du mode déterminé par l'horloge : **Sleep Mode (`fCLOCK
≤ 50 kHz`) : 1 µA typ** contre **Low-Power Mode (150-900 kHz) : 290 µA
typ** et **Standard Performance (1,1-4 MHz) : 670 µA typ**. Aucune
garantie de tomber en Sleep avec une horloge flottante/indéfinie.
**253-275 µA mesurés est très proche du Low-Power Mode (290 µA typ),
pas du Sleep Mode (1 µA typ)** -- correspondance quantitative forte.

**Test #12 -- résultat : 253 µA, exactement identique aux tests #9 et
#11.** Piloter `PDM_CLK` à un niveau bas défini n'a aucun effet -- le
microphone partagé n'est pas la cause (du moins pas via une horloge
flottante).

**Observation transversale importante** : trois modifications
logicielles indépendantes (fenêtre TWI étendue, broche PDM_CLK pilotée,
aucune des deux) donnent **exactement la même valeur, 253 µA**, à
chaque fois -- pas de variation, même à la deuxième décimale rapportée.
Seul le changement de **mode du régulateur** (LDO → Load Switch,
test #10) a fait varier la valeur (253 → 275,67 µA). Ceci pointe vers
une cause **interne au bloc LOADSW/LDO du nPM1300 lui-même** (boucle de
régulation active, réseau de contre-réaction) plutôt que vers la charge
en aval (IMU ou microphone) -- une charge en aval mal configurée
donnerait normalement une valeur plus sensible à ce qui est fait côté
SoC.

## 🔵 Piste écartée -- caractéristiques électriques du bloc LOADSW/LDO nPM1300

**Vérifié directement le 2026-08-27** sur le datasheet officiel nPM1300
Product Specification v1.1 (4490_483, 2024-06-16), Table 23 (LOADSW
electrical specification, p.72) et Table 24 (LDO electrical
specification, p.73) : **aucun paramètre de courant de repos n'est
documenté pour ce bloc**, ni en mode Load Switch ni en mode LDO --
seulement RDSON (200 mΩ typ), courant de sortie max (100 mA LS / 50 mA
LDO), temps de soft-start (1,8 ms typ), résistance de pull-down/active
discharge (2 kΩ typ) et plages de tension (VIN_LDO 2,6 V-VSYS,
VOUT_LDO 1,0-3,3 V). Piste définitivement close : Nordic ne spécifie
tout simplement pas ce courant dans sa documentation officielle.

## Bilan de l'investigation régulateur (2026-08-27, fin de session)

**13 tests indépendants** ont isolé et caractérisé le ~250-275 µA sans
en trouver la cause corrigible :
- Cause isolée à `regulator_enable()` sur `imu_vdd`/LDO1 lui-même
  (tests #7-9), indépendamment de l'IMU, du bus I2C, du mode
  bas-consommation accéléromètre.
- Mode LDO (253 µA) vs Load Switch (275,67 µA, test #10) : le mode
  change la valeur -- pointe vers le régulateur lui-même, pas la charge
  en aval.
- Fenêtre TWI étendue (test #11), broche PDM_CLK pilotée (test #12) :
  **aucun effet, valeur strictement identique (253 µA)** à chaque fois
  -- écarte l'errata [38] et le microphone partagé comme causes.
- Registre `LDSWCONFIG` : confirmé à sa valeur de reset (0x00).
- Tables électriques officielles LOADSW/LDO : aucun courant de repos
  documenté.
- Base sans régulateur reconfirmée stable à 3,3 µA (test #13) -- aucune
  dérive de mesure.

**Prochaine étape recommandée** : contacter le support Nordic DevZone
avec le symptôme exact et reproductible (voir brouillon ci-dessous) --
piste identifiée depuis le début de cette investigation, jamais encore
exécutée. Aucune autre piste locale (code, datasheet, errata, driver)
n'a été trouvée après recherche approfondie.

### Brouillon de question DevZone (prêt, à publier par l'utilisateur)

> **Titre** : nPM1300 LOADSW1/LDO1 draws ~250 µA in LDO mode with no
> load and no I2C traffic after enable -- reproducible, isolated to
> `regulator_enable()` itself
>
> Board: Seeed XIAO nRF54LM20A Sense (nPM1300 + nRF54LM20A), NCS 3.4.0,
> Zephyr regulator/MFD driver (`regulator_npm13xx.c`).
>
> Baseline (SoC in System OFF, GRTC wake + BLE advertising burst only,
> no LDSW/LDO active): **3.3 µA** measured (PPK2, BAT+/BAT-, USB-C
> disconnected).
>
> Enabling LOADSW1/LDO1 alone (`regulator_enable()`, mode forced to
> LDO via `regulator-initial-mode`, VOUT = 3.3V, `regulator-min/max-
> microvolt = 3300000`) with **nothing else changed** -- no I2C traffic
> to any device on the LDO1 rail, no sensor reads, immediately followed
> by `sys_poweroff()` -- current jumps to **~253 µA** and stays
> perfectly stable over multi-minute measurement windows.
>
> Tested and ruled out:
> - Extending I2C/TWI activity after enable (20x reads over 100 ms,
>   beyond the errata [38] single-read workaround already in the
>   driver): no change (253 µA).
> - Forcing Load Switch mode instead of LDO: worse (275.67 µA).
> - `LDSWCONFIG` (soft-start/active-discharge): confirmed at reset
>   value (0x00) via direct register readback.
> - Datasheet Table 23/24 (LOADSW/LDO electrical specification): no
>   quiescent current figure documented for either mode.
>
> Is there a known quiescent/ground current for the LOADSW/LDO block in
> LDO mode that simply isn't in the datasheet's electrical
> specification tables? Or a configuration step (beyond mode/voltage/
> soft-start/discharge, all already checked) required to reach a low
> idle current in LDO mode with no load?

## Test #14/#15 (2026-08-27)

- **Test #14** : `imu_vdd`/LDO1 activé sans mode forcé (`regulator-
  initial-mode` retiré de l'overlay) -- **250 µA**, cohérent avec les
  253/275 µA précédents.
- **Test #15** : question utilisateur -- LSM6DS3TR-C activé (accéléro
  104 Hz, `CTRL1_XL=0x40`, mode basse consommation `CTRL6_C=0x10`) puis
  toutes les interruptions explicitement coupées (`INT1_CTRL`,
  `INT2_CTRL`, `MD1_CFG`, `MD2_CFG`, `TAP_CFG` = 0x00). **Résultat :
  300 µA.** Incrément d'environ 47-50 µA par rapport au régulateur seul
  (250-253 µA), cohérent avec le courant de fonctionnement normal d'un
  accéléromètre en mode basse consommation à 104 Hz -- **les
  interruptions désactivées n'ajoutent pas de courant supplémentaire
  détectable**. Le ~250 µA de base (régulateur seul) reste le facteur
  dominant, toujours inexpliqué.

## Test #16 (2026-08-27) -- alignement sur l'exemple officiel Seeed

Source : `wiki.seeedstudio.com/xiao_nrf54lm20a_with_onboard/` (exemple
`imu_click`, réveil IMU fonctionnel d'après le fournisseur). Comparaison
avec notre code : différences trouvées --

1. **`regulator-boot-on;`** sur `imu_vdd`/LDO1 (nous l'avions retiré,
   activation manuelle via `regulator_enable()` à la place).
2. **`zephyr,deferred-init;`** sur le nœud `lsm6ds3tr_c` -- reporte
   l'initialisation automatique du driver (normalement lancée en
   `SYS_INIT`/`POST_KERNEL`, donc **avant `main()`**, donc avant tout
   `regulator_enable()` explicite -- échoue systématiquement et
   silencieusement faute d'alimentation). Seeed déclenche l'init
   explicitement via `device_init()` une fois l'IMU sous tension.
3. **`CONFIG_LSM6DSL_TRIGGER_GLOBAL_THREAD=y`** (nous avions
   `TRIGGER_NONE` -- aucune interruption réellement servie).

Configuration appliquée à l'identique (voir § Fichiers exacts) :
overlay (`regulator-boot-on` restauré + `zephyr,deferred-init` ajouté),
`prj.conf` (`CONFIG_LSM6DSL_TRIGGER_GLOBAL_THREAD=y`), `main.c`
(`device_init(lsm6dsl_dev)` remplace le `regulator_enable()` manuel et
les écritures I2C brutes du test #15).

**Vérification fonctionnelle** (diagnostic série temporaire) :
`device_init` réussit (`rc=0`, `device_is_ready()=1`) -- WHO_AM_I
confirmé, driver pleinement initialisé (bas niveau + interruption)
pour la première fois de cette investigation.

**Mesure de courant** : firmware reflashé et vérifié (114512 octets,
console désactivée), **résultat en attente**.

## Reprendre ensuite

Une fois la cause du régulateur résolue (ou cette piste également
écartée) : réintégration I2C/capteur/réveil IMU, puis trames A/C, puis
comparaison du courant final à l'objectif 5-6 µA.

## Fichiers exacts actuellement flashés (vérifiés octet-à-octet)

Board : `xiao_nrf54lm20a/nrf54lm20a/cpuapp`, board root vendor Seeed
(`C:/ncs/vendor/platform-seeedboards/zephyr`), NCS 3.4.0.

Chemin du projet : `nRF54LM20A/xiao_door_sensor/` (réorganisation par
composant, 2026-08-27 -- `BOARD_ROOT` dans `CMakeLists.txt` corrigé en
conséquence, un niveau `../` de plus).

### `boards/xiao_nrf54lm20a_nrf54lm20a_cpuapp.overlay`

```dts
&power_en {
	/delete-property/ regulator-boot-on;
};

#include <zephyr/dt-bindings/regulator/npm13xx.h>

/* Test #16 -- aligne sur l'exemple officiel Seeed (imu_click) */
&pmic {
	regulators {
		imu_vdd: LDO1 {
			regulator-boot-on;
			regulator-min-microvolt = <3300000>;
			regulator-max-microvolt = <3300000>;
		};
	};
};

/* Init differee du driver LSM6DSL -- declenchee explicitement dans
 * main.c via device_init() une fois l'IMU sous tension. */
&lsm6ds3tr_c {
	zephyr,deferred-init;
};

/ {
	cpuapp_sram@2007ec00 {
		compatible = "zephyr,memory-region", "mmio-sram";
		reg = <0x2007ec00 DT_SIZE_K(4)>;
		zephyr,memory-region = "RetainedMem";
		status = "okay";

		retainedmem0: retainedmem {
			compatible = "zephyr,retained-ram";
			status = "okay";
		};
	};

	aliases {
		retainedmemdevice = &retainedmem0;
	};
};

&cpuapp_sram {
	reg = <0x20000000 DT_SIZE_K(507)>;
	ranges = <0x0 0x20000000 0x7ec00>;
};

&pmic_leds {
	status = "disabled";
};

&py25q64 {
	status = "okay";
};

&usbhs {
	status = "disabled";
};

&usbhs_wrapper {
	status = "disabled";
};
```

### `prj.conf`

```ini
CONFIG_SERIAL=n
CONFIG_CONSOLE=n
CONFIG_UART_CONSOLE=n
CONFIG_PRINTK=n
CONFIG_BOOT_BANNER=n
CONFIG_NCS_BOOT_BANNER=n
CONFIG_GPIO=y
CONFIG_SPI=y
CONFIG_FLASH=y
CONFIG_SPI_NOR=y
CONFIG_PM_DEVICE=y
CONFIG_PM_DEVICE_RUNTIME=y
CONFIG_POWEROFF=y
CONFIG_HWINFO=y

CONFIG_BT=y
CONFIG_BT_BROADCASTER=y
CONFIG_BT_DEVICE_NAME="XIAO-DOOR"

CONFIG_RETAINED_MEM=y
CONFIG_CRC=y

CONFIG_SENSOR=y
CONFIG_NPM13XX_CHARGER=y
CONFIG_MFD=y

CONFIG_REGULATOR=y

# Test #16 -- driver LSM6DSL avec interruption reellement servie
CONFIG_I2C=y
CONFIG_LSM6DSL=y
CONFIG_LSM6DSL_TRIGGER_GLOBAL_THREAD=y

CONFIG_MAIN_STACK_SIZE=4096
CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE=2048

CONFIG_REBOOT=y
```

### `src/main.c`

Fichier complet conservé tel quel dans le dépôt du projet
(`xiao_door_sensor/src/main.c`). Structure de `main()` : lecture
reset_cause → détermine `cold_boot`/`fresh_session` → `retained_load()`
→ LED déconnectées → `device_init(lsm6dsl_dev)` (init différée IMU,
test #16) → identité BLE fixe → `bt_enable()` → si `health_due` :
`send_frame_b()` (lecture batterie PMIC + rafale BLE 700 ms) →
suspension flash externe → `retained_save()` → calcule le délai avant
la prochaine échéance → `z_nrf_grtc_wakeup_prepare()` →
`sys_poweroff()`.

Fonctions clés : `retained_load()`/`retained_save()` (CRC32, région RAM
retenue), `set_fixed_ble_identity()`, `read_battery()`/
`voltage_to_percent()` (PMIC nPM1300), `send_frame_b()`/
`advertise_burst()` (BTHome v2, trame B uniquement),
`next_health_deadline()`. `imu_vdd`/LDO1 n'est plus activé
manuellement (`regulator-boot-on` s'en charge) ; `lsm6dsl_dev` est le
seul point d'entrée IMU (`device_init()`, test #16).

## Procédure de test (rappel, protocole du projet)

1. Vérifier la connexion USB avant toute action (`Get-PnpDevice`,
   chercher `USB\VID_2886&PID_0068\C5F0E209` statut `OK` pour l'unité
   #01).
2. Build + flash (`nrf54lm20a-load`) + **vérifier avec `verify_image`**
   (jamais `dump_image`+`cmp`, faux positifs sur les trous RRAM ;
   reflasher le même fichier si `verify_image` échoue, déjà arrivé,
   n'est pas systématique).
3. PPK2 sur BAT+/BAT- UNIQUEMENT après déconnexion complète de l'USB-C
   (jamais les deux simultanément).
4. Vérifier `prj.conf` (`CONFIG_SERIAL=n`) avant tout flash destiné à
   une mesure PPK2 -- voir règle absolue en tête de document.

## Pour reprendre dans une nouvelle conversation

1. Lire ce document en entier.
2. Présenter le § Plan proposé à l'utilisateur pour approbation avant
   toute nouvelle mesure ou tout nouveau flash.
3. Une fois approuvé, exécuter les étapes une à une, avec mesure PPK2
   après chacune, en respectant la règle absolue `CONFIG_SERIAL=n`.
4. Consigner le résultat de chaque étape ici au fur et à mesure (pas
   dans `Transition-nRF54LM20A-Optimisation-Consommation.md`, réservé à
   l'historique détaillé si besoin de retrouver le raisonnement complet).
