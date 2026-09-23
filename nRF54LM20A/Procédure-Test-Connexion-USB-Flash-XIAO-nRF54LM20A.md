# Procédure — Test de connexion USB et flash (XIAO nRF54LM20A)

**Objectif de ce document** : vérifier de façon fiable et reproductible
qu'une unité XIAO nRF54LM20A est correctement identifiée, lisible
(numéro de série du pont SWD) et flashable via PowerShell + OpenOCD,
indépendamment de l'image flashée. Ce document couvre uniquement la
**mécanique de connexion/flash** — pour le choix de l'image à déployer
et le clonage en lot, voir `Procedure-Clonage-XIAO-nRF54LM20A.md`.

Formalisé le 2026-09-23 à partir des commandes déjà établies et testées
dans `Configuration-nRF54LM20A-System-ON-IDLE.md` §5 et
`Procedure-Clonage-XIAO-nRF54LM20A.md`, consolidées ici en une procédure
unique et testée de bout en bout sur #02.

---

## Prérequis (vérifiés par lecture directe le 2026-09-23)

| Élément | Chemin | Statut |
|---|---|---|
| OpenOCD xPack v0.12.0-7 | `C:\ncs\tools\xpack-openocd-0.12.0-7\bin\openocd.exe` | Existe, vérifié |
| Config carte Seeed | `C:\ncs\vendor\platform-seeedboards\zephyr\boards\arm\xiao_nrf54lm20a\support\openocd.cfg` | Existe, vérifié |
| Câble | USB-C direct PC ↔ XIAO | Le pont SAMD11 embarqué suffit, pas de sonde externe en usage normal |

**Règle absolue de cette machine, non négociable** : `openocd` s'exécute
**toujours via l'outil PowerShell, jamais Bash/Git-Bash**
(`C:\ncs\CLAUDE.md`, règle « PowerShell pour tout outil USB/série/HID »).
Sous Bash, la lecture du numéro de série échoue de façon quasi
systématique sur cette machine ; la commande strictement identique
réussit sous PowerShell.

---

## Étape 0 — Identifier ce qui est branché (avant toute autre étape)

```powershell
Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -like "*VID_2886*" } | Select-Object FriendlyName, InstanceId, Status
```

La ligne `Périphérique USB composite` a un `InstanceId` qui se termine
par le numéro de série du pont SWD (fixe par carte). **Si plusieurs
lignes VID_2886 apparaissent avec des numéros de série différents,
plusieurs XIAO sont branchés simultanément — ne jamais supposer qu'un
seul l'est.**

### Unités connues

Tableau unique (S/N, adresse BLE, image, statut) :
`Configuration-nRF54LM20A-System-ON-IDLE.md` § 7. **#01 : toute
modification requiert un accord explicite de l'utilisateur.**

---

## Étape 1 — Vérification lecture seule du numéro de série (OBLIGATOIRE avant tout flash)

```powershell
$env:Path = "C:\ncs\tools\xpack-openocd-0.12.0-7\bin;" + $env:Path
$BOARD_DIR = "C:/ncs/vendor/platform-seeedboards/zephyr/boards/arm/xiao_nrf54lm20a"
openocd -s "$BOARD_DIR/support" -f "$BOARD_DIR/support/openocd.cfg" `
  -c "cmsis-dap vid_pid 0x2886 0x0068" -c "cmsis-dap backend hid" -c "adapter speed 500" `
  -c "init" -c "exit"
```

Chercher `CMSIS-DAP: Serial# = ...` dans la sortie. **Comparer
explicitement, par écrit, au numéro de série attendu pour l'unité
ciblée avant de continuer** — ne jamais flasher sur la seule foi du
contexte de conversation ("on teste sur #02"). C'est exactement
l'erreur à l'origine de l'incident du 2026-09-07 (flash accidentel de
#01, voir `C:\ncs\CLAUDE.md`).

`-c "cmsis-dap backend hid"` est obligatoire — sans cette ligne, le
pont SAMD11 utilise par défaut le backend WinUSB v2, qui peut s'énumérer
correctement (serial lu) tout en échouant sur **chaque** transaction
réelle.

