# État + plan -- System OFF + GRTC + RAM retenue + BLE (sans IMU)

**Document de reprise pour une nouvelle conversation — à ouvrir en
premier.** Mis à jour le 2026-08-28. Ne contient que l'état actuel
vérifié et un plan en attente d'approbation -- pas de narration
d'essais/erreurs (l'historique complet de diagnostic reste dans
`Transition-nRF54LM20A-Optimisation-Consommation.md`, non dupliqué ici).

## 🟢 PIVOT D'ARCHITECTURE (2026-08-28) -- À LIRE EN PREMIER

**L'architecture "System OFF + redémarrage complet à chaque cycle de
sondage" (tests #22-31 ci-dessous) est abandonnée comme cible finale.**
Elle a un plancher mesuré ~70-144 µA (redémarrage complet = réinit
MFD/régulateur/chargeur/BLE à chaque cycle, incompressible avec cette
approche -- voir § Tests #22-31 et § MFD deferred-init pour le détail).

**Remplacée par : System ON IDLE + réveil par comparaison GRTC (pas de
reboot).** Justification chiffrée, trouvée dans le datasheet Nordic
officiel (`docs/nRF54LM20A_nRF54LM20B_Datasheet_v1.0.pdf`) :

- **Page 1 (tableau "Power consumption highlights")** : *System ON IDLE
  avec GRTC (XOSC) et 512 KB RAM = **4,3 µA*** -- sous l'objectif 5-6 µA,
  AVANT même d'ajouter le coût du sondage IMU par-dessus. À comparer à
  System OFF + réveil GRTC (1,0 µA) et System OFF (0,7 µA) -- l'écart
  entre les deux approches n'est pas dans le mode d'alimentation matériel
  lui-même, mais dans le fait qu'un cycle System OFF classique réinitialise
  tout à chaque réveil (RAM/périphériques non retenus), alors que System ON
  IDLE ne fait qu'endormir le CPU (WFI), RAM et périphériques restant
  configurés.
- **Page 304 (GPIOTE) et page 317 (GRTC)** : System ON IDLE est un sommeil
  CPU par WFI/WFE -- exactement ce que fait Zephyr automatiquement en idle
  "tickless" (`CONFIG_PM=y`). Nordic documente explicitement la procédure
  recommandée (p.317, § « Entering System OFF mode ») : programmer un
  événement de comparaison GRTC (`CC[n]`), endormir le CPU, et **si
  l'événement de comparaison se déclenche → réveiller le CPU par
  interruption SANS passer par System OFF**.
- Cette architecture est **exactement celle déjà validée sur le projet
  frère nRF52840** (`CONFIG_PM=y`, boucle `main()` avec `k_sleep()` entre
  cycles de sondage ~1s, ~10 µA mesurés) -- voir
  `nRF52840/Transition-nRF52840-Optimisation-Consommation.md`. Les deux
  pistes listées plus bas (§ Décider maintenant, désormais résolue)
  convergent donc sur la même réponse : ce n'est plus une question de
  choisir entre les deux, la datasheet confirme que la piste nRF52840 est
  réalisable sur ce SoC avec un plancher matériel documenté sous
  l'objectif.

**Ce qui doit changer dans `main.c`** : remplacer le cycle
`z_nrf_grtc_wakeup_prepare()` + `sys_poweroff()` (redémarrage complet à
chaque réveil) par une boucle `main()` infinie avec `CONFIG_PM=y` actif et
un sommeil basé sur GRTC CC compare (ou `k_sleep()`, qui s'appuie dessus
via le driver de timer système Zephyr) -- le SoC ne redémarre plus jamais
tant que l'appareil reste alimenté. Les optimisations déjà validées
(chargeur nPM1300 en init différée, trame BLE santé uniquement si due)
restent valables et doivent être conservées : elles réduisent un coût qui
existe indépendamment de l'architecture de réveil.

**Ce qui reste inchangé** : ne jamais laisser `imu_vdd`/LDO1 actif en
continu (~250-300 µA, cause non résolue -- voir § Résultat de
l'investigation IMU) ; le sondage IMU doit rester bref et cyclique
(allumage → lecture → extinction), maintenant à l'intérieur d'un cycle
System ON IDLE au lieu d'un cycle System OFF.

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

## Test #18 -- reproduction stricte de l'exemple Seeed (2026-08-27)

Documenté séparément :
`Test-Seeed-imu_click-Reproduction-Stricte.md`. Résumé : le code
officiel Seeed (`imu_click`), exécuté sans modification, donne
~320-350 µA -- même ordre de grandeur que nos propres mesures. Le
~250-300 µA associé à `imu_vdd`/LDO1 actif n'est donc **pas propre à
notre firmware**.

## 🔵 Plan en cours -- PAS DE CONCLUSION DE STRATÉGIE À CE STADE

1. ✅ Documenter le test #18 (reproduction stricte) -- fait,
   `Test-Seeed-imu_click-Reproduction-Stricte.md`.
2. ✅ Optimiser `xiao_seeed_imu_click` : LED retirée (test #19),
   `CONFIG_PM_DEVICE`/`CONFIG_PM_DEVICE_RUNTIME` ajoutés (test #20).
3. ✅ Mesuré : 319,48 µA (test #19) puis 315 µA (test #20) -- aucun
   effet mesurable par rapport aux 325-333 µA de référence.
4. ✅ Documenté séparément --
   `Test-Seeed-imu_click-Optimisations.md`.
5. ✅ Test #21 -- vrai `sys_poweroff()` implémenté (réveil GPIO sur
   INT1, même mécanisme que l'exemple officiel Zephyr `system_off`).
   Fonctionnellement vérifié (le SoC exécute réellement l'appel
   matériel de mise hors tension). **Mesuré : 312 µA -- aucune
   baisse.** Cause identifiée dans le code : `imu_vdd`/LDO1 n'est
   jamais désactivé avant `sys_poweroff()`, et le régulateur du
   nPM1300 reste actif indépendamment du System OFF du SoC (fait déjà
   établi). Documenté séparément -- `Test-Seeed-imu_click-System-OFF.md`.
6. **Implication** : pour qu'une interruption IMU reste possible,
   `imu_vdd` doit rester actif en continu -- ce qui coûte ~250-300 µA
   sur ce matériel, indépendamment de tout ce que fait le SoC. Un vrai
   System OFF du SoC seul ne suffit donc pas.

## Tests #22-30 -- architecture sondage periodique, xiao_door_sensor (2026-08-28)

Implémentation directe sur le projet de production (pas un projet
isolé) de la stratégie "sondage périodique, pas d'interruption
asynchrone" (option retenue après examen du nRF52840 -- voir
ci-dessous). Réveil GRTC court (1-2 s) au lieu de 15 min, `imu_vdd`
allumé/lu/éteint à chaque cycle plutôt que laissé actif en continu.

**Progression mesurée** (charge par cycle en µC, plus fiable que la
moyenne brute pour comparer -- indépendante de l'intervalle) :

| Test | Changement | Moyenne | Charge/cycle |
|---|---|---|---|
| #22/23 | Sondage IMU actif, BLE corrigé (santé seulement si due) | ~180 µA | ~180 µC |
| #24 | Sondage IMU désactivé (isolation) | ~99 µA | ~99 µC |
| #25 | + `suspend_external_flash()` désactivé | ~100 µA | ~100 µC (régression : broches SPI flottantes) |
| #26 | Régression corrigée (broches forcées, appels `pm_device_action_run` seuls retirés) | 92,06 µA | ~92 µC |
| #27 | Intervalle 2000 ms (test de linéarité) | 51,68 µA | ~98-103 µC -- **confirme un coût fixe par redémarrage**, pas variable |
| #28 | `k_msleep(20)` retiré | 99,91 µA | ~96 µC -- **aucun effet**, piste écartée |
| #29 | Init différée du driver chargeur nPM1300 (~12-15 écritures I2C évitées sauf si trame santé due) | **69,98 µA** | **~70 µC** |
| #30 | Init différée du régulateur `imu_vdd` tentée | -- | **échec de compilation** : `zephyr,deferred-init` non supporté par le binding `nordic,npm1300-regulator` (limité aux devices top-level type capteur) |
| #31 | `sample_motion()` réactivé sur la base optimisée du test #29 (coût réel complet, sondage IMU inclus) | **144,61 µA** | -- |

**Bilan** : 180 → 70 µA sur la base sans sondage IMU actif (~2,6× de
réduction), via l'identification et le report de deux inits
automatiques coûteuses (BLE, chargeur nPM1300) qui s'exécutaient à
chaque redémarrage sans nécessité. Avec le sondage IMU réactivé
(test #31), 144,61 µA -- encore ~24-29× l'objectif (5-6 µA), ~14×
la référence nRF52840 (~10 µA). Voir § Pivot d'architecture en tête
de document : ce chiffre est celui de l'architecture *abandonnée*
(reboot par cycle), conservé ici pour référence de comparaison avec
la nouvelle architecture (System ON IDLE) une fois implémentée.

**Init différée du MFD (`&pmic`, top-level) -- également fermée
(2026-08-28), raison définitive** : contrairement au régulateur (device
enfant sans `compatible` propre, § test #30), le nœud MFD top-level a
bien son propre `compatible = "nordic,npm1300"` et pourrait en théorie
accepter `zephyr,deferred-init`. Mais le driver régulateur (lui-même
non différable, test #30) s'auto-initialise à la priorité `POST_KERNEL`
-- **avant `main()`** -- et vérifie à ce moment précis que le MFD est
prêt (`device_is_ready(config->mfd)`). Si le MFD est différé, cette
vérification échoue systématiquement, puisque `main()` (seul endroit où
notre code pourrait appeler `device_init()` sur le MFD) ne s'exécute
qu'après tous les inits `POST_KERNEL` -- donc toujours trop tard pour
le régulateur. Différer le MFD casserait le régulateur sans espoir de
contournement applicatif. **Les ~3 écritures I2C du MFD (config
ship-mode) et les ~1-2 du régulateur sont donc incompressibles avec
cette architecture (driver Zephyr standard + reboot complet par
cycle)** -- ce qui confirme, indépendamment de la datasheet, que le
plancher ~70 µA de l'architecture reboot-par-cycle est structurel et
non un simple manque d'optimisation restant à trouver.

## 🟢 Décision (2026-08-28) -- résolue par la datasheet, voir § Pivot d'architecture

Les deux options listées précédemment (sondage périodique à faible
rapport cyclique, vs. porter la stratégie nRF52840) **ne sont pas des
alternatives concurrentes : elles convergent sur la même architecture**.
« Porter la stratégie nRF52840 » = System ON IDLE + boucle de sondage
(pas de reboot) ; « faible rapport cyclique pour l'IMU » reste nécessaire
*à l'intérieur* de cette boucle (le rail `imu_vdd` doit quand même être
allumé brièvement puis rééteint à chaque échantillon, indépendamment de
l'état du SoC -- voir § Résultat de l'investigation IMU, cause non
résolue). La datasheet Nordic confirme que cette architecture combinée a
un plancher matériel de 4,3 µA sur ce SoC précis, avant le coût du
sondage IMU. Plus de décision à prendre ici -- voir § Pivot
d'architecture en tête de document pour le plan d'implémentation.

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

## 🟢 Tests #32/#33 (2026-08-28) -- System ON IDLE implémenté et mesuré, unité #01

Architecture System ON IDLE implémentée dans `xiao_door_sensor` (`main.c`
en boucle infinie, `CONFIG_PM=y`, plus de `sys_poweroff()`/reboot par
cycle -- voir § Pivot d'architecture en tête de document pour le
raisonnement). `bt_enable()` appelé une seule fois au démarrage.
`sample_motion()` réécrit désormais par I2C direct `CTRL3_C`
(BDU+IF_INC) et `CTRL6_C` (XL_HM_MODE) à chaque cycle : nécessaire car
`device_init()` (kernel Zephyr, `kernel/device.c`) ne ré-exécute plus
`lsm6dsl_init_chip()` après le premier appel, alors que `imu_vdd` est
coupé/rallumé à chaque cycle (donc la puce physique perd sa
configuration) -- sans quoi les lectures X/Y/Z deviendraient
incohérentes en silence à partir du 2e cycle.

**Mesures PPK2, unité #01, XIAO au repos (fenêtre 10 s sauf mention) :**

| Test | Changement | Moyenne 10s | Charge/cycle (salve IMU isolée) |
|---|---|---|---|
| Pivot initial | System ON IDLE, `k_msleep(20)`+`k_msleep(15)` (valeurs héritées, non justifiées par un spec) | 30,85 µA (308,53 µC) | 42,76 ms / 24,20 µC |
| #32 | Stabilisation régulateur réduite 20→5 ms (marge ~2,8× au-dessus du soft-start nPM1300 documenté, 1,8 ms typ, Table 24) | 27,07 µA (270,66 µC) | 27,03 ms / 19,49 µC |
| #33 | Attente ODR réduite 15→10 ms (marge ~0,4 ms au-dessus de la période réelle 104 Hz, ~9,6 ms) | 25,45 µA (254,51 µC) -- **fenêtre non comparable, contient une salve BLE ponctuelle** (voir note) | 22,52 ms / 17,95 µC |

Concordance mesure/prédiction à chaque étape : test #32 prédit
~3,7-4,1 µC/cycle économisés (15 ms × ~250-275 µA), mesuré −3,79 µC/cycle
sur la fenêtre 10s -- quasi exact. Test #33 prédit ~1,4 µC économisé
(5 ms × ~275 µA), mesuré −1,54 µC sur la salve isolée -- concordant.

**Note test #33** : la fenêtre 10 s mesurée juste après un reflash contient
un pic à 87,13 mA, nettement plus haut que les pics de sondage IMU
(~15-20 mA) -- une salve BLE (trame B), déclenchée par
`retained.next_health_us = 0` après reflash (`health_due` vrai dès le
premier cycle). La moyenne 25,45 µA/254,51 µC de cette fenêtre mélange
donc l'effet du changement de code et un événement ponctuel (prochaine
trame B dans ~15 min) -- pas comparable directement à la moyenne 10s du
test #32. Seule la comparaison "charge/cycle, salve IMU isolée" (19,49 →
17,95 µC) est propre entre #32 et #33.

**Bilan** : 70-144 µA (ancienne architecture, reboot par cycle) → ~27 µA
(System ON IDLE, tests #32/#33) -- gain ~2,6-5,3×. La salve `imu_vdd`
reste ~70 % du coût par cycle (~18 µC sur ~25-27 µC), le fond entre les
salves (~6,6-9,8 µA selon les captures) est déjà proche du plancher
datasheet (4,3 µA). Les deux délais de stabilisation resserrés (5 ms
régulateur, 10 ms ODR) approchent maintenant leurs planchers documentés
-- gains supplémentaires par ce seul levier désormais marginaux. Objectif
final (5-6 µA) pas encore atteint ; référence nRF52840 (~10 µA) pas
encore atteinte non plus.

**Mesure propre confirmée** : 25,21 µA / 252,06 µC (10 s, sans salve
BLE, max 24,53 mA cohérent avec les pics IMU seuls) -- confirme
l'amélioration du test #33 par rapport aux 27,07 µA du test #32.

## 🔵 Test #34 (2026-08-28) -- investigation motif ~10 ms sur le plancher idle, deux pistes écartées

Observation sur le zoom µA du plancher entre deux sondages IMU (captures
PPK2 des tests #32/#33) : un motif en dents de scie se répète toutes les
~9-12 ms, sans rapport avec le cycle de sondage IMU (1/s). Deux pistes
de code vérifiées et écartées avec preuve :

1. **Contrôleur BT hôte** : build diagnostic jetable, `bt_enable()`
   jamais appelé (`CONFIG_BT` reste `=y`, le code compile normalement,
   seul l'appel runtime est saute -- voir historique dans
   `main.c` si besoin de retrouver le mécanisme, retiré après ce test).
   Mesuré : 23,22 µA / 232,17 µC (10 s), motif ~10 ms **toujours
   présent**, salve IMU inchangée (~789 µA / 17,45-17,47 µC, cohérent
   avec test #33). Effet réel mais distinct trouvé au passage : avoir
   `bt_enable()` actif coûte ~2 µC/cycle même sans jamais émettre
   (25,21 vs 23,22 µC/cycle) -- petit, mais BLE reste indispensable à la
   fonction du capteur, pas un levier utilisable. **Firmware réel
   restauré et reflashé immédiatement après cette mesure** (unité #01
   refonctionne normalement).
2. **Calibration MPSL** (`CONFIG_MPSL_CALIBRATION_PERIOD`,
   `nrf/subsys/mpsl/init/Kconfig:104-108`) : valeur par défaut **60000 ms
   (60 s)** dans ce build, confirmée dans le `.config` généré -- écart
   d'un facteur 6000 par rapport aux ~10 ms observés, ne peut pas être la
   cause. `CONFIG_CLOCK_CONTROL_NRF_K32SRC_XTAL=y` confirme aussi que
   LFCLK est sur cristal (pas RC) -- la piste "calibration RC" reste hors
   sujet (déjà écartée pour HFINT plus tôt).

**Conclusion** : la source exacte du motif ~10 ms reste dans la partie
fermée de MPSL (bibliothèque binaire Nordic, radio/ordonnancement
d'horloge) -- hors de portée d'une analyse de code statique, nécessite
une instrumentation runtime (RTT, GPIO toggle) non disponible ici.
Investigation arrêtée à ce stade : poids estimé ~2-3 µA sur ~23-25 µA
total, nettement plus petit que le poste dominant restant (la salve
`imu_vdd`, ~17-19 µC sur ~70-80 % du coût par cycle), et les deux délais
de stabilisation de cette salve sont déjà resserrés à leurs planchers
documentés (tests #32/#33).

**Progression cumulée, unité #01, XIAO au repos** :
70-144 µA (ancienne architecture, reboot par cycle) → 30,85 µA (pivot
System ON IDLE) → 27,07 µA (test #32) → **25,21 µA (test #33, état
actuel du firmware flashé)** -- gain ~2,8-5,7× par rapport à l'ancienne
architecture. Objectif final (5-6 µA) et référence nRF52840 (~10 µA) pas
encore atteints.

**Prochaine étape à évaluer** : la salve `imu_vdd` reste le plus gros
poste (~70-80 % du coût par cycle) et ses deux délais de stabilisation
sont déjà proches de leurs planchers documentés -- les gains
supplémentaires par ce seul levier sont désormais marginaux. Pas de
piste de code supplémentaire identifiée à ce stade sans instrumentation
runtime plus poussée.

## 🟢 Test #35 (2026-08-28) -- étude approfondie du datasheet Nordic, RAM power-down

Étude systématique du datasheet officiel
(`docs/nRF54LM20A_nRF54LM20B_Datasheet_v1.0.pdf`, chapitres § Power and
clock management p.69-108, § MEMCONF p.46-48, § RRAMC p.48-68) pour
vérifier si l'architecture logicielle exploite tout ce qui est possible
côté matériel. Trois points **déjà corrects par défaut** (vérifiés,
rien à changer) :

- **Sub-power mode Low-power** (p.69-70) : par défaut à l'entrée en
  System ON, sans action logicielle -- c'est le mode le plus économe
  (l'alternative, Constant Latency, coûte plus cher et n'est utile que
  pour une latence de réveil garantie, pas notre besoin).
- **RRAMC POWER.LOWPOWERCONFIG** (p.61) : reset = `PowerOff` (0) -- la
  RRAM (mémoire programme) est déjà coupée automatiquement en System ON
  IDLE, jamais laissée en `Standby`. Rien à configurer.
- **VREGMAIN (régulateur principal du SoC) en mode DC/DC** (p.106) :
  vérifié déjà activé par le board support Seeed
  (`nrf54lm20a_cpuapp_common.dtsi:52-54`,
  `regulator-initial-mode = <NRF5X_REG_MODE_DCDC>`) -- pas le mode LDO
  de repli, moins efficace. Rien à changer, ni à notre niveau ni au
  niveau du board.

**Un levier concret trouvé et implémenté** : la bibliothèque officielle
NCS `RAM_POWER_DOWN_LIBRARY` (`nrf/lib/ram_pwrdn/`), qui coupe
physiquement les sections RAM (granularité 32 Ko, `MEMCONF`) non
utilisées par l'image liée. **Explicitement supportée pour notre puce
exacte** (`CONFIG_SOC_NRF54LM20A_CPUAPP`, en dur dans
`ram_pwrdn.c:53`) et **jamais activée jusqu'ici**
(`# CONFIG_RAM_POWER_DOWN_LIBRARY is not set`, vérifié dans le
`.config` du build précédent). Notre image n'utilise que 23,5 Ko sur les
507 Ko retenus/alimentés en continu par l'overlay -- soit ~15 sections
sur 16 (~94 % de la RAM) totalement inutilisées mais alimentées quand
même.

Implémenté : `CONFIG_RAM_POWER_DOWN_LIBRARY=y` (`prj.conf`),
`power_down_unused_ram()` appelé une fois dans `main()` juste avant la
boucle `while(1)`, après toute l'init (BLE/MPSL inclus) -- voir
`src/main.c`.

**Risque identifié et vérifié avant mesure** : la doc de la bibliothèque
prévient explicitement qu'un accès à de la RAM coupée cause un bus
fault, et qu'il ne faut pas l'utiliser si l'application dépend
d'allocation dynamique (malloc) qui pourrait déborder dans la zone
coupée. Vérifié : aucun `malloc()`/`k_malloc()` dans ce firmware, pile
BLE Zephyr basée sur des pools `net_buf` statiques (pas de heap
dynamique). **Vérification fonctionnelle faite avant la mesure PPK2**
(build diagnostic temporaire, console réactivée puis remise à `n`
immédiatement après, jamais utilisé pour une mesure) : capture série de
8 s, 16 cycles observés, lectures accéléromètre cohérentes
(`accel x=-6 y=-1 z=7` à chaque cycle), `Bluetooth initialized once`
une seule fois comme attendu, aucune erreur ni redémarrage -- pas de bus
fault.

**Firmware flashé et vérifié** (unité #01, `verified 113136 bytes`).

**Mesure PPK2 confirmée** : 20,63 µA / 206,30 µC (10 s), contre
25,21 µA / 252,06 µC au test #33 -- **−18 % de charge**, cohérent avec
la coupure de ~94 % de la RAM inutilisée (le plancher idle diminue,
comme attendu si la rétention/fuite RAM en était une composante). Un pic
isolé à 73,29 mA observé dans cette capture, bref (largeur ~100-200 µs à
l'échelle du graphe, charge de cette salve 20,11 µC -- dans la fourchette
normale des salves IMU ~17-19 µC) : ne remet pas en cause l'amélioration
mesurée sur la charge totale, cause non élucidée, laissé de côté (pas
assez d'impact pour justifier une investigation dédiée à ce stade).

**Progression cumulée, unité #01, XIAO au repos** :
70-144 µA (ancienne architecture) → 30,85 µA (pivot System ON IDLE) →
27,07 µA (#32) → 25,21 µA (#33) → **20,63 µA (#35, RAM power-down)** --
gain ~3,4-7,0× par rapport à l'ancienne architecture. Objectif final
(5-6 µA) et référence nRF52840 (~10 µA) toujours pas atteints, mais
l'écart continue de se réduire par paliers mesurés.

## 🟢 Test #36 (2026-08-28) -- ODR IMU doublée 104→208 Hz

Objectif : réduire la salve `imu_vdd` (poste dominant, ~70-80 % du coût
par cycle) en raccourcissant l'attente d'une période d'échantillonnage
complète après l'écriture ODR (`k_msleep`), sans toucher au coût fixe du
régulateur lui-même. `sensor_attr_set()` ODR 104→208 Hz (valeur standard
documentée du LSM6DSL en mode bas-consommation, `XL_HM_MODE` inchangé),
attente réduite 10→6 ms (période réelle à 208 Hz ~4,8 ms, marge
~1,2 ms). Vérification fonctionnelle faite avant mesure (console
temporaire, 16 cycles, lectures identiques et cohérentes à celles du
test #35 -- aucune dégradation de la lecture accéléromètre). Firmware
flashé et vérifié (unité #01, `verified 113136 bytes`).

**Mesure PPK2** : salve isolée 18,78 ms / 18,40 µC (contre 21,95-27,03 ms
/ 17,45-20,11 µC aux tests #32/#33/#35) -- confirme la réduction de durée
attendue. Plancher idle 4,26 µA (zoom 127,6 ms) -- **très proche du
plancher datasheet 4,3 µA**, en net progrès par rapport aux 6,6-9,8 µA
mesurés avant le RAM power-down (test #35) : meilleure confirmation à ce
jour que ce levier agit bien sur le plancher continu.

**Biais de mesure identifié (motif récurrent, déjà vu aux tests
#33/#35)** : la fenêtre 10 s contient une salve isolée à 66,31 mA,
nettement plus haute que les salves IMU normales (~15-24 mA) --
probablement la trame de santé BLE déclenchée par le flash récent
(`retained.next_health_us` repart à 0 après reflash, `health_due` vrai
au premier cycle). Moyenne brute de cette fenêtre : 21,54 µA / 215,42 µC
-- **pas directement comparable** au 20,63 µA du test #35 (qui n'avait
pas cette salve). Une mesure prise plus de 15 min après le dernier flash
donnera un chiffre 10 s propre et comparable.

**Constat pour la suite** : la salve `imu_vdd` restante (~18,4 µC) est
désormais dominée presque entièrement par le coût fixe du régulateur
(~250-300 µA pendant ~15-19 ms incompressibles : ~5 ms stabilisation +
~6 ms attente ODR + trafic I2C), pas par la sensibilité au capteur --
les gains suivants sur ce poste nécessiteront soit de comprendre enfin
la cause du ~250-300 µA du régulateur `imu_vdd`/LDO1 lui-même (question
DevZone toujours en attente, voir plus haut), soit de réduire encore le
temps d'activation du régulateur en dessous de ce qui est déjà testé.

## 🔴 Test #37 (2026-08-28) -- relance TWI périodique vers le nPM1300, écartée par la mesure

Recherche web approfondie (DevZone, errata complet nPM1300, PR Zephyr
#83790) pendant l'attente de la réponse Nordic sur la question §8 du
rapport. Piste trouvée : l'errata BUCK analogue [31] ("Increased BUCK
Hysteretic quiescent current") précise le correctif comme "**prompt**
read or write over TWI from host" -- suggérant une relance nécessaire,
pas un correctif ponctuel au démarrage. Vérifié dans le devicetree :
le nPM1300 est sur **i2c22** (0x6b), l'IMU sur **i2c30** (0x6a) -- bus
différents. Après l'unique lecture `LDSWSTATUS` intégrée par le pilote
(`regulator_npm13xx.c:460-464`, contournement errata [38], ~2 ms après
activation), le nPM1300 ne reçoit plus aucune transaction TWI pendant le
reste de la salve (~15-19 ms), puisque le trafic I2C vers l'IMU emprunte
un bus différent.

**Test** : une deuxième lecture `LDSWSTATUS` (adresse registre
base=0x08/offset=0x04) ajoutée juste avant `regulator_disable()`.
Vérification fonctionnelle OK (16 cycles, console temporaire, aucune
erreur). **Résultat PPK2 : charge de la salve AUGMENTÉE (18,40 → 20,64-
20,67 µC), pas réduite** -- le bus i2c22 tourne à 100 kHz (contre
400 kHz pour i2c30/IMU), donc la transaction ajoutée coûte
proportionnellement plus cher sans aucun bénéfice sur le courant de
repos. **Hypothèse infirmée par la mesure elle-même : le ~250-300 µA de
`imu_vdd`/LDO1 ne dépend pas d'un rafraîchissement TWI périodique.**
Code retiré, firmware restauré à l'état du test #36 (113136 octets,
reflashé et vérifié).

## 🔵 Recherche web complémentaire (2026-08-28) -- pistes explorées et closes

En parallèle du test #37, recherche systématique pour d'autres pistes
d'amélioration pendant l'attente de la réponse Nordic :

- **Errata nPM1300 complet** (liste officielle vérifiée exhaustive via
  `docs.nordicsemi.com/bundle/errata_npm1300_rev1`) : seules [38], [40],
  [41] concernent LOADSW/LDO -- toutes les trois déjà écartées (voir
  section IMU plus haut). Aucune erratum supplémentaire non considérée.
- **Propriété devicetree `nordic,anomaly38-disable-workaround`** :
  permet de DÉSACTIVER le contournement existant (pas d'en ajouter un
  nouveau) -- non pertinent pour notre cas (contournement déjà actif par
  défaut, jamais désactivé).
- **Domaine de puissance TWIM/I2C séparé** (architecture nRF54L,
  confirmé par un guide communautaire Hubble Network + vérifié dans le
  code du pilote Zephyr) : déjà gèré automatiquement par
  `CONFIG_PM_DEVICE_RUNTIME=y` + `pm_device_driver_init()` dans
  `i2c_nrfx_twim_common.c` -- notre bus I2C se suspend déjà seul entre
  nos transactions. Rien à configurer.
- **CRACEN (accélérateur crypto)** : confirmé dans le domaine de
  puissance MCU auto-géré par le matériel (source : cours Nordic
  Developer Academy nRF54L), pas un domaine "toujours actif" séparé --
  cohérent avec l'absence d'effet observé sur nos mesures en régime
  établi.
- **Fil DevZone "nPM1300 Quiescent Current"** et **"Dynamically control
  nPM1300 Load Switch 1 from firmware"** : cas similaires en apparence
  (courant élevé sur nPM1300) mais causes différentes à chaque fois
  (BUCK2 mal configuré sur PCB tiers ; condensateur de découplage lent à
  décharger) -- pas transposables à notre cas (LDO1 seul, aucune charge,
  cause déjà isolée au régulateur lui-même).

**Bilan recherche** : aucune piste supplémentaire trouvée sur le
~250-300 µA de `imu_vdd`/LDO1 au-delà de ce qui était déjà documenté --
la question reste ouverte auprès de Nordic (rapport
`Nordic-Support-Report-XIAO-nRF54LM20A.md`, §8). Les autres pistes
explorées (RAM, domaines de puissance, crypto) sont soit déjà
optimales par défaut, soit sans rapport avec le problème.

## 🔴 Test #38 (2026-08-28) -- audit broches, INCIDENT pic ~200 mA, changements retirés d'urgence

Audit systématique des broches GPIO potentiellement flottantes, demandé
explicitement. Trois changements identifiés et **groupés dans une seule
mesure** (écart au principe single-variable, à ne pas refaire) :

1. **NFC (P1.01/P1.02)** : `&nfct { status = "okay"; }` actif par défaut
   dans le devicetree de base du board alors que le NFC n'est jamais
   utilisé -- broches jamais routées vers un connecteur sur ce board
   (vérifié). Correctif : `&uicr { nfct-pins-as-gpios; }` (syntaxe
   confirmée sur la carte de référence `nrf54lm20dk`, même famille de
   puce) + déconnexion GPIO explicite.
2. **INT1 IMU (gpio0.6)** : pilote LSM6DSL configure cette broche en
   entrée sans aucun pull (`lsm6dsl_trigger.c:135`), une seule fois. Avec
   `imu_vdd` coupé ~98% du temps, broche potentiellement flottante côté
   SoC. Correctif tenté : pull-down réimposé à chaque cycle après
   `regulator_disable()`.
3. **PDM** (`configure_pdm_pins_for_system_off()`) : code du test #12
   jamais appelé depuis, réactivé par hygiène.

**Vérification fonctionnelle console OK** (16 cycles, aucune erreur) --
mais **la mesure PPK2 a montré un pic ~200 mA, jamais observé nulle part
ailleurs dans ce projet** (pire pic précédent : ~87 mA, salve BLE santé).
Charge d'une seule salve : 108,66-108,72 µC (contre ~18-20 µC en
temps normal) -- environ 5-6× le coût normal d'un cycle.

**Action immédiate** : les trois changements ont été retirés en bloc
(pas de tri fin -- la sévérité de l'anomalie ne justifiait pas de
continuer à expérimenter sur le matériel réel avant d'avoir un
diagnostic). Firmware reflashé et revérifié à l'identique du test #36
(`verified 113136 bytes`, binaire byte-pour-byte identique). Unité #01
refonctionne normalement.

**Suspect le plus probable (non confirmé)** : le pull-down INT1 (#2),
seule broche des trois réellement pilotée par un composant externe actif
(le LSM6DS3TR-C) -- NFC et PDM n'ont jamais été connectées à quoi que ce
soit sur ce board. Hypothèse : contention électrique entre le pull-down
interne et une sortie INT1 encore active pendant une décharge lente de
`imu_vdd` après `regulator_disable()` (mécanisme analogue à un fil
DevZone nPM1300 trouvé plus tôt -- "condensateur de découplage lent à
décharger"). **Non vérifiée** : un pull-down résistif classique ne
devrait normalement produire que quelques centaines de µA au pire contre
une sortie activement pilotée, pas 200 mA -- l'ampleur du pic dépasse ce
qui est expliqué par cette hypothèse seule. Cause exacte non élucidée.

**Incident secondaire, résolu** : après le reflash, énumération USB
instable sur plusieurs vérifications consécutives (composants
CMSIS-DAP/série apparaissant et disparaissant), contrairement à toutes
les connexions précédentes de cette session (toujours stables dès la
première vérification). Utilisateur a confirmé une procédure de
reconnexion identique à d'habitude (60 s sans alimentation avant
reconnexion USB, protocole PPK2 standard respecté) -- l'instabilité
s'est résorbée d'elle-même après quelques vérifications supplémentaires
(énumération Windows, pas un symptôme matériel confirmé). Flash réussi
une fois l'énumération stable.

**Prochaine étape, à ne PAS sauter** : ne jamais réintroduire ces trois
changements ensemble. Si retesté, un seul à la fois :
1. NFC seul (le plus sûr, broches jamais connectées) en premier.
2. PDM seul en second (déjà validé sans effet dans l'ancienne
   architecture, à reconfirmer sur celle-ci).
3. INT1 pull-down **en dernier, avec prudence** -- envisager d'abord de
   vérifier si `imu_vdd` est réellement retombé à 0 V avant d'appliquer
   le pull-down (ex. lire l'état du régulateur, ou ajouter un court
   délai après `regulator_disable()` avant de toucher à INT1), plutôt
   que de le retenter tel quel.

## 🟢 Test #39 (2026-08-28) -- sensibilité à l'intervalle de sondage IMU, 1000 vs 1500 ms

Test de comparaison demandé explicitement (mesure 60 s, unité #01,
firmware de production sinon inchangé -- seule variable :
`MOTION_POLL_INTERVAL_MS`) :

| Intervalle | Moyenne (fenêtre 60 s) | Charge |
|---|---|---|
| 1000 ms (référence) | 21,52 µA | 1,29 mC |
| 1500 ms | 15,87 µA | 951,92 µC |

**−26 % de consommation pour −33 % de fréquence de sondage** -- confirme
que la charge par salve `imu_vdd` est quasi fixe (~18-20 µC),
indépendamment de l'intervalle. **Décision explicite de l'utilisateur :
revenir à 1000 ms** -- la réactivité ~1 s (référence nRF52840 Sense,
~10 µA) reste prioritaire sur le gain de consommation disponible en
relâchant cet objectif. Firmware restauré à 1000 ms, reflashé et
revérifié.

**Observation transversale à investiguer plus tard** : la mesure 60 s à
1000 ms (capture indépendante, refaite par l'utilisateur) montre des
pics occasionnels plus hauts (jusqu'à ~82 mA) revenant environ toutes les
~5 s tout au long de la minute -- ni le cycle IMU (~1 s) ni la trame
santé BLE (15 min) n'expliquent cette période. Cause non identifiée,
notée pour une investigation séparée, ne bloque pas les résultats
ci-dessus (le total 21,52 µA/1,29 mC reste valide comme référence).

**Piste HFXO -- vérifiée et écartée (2026-08-28), aucun changement de
code fait** : le pilote TWIM Zephyr (`i2c_nrfx_twim.c`,
`i2c_nrfx_twim_common.c`) ne contient **aucune référence à
HFXO/XOSTART** -- il ne force jamais le démarrage du cristal 32 MHz.
D'après le schéma d'horloge (Fig.12 p.73) et le texte p.74 ("quand
toutes les requêtes HFCLK control se terminent, le PLL/HFINT s'arrête
automatiquement"), **HFINT est la source par défaut** tant que rien
n'appelle explicitement `XOSTART` -- notre code ne le fait jamais, et
MPSL (`CONFIG_MPSL_HFCLK_LATENCY=1650`) ne demande HFXO que ponctuellement
juste avant un événement radio réel (la salve BLE toutes les 15 min),
pas en continu ni à chaque cycle de sondage IMU. Les durées de salve
mesurées (tests #32/#33/#35, ~20-27 ms) collent exactement au budget des
délais explicites du code (`k_msleep(5)` + `k_msleep(10)` + trafic I2C)
-- aucun surcoût inexpliqué qui trahirait un démarrage HFXO caché à
chaque cycle. **Conclusion : le trafic I2C tourne déjà sur HFINT par
défaut, sans coût de démarrage cristal ajouté -- rien à corriger, piste
fermée.**

## Pour reprendre dans une nouvelle conversation

1. Lire ce document en entier, en particulier § Pivot d'architecture et
   § Tests #32/#33 en tête de document -- c'est l'état actuel du travail,
   pas une option parmi d'autres.
2. ✅ Architecture System ON IDLE implémentée et mesurée (tests #32/#33,
   ~27 µA, unité #01). Étapes 1-4 de cette liste (implémenter, mesurer,
   comparer aux références) sont faites -- voir § Tests #32/#33.
3. Prochaine étape : obtenir une mesure 10s propre (sans salve BLE, voir
   note du test #33) pour confirmer le chiffre de référence, puis évaluer
   si d'autres leviers de code existent au-delà des délais de
   stabilisation (déjà proches de leurs planchers documentés) pour
   continuer à réduire le coût de la salve `imu_vdd` (~70 % du coût par
   cycle) -- sans jamais remettre en cause la réactivité ~1 s ni la
   correction des données envoyées à HA (objectifs non négociables, voir
   § Objectif final du projet).
4. Progression single-variable comme jusqu'ici : un seul changement à la
   fois entre deux mesures.
5. Consigner le résultat de chaque étape ici au fur et à mesure (pas
   dans `Transition-nRF54LM20A-Optimisation-Consommation.md`, réservé à
   l'historique détaillé si besoin de retrouver le raisonnement complet).