**✅ Résultat obtenu le 2026-09-23 (test réel sur l'unité alors
branchée)** :
```
Info : CMSIS-DAP: Serial# = 9C4A557D
```
Conforme à #02 attendu, confirmé indépendamment par `Get-PnpDevice`
(Étape 0) et par OpenOCD — première tentative, aucun retry nécessaire,
aucune autre unité VID_2886 présente simultanément.

---

## Étape 2 — Flash + vérification (écriture réelle — confirmation explicite requise avant exécution)

Cibler l'unité **explicitement par son numéro de série** (`adapter
serial`), jamais seulement par `vid_pid` (identique pour toutes les
unités du projet) :

```powershell
$env:Path = "C:\ncs\tools\xpack-openocd-0.12.0-7\bin;" + $env:Path
$BOARD_DIR = "C:/ncs/vendor/platform-seeedboards/zephyr/boards/arm/xiao_nrf54lm20a"
$SERIAL = "<numéro confirmé à l'Étape 1>"
$HEX = "<chemin du fichier .hex à flasher>"
openocd -s "$BOARD_DIR/support" -f "$BOARD_DIR/support/openocd.cfg" `
  -c "cmsis-dap vid_pid 0x2886 0x0068" -c "cmsis-dap backend hid" -c "adapter serial $SERIAL" -c "adapter speed 500" `
  -c "init" -c "reset halt" `
  -c "nrf54lm20a-load `"$HEX`"" `
  -c "reset halt" `
  -c "verify_image `"$HEX`"" `
  -c "reset" -c "exit"
```

Résultat attendu : une ligne `verified` avec la taille exacte du
fichier flashé.

**Ne jamais utiliser `dump_image` + comparaison manuelle comme
vérification** — la RRAM ne s'efface pas avant écriture, ce qui produit
de fausses corruptions sur des zones de padding jamais réécrites par le
fichier courant (faux positifs déjà documentés sur ce projet).
`verify_image` est la seule méthode de vérification fiable.

Ne jamais rediriger stderr (`2>&1`) sur ces commandes PowerShell —
enveloppe chaque ligne dans un `NativeCommandError` qui rend le code de
sortie peu fiable même en cas de succès réel ; PowerShell capture déjà
stderr sans ça.

**✅ Résultat obtenu le 2026-09-23 (test réel, confirmation utilisateur
obtenue avant exécution)** : numéro de série re-vérifié indépendamment
juste avant flash (`9C4A557D`, conforme), flash ciblé explicitement
`adapter serial 9C4A557D`, résultat :
```
verified 117128 bytes in 0.160720s (711.690 KiB/s)
```
Taille exacte attendue, aucune erreur, aucun HardFault, première
tentative.

---

## Étape 3 — Cycle d'alimentation obligatoire après tout flash

Débrancher puis rebrancher **complètement** le câble USB-C. Tant
qu'une session OpenOCD a touché la carte, le SoC reste en « Debug
Interface mode » (datasheet nRF54LM20A §9.3), qui émule le System OFF
au lieu de l'appliquer réellement — sans ce cycle, une mesure de
consommation ou un test fonctionnel donnerait un résultat trompeur.

---

## Étape 4 — Vérification fonctionnelle minimale

- Présence des trames BTHome dans Home Assistant (mouvement, batterie,
  etc.).
- (Optionnel, recommandé sur un sous-échantillon plutôt que
  systématique) Mesure PPK2 à la valeur de référence documentée pour
  l'image flashée — **PPK2 et USB-C jamais connectés simultanément**
  (protocole établi du projet).

---

## Mettre à jour une unité du lot (procédure courante)

1. Brancher l'unité (seule). Vérifier en 8 s que le branchement est
   propre (commande du § « Tempête USB au branchement »). Si tempête
   (unité encore sur l'ancien firmware) : débrancher/rebrancher ; si elle
   persiste, contournement PPK2 (§ dédié).
2. `deploy-scripts\Flash-XiaoUnit.ps1 -Serial <S/N attendu>` (PowerShell) :
   relit le S/N et refuse s'il diffère, affiche l'adresse BLE (FICR),
   flashe l'image d'or actuelle, `verify_image`. Attendu :
   `verified 117260 bytes`.
3. Reporter S/N, adresse BLE et résultat dans
   `Configuration-nRF54LM20A-System-ON-IDLE.md` § 7 et
   `deploy-scripts/deployment-log.csv`.
4. Débrancher/rebrancher 3 fois : LED rouge allumée à chaque branchement,
   aucune rafale d'événements 1010 dans le journal Windows (une
   disparition par débranchement uniquement).

---

## Unité sans USB (connecteur USB-C cassé) — flash par J-Link externe

**Statut : procédure préparée, pas encore exécutée** — première exécution
à superviser en lisant la sortie J-Link complète (le script l'affiche).

**Sonde : J-Link V9** (logiciel SEGGER `C:\Program Files\SEGGER\JLink_V924a`,
cible `nRF54LM20A_M33`). Un **CH340 ne convient pas** : convertisseur
USB↔UART, il ne parle pas SWD, et ni le nRF54LM20A ni notre firmware
n'ont de bootloader série. Si le logiciel SEGGER refuse le V9 (clone),
repli possible : le J-Link embarqué d'une nRF52840 DK (connecteur
« Debug out »).

**Alimentation : PPK2 en Source meter, 3,7 V, sur BAT+/BAT-.** Ne pas
utiliser la broche 5 V du J-Link : elle passerait par VBUS et la limite
100 mA du nPM1300, avec le risque de redémarrages en boucle tant que
l'unité porte l'ancien firmware (§ « Tempête USB au branchement »).
Sans USB, le pont SAMD11 ne gêne pas la sonde (schéma V1.0, feuille
« 04 Debug ») : son alimentation `SAMD11_3V3` vient de l'interrupteur U3,
commandé par VBUS ; sans VBUS, le traducteur de niveaux U4 (UM3204H)
entre le SAMD11 et le nRF54 a son OE tiré à la masse par R16 → sorties
haute impédance.

**Câblage** (connecteur J-Link 20 broches, broche 1 repérée par le
triangle ; pastilles « Back debugging function points » au dos du XIAO,
noms du schéma — utiliser TP1/TP2, **jamais TP7/TP8** `SWCLK2`/`SWDIO2`
qui vont au SAMD11) :

| J-Link (20 broches) | XIAO nRF54LM20A |
|---|---|
| 1 — VTref | TP5 `3V3` (= `3V3_OUT`), ou broche `3V3` du connecteur XIAO |
| 4 (ou toute broche paire 4-20) — GND | TP3 `GND`, ou broche `GND` |
| 7 — SWDIO | TP2 `SWDIO` (nRF54) |
| 9 — SWCLK | TP1 `SWCLK` (nRF54) |
| 15 — RESET | TP4 `RST` (nRF54, facultatif) |
| 19 — 5 V | **ne rien brancher** |
| PPK2 VOUT / GND | `BAT+` / `BAT-` |

VTref est indispensable : le J-Link s'en sert pour régler ses niveaux
logiques (sans lui : « VTref 0 V », pas de connexion).

**Déroulé** :
1. Câbler, J-Link branché au PC, puis PPK2 en Source meter 3,7 V, sortie
   activée.
2. Identification (lecture seule) : `deploy-scripts\Flash-XiaoUnit-JLink.ps1`
   → affiche l'adresse BLE (FICR). La comparer au tableau
   `Configuration-nRF54LM20A-System-ON-IDLE.md` § 7 pour savoir de quelle
   unité il s'agit.
3. Flash : `deploy-scripts\Flash-XiaoUnit-JLink.ps1 -Flash -ExpectedMac
   <MAC lue à l'étape 2> -Label <unitNN>` — refuse si la MAC diffère ou
   si c'est #01 ; `loadfile` + `verifybin` + ligne dans
   `deployment-log.csv`.
4. Couper puis rétablir l'alimentation PPK2 et vérifier le fonctionnement
   (trames BTHome dans Home Assistant, LED rouge).

---

## Pannes connues et leur traitement (appliquer directement, ne pas re-diagnostiquer)

| Symptôme | Cause | Traitement |
|---|---|---|
| Échec sous Bash/Git-Bash (périphérique introuvable, erreurs `WriteFile`) | `openocd` nécessite un accès USB/HID direct, cassé sous Git-Bash sur cette machine | Relancer la **même** commande via l'outil PowerShell — pas de nouveau diagnostic |
| `unable to find a matching CMSIS-DAP device` / `cannot read IDR` | Isolé : pont SAMD11 intermittent. Persistant : très probablement une tempête USB (§ dédié) | Isolé : relancer la même commande. Persistant : vérifier la tempête (8 s) avant toute autre piste |
| Transactions échouent alors que le serial est lu correctement (`Entity not found`, `could not claim interface`) | Backend WinUSB v2 par défaut au lieu de HID | Vérifier que `-c "cmsis-dap backend hid"` est bien présent |
| Pont qui disparaît/réapparaît en boucle dès le branchement, LED rouge éteinte, aucune session openocd possible | Tempête USB : limite VBUS 100 mA du nPM1300 dépassée par l'allumage de LDO1 (firmware sans correctif) | Voir § « Tempête USB au branchement » : vérifier en 8 s, rebrancher jusqu'à branchement propre, flasher l'image corrigée |
| HardFault (`pc: 0xeffffffe`) au tout premier flash d'une carte avec une nouvelle version de firmware | Connu, cause non documentée plus précisément | Reflasher immédiatement la même commande — suffit systématiquement à ce jour |
| `verify_image` échoue après un flash apparemment réussi | Vraie divergence de contenu — **NE PAS ignorer** | Ne pas déployer/utiliser l'unité ; réinvestiguer avant de recommencer |

---

## Tempête USB au branchement — cause, correctif, détection

**Symptôme** : dès le branchement USB-C, le pont SAMD11
(`VID_2886&PID_0068`) disparaît puis réapparaît ~1,7 fois par seconde,
indéfiniment. Journal Windows `Microsoft-Windows-Kernel-PnP/Device
Management`, événement **1010** (« supprimé de manière inattendue, car
il est signalé comme manquant sur le bus »). Aucune session OpenOCD
possible (`unable to find a matching CMSIS-DAP device`, `error reading
data: Success`, `hid_write … 0x3E3`, `cannot read IDR`). **LED rouge
éteinte** (LED0 du nPM1300, normalement allumée). Touche toutes les
unités portant le firmware `xiao_door_sensor` sans correctif, au hasard
d'un branchement à l'autre (#01 : 5 branchements sur 5 le 2026-09-23),
et persiste jusqu'au débranchement. Démarre **sans aucune commande côté
PC** (vérifié par surveillance passive).

**Cause (confirmée par test)** : le nPM1300 redémarre à chaque
branchement (pas de batterie) avec une **limite d'entrée VBUS de
100 mA** — seul le logiciel peut la relever (`VBUSINILIM0`).
L'**appel de courant à l'allumage de LDO1** (`imu_vdd`, IMU + micro)
dépasse 100 mA → la protection du PMIC coupe VSYS → le nRF54 **et** le
pont SAMD11, alimentés par le même BUCK2 `VSYS_3V3` (schéma Seeed V1.0,
page Power : U3 alimente `SAMD11_3V3` depuis `VSYS_3V3`), redémarrent ;
le SAMD11 réinitialise aussi le nRF54 (PA04 → Q1 → `nRF54_RESET`) → même
séquence → boucle. Problème documenté par Seeed :
[platform-seeedboards#81](https://github.com/Seeed-Studio/platform-seeedboards/issues/81)
(corrigé chez Seeed par la PR #82, absente de notre clone local
`vendor/platform-seeedboards`, `c0387a2`). Déclencheur dans notre code :
`&pmic_charger { zephyr,deferred-init; }` (commit `46192e7`, 2026-08-29)
reportait l'init du chargeur — qui porte la limite 500 mA de la carte —
après le premier `regulator_enable(imu_vdd)` de `sample_motion()`. Et le
driver n'écrit que `VBUSINILIMSTARTUP`, appliqué au branchement suivant
seulement (perdu sans batterie).

**Correctif** (`src/main.c`, `raise_vbus_current_limit()` appelée en tout
début de `main()`, avant le BLE et avant tout allumage de LDO1) :
`device_init()` du chargeur puis `sensor_attr_set(charger,
SENSOR_CHAN_CURRENT, SENSOR_ATTR_CONFIGURATION, 0,5 A)` → écrit
`VBUSINILIM0` + `ILIMUPDATE`, effet immédiat. Image :
`golden-image/unit01-verified-2026-09-23-ABD-VBUSFIX-15uA.hex` (`.bin`
117 260 octets, SHA-256
`b5b41c8210d421a3a28f68ed24b2ef7f2d9cc919a1c2a868195ed27bdbde030f`)
= image A+B+D 16-17 µA (`b54850ec…`, reproduite à l'octet par un build
de référence des mêmes sources) + ce seul changement. **Consommation
mesurée sur #01 : 15,65 µA (`nRF54LM20A/archive/mesures-brutes/15.65microAppk-20260923T114931.csv`)** — promue image
d'or.

**Validation sur #01 (`C5F0E209`)** : ancien firmware 5 tempêtes sur 5
branchements ; firmware corrigé **15 branchements (10 rapides ~3 s, 5
lents ~7-8 s), 0 tempête**, LED rouge allumée à chaque branchement.

**Détecter une tempête (8 s, sans openocd)**, juste après branchement :
```powershell
$start = Get-Date; Start-Sleep -Seconds 8
@(Get-WinEvent -FilterHashtable @{LogName='Microsoft-Windows-Kernel-PnP/Device Management'; Id=1010; StartTime=$start} -ErrorAction SilentlyContinue | Where-Object { $_.Message -match "VID_2886" }).Count
```
`0` = branchement propre. `> 0` sans avoir débranché = tempête.

**Flasher une unité qui porte encore l'ancien firmware** : ne lancer
openocd que sur un branchement propre (sinon inutile). En cas de
tempête, débrancher/rebrancher et revérifier ; si l'unité reste en
tempête, le cycle PPK2 (USB-C débranché → PPK2 sur BAT+/BAT-, mesure
courte, coupure, retrait → USB-C) a donné un branchement propre sur
unit04 et #01 (mécanisme non établi). Une fois l'unité flashée avec le
correctif, le problème ne se pose plus.

**Pistes vérifiées et écartées — ne pas les refaire** :
- Bash vs PowerShell, processus `nrfutil --hotplug`, backend
  `usb_bulk`/`auto`, suspension sélective USB (déjà désactivée),
  relances répétées d'openocd : sans effet.
- **`Disable-PnpDevice` sur le composite XIAO : ne jamais l'utiliser.** Il
  désactive l'instance dans Windows (`ConfigFlags = 0x1`,
  `CM_PROB_DISABLED`) tout en renvoyant « Échec générique ». Réparation
  (admin, pendant que l'unité est présente) :
  `pnputil /enable-device "USB\VID_2886&PID_0068\<SERIAL>"`.
- `power_en` (P1.12) : **non connectée** sur la carte V1.0 — sans effet.
- `-c "reset"` OpenOCD : reset logiciel, ne remplace pas un cycle
  d'alimentation.
- Scripts PowerShell : jamais `2>&1`, jamais `*>` combiné à `2>` ; lire
  la sortie openocd depuis un fichier (`*> fichier` puis `Get-Content`),
  pas via `$var = & openocd`.

**Unités à mettre à jour** : toutes les unités `xiao_door_sensor` sans
correctif (#02, unit04-13) — à reflasher avec
`unit01-verified-2026-09-23-ABD-VBUSFIX-15uA.hex`.

---

## Historique de validation de cette procédure

| Date | Unité testée | Étape testée | Résultat |
|---|---|---|---|
| 2026-09-23 | #02 (`9C4A557D`) | Étape 0 (identification) + Étape 1 (lecture seule S/N) | Conforme, premier essai, aucun retry, aucune autre unité présente |
| 2026-09-23 | #02 (`9C4A557D`) | Étape 2 (flash + verify_image) | `verified 117128 bytes`, conforme, 1ère tentative |
| 2026-09-23 | #02 (`9C4A557D`) | Étape 3 (cycle d'alimentation) | Confirmé OK par l'utilisateur |
| 2026-09-23 | #02 (`9C4A557D`) | Étape 4 (vérification fonctionnelle HA) | Confirmé OK par l'utilisateur |
