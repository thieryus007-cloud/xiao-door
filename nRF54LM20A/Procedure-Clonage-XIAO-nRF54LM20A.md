# Procédure de clonage — déployer une unité XIAO nRF54LM20A

**Document de référence unique pour dupliquer le firmware sur une
nouvelle unité.** Ne contient que la procédure de clonage elle-même —
pour l'état fonctionnel du firmware, voir
`Configuration-nRF54LM20A-System-ON-IDLE.md` ; pour régénérer l'image
d'or depuis les sources après une vraie modification de code, voir
`Configuration-nRF54LM20A-System-ON-IDLE.md` § « Régénérer l'image d'or ».

---

## Pourquoi cloner plutôt que rebuilder

**Depuis le 2026-08-30, ne pas rebuilder depuis les sources pour
déployer une unité supplémentaire.** Deux rebuilds successifs (correctif
GRTC, puis tentative de correctif de stabilisation accéléromètre) n'ont
pas résolu une anomalie de consommation réelle sur l'unité #02 (~80 puis
~200+ µA au lieu de ~20-22 µA attendus) et l'ont même aggravée — l'unité
#01, même génération de firmware, restait mesurée à ~20 µA sans ce
problème. La cause exacte de cet écart entre les deux unités avec un
firmware nominalement identique n'est toujours pas comprise.

**La méthode fiable est de cloner, octet pour octet, la mémoire flash
d'une unité déjà vérifiée en fonctionnement réel (mesure PPK2 conforme),
jamais de repartir des sources pour une unité supplémentaire.** Cloner
élimine le risque qu'un rebuild introduise un nouveau bug non détecté
avant un flash réel, quelle que soit la qualité apparente du
raisonnement de code.

## Pourquoi c'est sûr : chaque unité garde sa propre identité

Cloner le contenu exact de la flash d'une unité vers une autre **ne crée
aucune collision d'adresse BLE ni de numéro de série**, par construction
du firmware :
- L'adresse BLE fixe n'est **pas stockée dans le fichier flashé** :
  `set_fixed_ble_identity()` (`xiao_door_sensor/src/main.c`) appelle
  `hwinfo_get_device_id()` à chaque démarrage, qui lit l'identifiant
  unique gravé en usine dans le silicium du SoC — donc le même contenu
  de flash, exécuté sur deux puces physiquement différentes, dérive
  automatiquement deux adresses BLE différentes.
- Le numéro de série du pont USB↔SWD (SAMD11) est une puce séparée,
  indépendante du SoC principal et de son contenu flash.

## Image d'or (golden image) actuelle

| Fichier | Origine | Statut |
|---|---|---|
| `xiao_door_sensor/golden-image/unit01-verified-2026-09-23-ABD-VBUSFIX-15uA.bin` / `.hex` | Image A+B+D ci-dessous + un seul changement : `raise_vbus_current_limit()` en début de `main()` (limite d'entrée VBUS du nPM1300 relevée à 500 mA avant tout allumage de LDO1 — corrige la tempête USB au branchement, voir `Procédure-Test-Connexion-USB-Flash-XIAO-nRF54LM20A.md` § « Tempête USB au branchement »). SHA-256 `zephyr.bin` = `b5b41c8210d421a3a28f68ed24b2ef7f2d9cc919a1c2a868195ed27bdbde030f`, 117 260 octets. Build de référence des mêmes sources sans le changement = image A+B+D à l'octet près. | **Vérifiée sur #01 : 15,65 µA au PPK2 (`nRF54LM20A/archive/mesures-brutes/15.65microAppk-20260923T114931.csv`) ; 15 branchements USB sans tempête (contre 5 sur 5 avec l'ancien firmware).** Image de référence **actuelle**. |
| `archive/golden-image-anciennes/unit02-verified-2026-09-23-ABD-16uA.bin` / `.hex` | Rebuild depuis les sources (`west build --pristine`), commit `c477dc4` — étapes A (I2C PMIC 400 kHz), B (IMU 6→3 transactions), D (accéléromètre 833 Hz HP) du `archive/docs-historique/Plan-Reduction-Consommation-2026-09-22.md` ; C (LDO1 par broche) et F (LDO1 3,0 V) tentées puis revertées (régressions confirmées, voir `archive/docs-historique/Journal-Travail-2026-09-22.md`). SHA-256 `zephyr.bin` = `b54850ec5e29dfdb8530df934acbba85d5374565bcc06567b71778c3c21d78fa`, reproductibilité du build confirmée à deux reprises indépendantes (2026-09-22 et 2026-09-23). | **Vérifiée en fonctionnement réel sur #02 : régime établi 16,98 µA puis 16,84 µA (240 s) puis 15,95 µA (360 s), mesures PPK2 indépendantes cohérentes** — tests fonctionnels HA confirmés par l'utilisateur. Trame santé (15 min) non capturée dans une fenêtre PPK2 dédiée à ce jour — point encore ouvert, voir §12 du plan. Remplacée par l'image VBUSFIX ci-dessus (même consommation, sans la tempête USB au branchement) — ne plus déployer. |
| `archive/golden-image-anciennes/unit02-verified-2026-09-01-H_LACTIVE.bin` / `.hex` | Patch binaire (voir § suivant) de `unit01-verified-2026-08-30.bin`, un seul octet modifié | Vérifiée (~22-23 µA) — **conservée comme référence historique**, ne plus déployer directement (remplacée par le fichier ci-dessus). Unités 04-13 tournent toujours cette image ; redéploiement en attente d'une décision explicite de l'utilisateur (plan §12.2). |
| `archive/golden-image-anciennes/unit01-verified-2026-08-30.bin` / `.hex` | Dump physique de l'unité #01 (lecture seule, `serial=C5F0E209`) | Vérifiée (~20-22 µA) — conservée comme référence historique, ne plus déployer directement |

Ces fichiers `.hex` doivent rester suivis sur GitHub (exception dédiée
dans `.gitignore` au motif générique `*.hex`) — c'est la seule référence
dont la validité a été confirmée par une mesure physique, pas seulement
par une relecture de code.

## Déployer un lot d'unités — méthode recommandée (scripts)

**Pour flasher plusieurs unités à la suite (déploiement en lot), utiliser
les scripts `xiao_door_sensor/deploy-scripts/check-unit.sh` et
`flash-unit.sh` plutôt que les commandes `openocd` manuelles des Étapes
1-4 ci-dessous.** Les Étapes 1-2 (dump + conversion `.hex`) restent
nécessaires **une seule fois**, uniquement pour produire ou mettre à jour
l'image d'or elle-même (voir § précédent) — pas à chaque unité
supplémentaire.

Ces deux scripts encodent directement la règle absolue de ce projet
(`C:\ncs\CLAUDE.md`, « vérifier le numéro de série SWD avant TOUT
flash ») : impossible de flasher sans lecture indépendante et répétée du
numéro de série, impossible de flasher #01/#02/#03 par erreur (liste
interdite en dur dans les deux scripts), et chaque résultat (succès ou
échec) est journalisé — jamais de faux succès silencieux.

### Utilisation, par unité

```bash
cd "C:/ncs/projects/nRF54LM20A/xiao_door_sensor/deploy-scripts"

# Etape 1 -- lecture seule, identifie l'unite branchee
./check-unit.sh
# -> affiche le numero de serie, refuse si c'est #01/#02/#03,
#    signale si ce serial est deja dans le journal

# Etape 2 -- flash, avec le numero de serie EXACT affiche par check-unit.sh
./flash-unit.sh <etiquette, ex: unit14> <numero-de-serie>
# -> relit le numero de serie de facon independante et refuse de flasher
#    si une autre carte a ete branchee entretemps (mismatch)
#    -> refuse aussi #01/#02/#03 (deuxieme filet de securite)
#    -> flashe l'image d'or, verify_image, journalise le resultat
```

Après chaque flash réussi : débrancher/rebrancher complètement l'USB-C
(sortir du Debug Interface Mode, voir § « Notes de connexion SWD » plus
bas), puis vérifier au moins la présence des trames BTHome dans Home
Assistant. Une mesure PPK2 complète (~20-22 µA) n'est pas nécessaire sur
les 10 unités — un sous-échantillon suffit, la vérification `verify_image`
byte-for-byte garantit déjà un contenu flash identique à l'image d'or.

### Ce que les scripts gèrent déjà tout seuls

- **Pont SAMD11 intermittent** (`unable to find a matching CMSIS-DAP
  device`, `cannot read IDR`) : les deux scripts retentent automatiquement
  jusqu'à 5 fois — symptôme déjà documenté, se résout presque toujours
  ainsi sans intervention.
- **Échec persistant au-delà de 5 tentatives** (observé le 2026-09-13,
  après une longue série de sessions SWD consécutives sur la même unité) :
  les scripts échouent proprement et journalisent l'échec plutôt que de
  boucler indéfiniment. **Action manuelle requise dans ce cas** :
  débrancher puis rebrancher complètement l'USB-C de l'unité concernée
  (ou, si ça ne suffit pas, passer par un cycle d'alimentation PPK2 — voir
  § « Notes de connexion SWD »), puis relancer `check-unit.sh` sur cette
  même unité.
- **Journal** : `deploy-scripts/deployment-log.csv` (horodatage,
  étiquette, numéro de série, image utilisée, statut) — une ligne par
  tentative, y compris les échecs, pour garder une trace complète.

### Si l'image d'or change

Mettre à jour la variable `GOLDEN_HEX` en tête de `flash-unit.sh` (chemin
vers le nouveau `.hex`) avant de démarrer un nouveau lot — les scripts ne
la déduisent pas automatiquement du tableau ci-dessus.

### Historique des lots

| Date | Étiquettes | Résultat |
|---|---|---|
| 2026-09-13 | unit04 à unit13 (10 unités) | `verify_image OK` sur les 10, aucune touche à #01/#02/#03 — détail complet dans `deploy-scripts/deployment-log.csv` et le tableau § 7 de `Configuration-nRF54LM20A-System-ON-IDLE.md` |

## Ne pas inclure le transitoire de démarrage dans une moyenne PPK2

Posé explicitement le 2026-09-01, après une erreur qui a fait perdre du
temps sur une fausse alerte de régression : **une capture PPK2 juste
après un flash/reset contient un transitoire de démarrage (init BLE,
première trame de santé si l'échéance retenue est à zéro après un flash)
qui fausse fortement la moyenne globale, surtout sur une fenêtre courte
(20 s).** Toujours exclure au minimum les 3 premières secondes de la
capture avant de calculer une moyenne « au repos ». Exemple concret : un
binaire identique à l'image d'or à un bit près a mesuré 31,1 µA en
moyenne brute sur 20 s, contre 23,1 µA une fois les 3 premières secondes
exclues (conforme à la référence ~22 µA) — la version brute avait
déclenché une fausse investigation de régression.

## Patcher un binaire existant plutôt que reconstruire depuis les sources

**Méthode obsolète depuis le 2026-09-22 — ne plus l'utiliser pour un
nouveau correctif.** `west build` a été prouvé déterministe
(`archive/docs-historique/Plan-Reduction-Consommation-2026-09-22.md` §1) : un rebuild
`--pristine` de `c63908d` + `H_LACTIVE` (`main.c` réaligné, commit
`630b32a`) reproduit `archive/golden-image-anciennes/unit02-verified-2026-09-01-
H_LACTIVE.bin` octet pour octet (SHA-256 `0da087ae...`). La cause
apparente de l'écart ~33 µA ci-dessous était `k_msleep(40)` au lieu de
`k_msleep(6)`, introduit par erreur dans le source après le flash de
l'image d'or — pas un défaut de `west build`. Conservé ci-dessous comme
référence historique de la technique et de son contexte, et parce que
`unit02-verified-2026-09-01-H_LACTIVE.bin` (toujours l'image d'or de
plusieurs unités déployées, voir tableau plus bas) a réellement été
produite ainsi à l'origine.

**Contexte d'origine (2026-09-01 → 2026-09-22, résolu)** : `west build`
sur `xiao_door_sensor/src/main.c` produisait alors un binaire qui
mesurait ~33 µA au repos au lieu de ~20-22 µA (64159 octets de
différence avec l'image d'or sur 117396, cause exacte non comprise à
l'époque — `CONFIG_PM` ne s'active jamais sur cette puce, `HAS_PM`
n'étant sélectionné nulle part pour la famille nRF54L, mais ce point
était déjà confirmé vrai au moment de l'image d'or du 2026-08-30 donc
écarté comme cause à l'époque ; la vraie cause, `k_msleep(40)`, n'a été
identifiée que le 2026-09-22).

**Méthode alternative utilisée avec succès pour le correctif H_LACTIVE**
(un seul bit modifié dans un registre IMU, voir `Nordic-Support-Report-
XIAO-nRF54LM20A.md` §8.3) : localiser l'instruction machine exacte dans
un rebuild instrumenté (`objdump -dl`, corrélation avec les numéros de
ligne source via `-g -gdwarf-4`), identifier l'encodage ARM Thumb-2 précis
de l'octet à changer, vérifier son unicité dans le binaire d'or par
recherche de motif binaire, puis modifier cet unique octet directement
dans une copie de l'image d'or (jamais l'image d'or elle-même). Étapes :

1. Compiler UNE FOIS avec le changement de code souhaité (juste pour
   obtenir l'adresse/l'encodage exact via `objdump -dl`, pas pour
   déployer ce binaire directement).
2. Repérer l'instruction exacte (ex. `movw r3, #0x4412` -> `#0x6412`
   pour ajouter un bit), vérifier l'encodage avec `arm-zephyr-eabi-as`
   sur un fichier `.s` isolé si le calcul manuel des bits n'est pas
   évident (encodages Thumb-2 souvent non contigus).
3. Rechercher le motif d'octets exact (4-8 octets de contexte) dans
   l'image d'or via un script Python (recherche binaire, pas texte) --
   confirmer une seule occurrence avant de patcher.
4. Copier l'image d'or, modifier le ou les octets identifiés, vérifier
   par diff binaire complet que rien d'autre n'a changé.
5. Flasher avec `verify_image` comme d'habitude, mesurer au PPK2 en
   excluant le transitoire de démarrage (voir § ci-dessus) avant de
   promouvoir comme nouvelle image d'or.

**Historique de clonage :**

| Date | Unité cible | Résultat |
|---|---|---|
| 2026-08-30 | #02 | `verify_image` OK, 117396 octets identiques à l'image d'or — consommation PPK2 à confirmer |
| 2026-08-29 (session ultérieure) | #02 | Reclonée après un détour diagnostic (firmware de lecture registres PMIC, puis firmware de test avec correctif de confirmation intra-cycle — aucun des deux jamais destiné au déploiement). `verify_image` OK, 117396 octets. Diagnostic par trace instrumentée (SWD, 25 cycles réels) : boucle principale à la bonne cadence, aucune fausse détection de mouvement/angle, configuration LDO1/`imu_vdd` correcte (registres PMIC lus directement). Écart de consommation résiduel vs #01 (~45 µA de moyenne mesurée, plancher jamais sous ~3,8 µA sur 15,7 s) **non expliqué par le firmware** — toutes les pistes de logique applicative vérifiées et exclues une à une. Cause encore à déterminer à ce stade (mesure faite alors qu'un firmware de diagnostic venait d'être retiré, voir ligne suivante pour la confirmation propre). |
| 2026-08-30 | #02 | Reclonée depuis `unit01-verified-2026-08-30.hex` (image d'or standard, sans aucune instrumentation de diagnostic) après nettoyage complet des firmwares de test. `verify_image` OK, 117396 octets. **Mesure PPK2 confirmée par l'utilisateur : moyenne de consommation identique à #01.** Cette mesure valide la procédure de clonage standard elle-même (déjà documentée depuis le début, aucune méthode différente) — les écarts précédents (~45-70 µA) sont attribuables aux firmwares de diagnostic laissés en place pendant l'investigation, pas à un défaut du clonage ou du firmware de référence. Voir `Configuration-nRF54LM20A-System-ON-IDLE.md` §4 « Règle absolue : ne jamais laisser un firmware de diagnostic flashé » pour l'incident et la règle qui en découle. |
| 2026-08-31 | #02 | Reclonée depuis la même image d'or après une session de diagnostic lecture-seule (`xiao_imu_pin_diag`, questions broches/état IMU pour le suivi Nordic — voir `Nordic-Support-Report-XIAO-nRF54LM20A.md` §8.2/§8.3). `verify_image` OK, 117396 octets. Cycle d'alimentation USB-C complet effectué avant mesure (règle SWD ci-dessous). **Mesure PPK2 confirmée par l'utilisateur : ~22 µA, identique à la référence.** Confirme que des sessions SWD répétées (flash/lecture) n'altèrent pas la consommation une fois l'image d'or restaurée et un cycle d'alimentation complet effectué. |
| 2026-09-01 | #02 | **Nouvelle image d'or produite** : correctif `H_LACTIVE` (registre CTRL3_C de l'IMU, voir §8.3 du rapport Nordic) patché directement dans l'image d'or du 2026-08-30 (un seul octet modifié, offset 46096, `0x44`→`0x46`, voir § « Patcher un binaire existant » ci-dessus — le rebuild depuis les sources était cassé ce jour-là, voir même section). `verify_image` OK, 117396 octets. **Mesure PPK2 confirmée : 23,061 µA en excluant les 3 premières secondes** (31,132 µA en moyenne brute — transitoire de démarrage, voir § dédié ci-dessus), conforme à la référence. Promue comme image d'or actuelle sous `unit02-verified-2026-09-01-H_LACTIVE.hex`. |
| 2026-09-07 | #02 | Détour diagnostic (`xiao_int1_fix_test` étendu à 4 segments, test `PP_OD` suite à une suggestion Nordic — voir `Nordic-Support-Report-XIAO-nRF54LM20A.md` §8.3 point 5). Confirmé : `PP_OD=1` n'apporte aucun gain supplémentaire sur `H_LACTIVE=1` seul (239,082 vs 239,009 µA, dans le bruit de mesure) — pas de changement de firmware. **Reclonée depuis `unit02-verified-2026-09-01-H_LACTIVE.hex` après ce diagnostic**, `verify_image` OK, 117396 octets. |

---

## Étape 1 — Dump en lecture seule de l'unité source (celle qui fonctionne)

**Aucune écriture, aucun effacement.** Halte le CPU brièvement (requis
par SWD pour lire la mémoire), lit, puis relance l'exécution normale.

```bash
export PATH="/c/ncs/tools/xpack-openocd-0.12.0-7/bin:$PATH"
BOARD_DIR="C:/ncs/vendor/platform-seeedboards/zephyr/boards/arm/xiao_nrf54lm20a"
DUMP="C:/ncs/projects/nRF54LM20A/xiao_door_sensor/golden-image/<nom>.bin"

openocd -s "$BOARD_DIR/support" -f "$BOARD_DIR/support/openocd.cfg" \
  -c "cmsis-dap vid_pid 0x2886 0x0068" -c "cmsis-dap backend hid" -c "adapter speed 500" \
  -c "init" -c "reset halt" \
  -c "dump_image \"$DUMP\" 0x0 117396" \
  -c "reset" -c "exit"
```

`-c "cmsis-dap backend hid"` est obligatoire (voir § « Notes de
connexion SWD » plus bas). `117396` = taille en octets de l'image
actuelle (confirmée par `verify_image` au dernier flash connu — ajuster
si l'image d'or change).

**Obligatoire après ce dump : débrancher puis rebrancher complètement le
câble USB-C de l'unité source.** Une session SWD, même en lecture seule,
peut laisser le SoC en « Debug Interface mode » (émule le System OFF au
lieu de l'appliquer réellement) tant que le cycle d'alimentation complet
n'a pas eu lieu — sans ça, l'unité source pourrait sembler se comporter
différemment alors que son firmware n'a pas changé.

## Étape 2 — Convertir en `.hex` (une fois, pas à chaque clonage)

```bash
export PATH="/c/ncs/toolchains/dcbdc366a1/opt/zephyr-sdk/gnu/arm-zephyr-eabi/bin:$PATH"
arm-zephyr-eabi-objcopy -I binary -O ihex --change-address 0x0 \
  "<dump>.bin" "<dump>.hex"
```

## Étape 3 — Flasher l'unité cible avec ce fichier

```bash
export PATH="/c/ncs/tools/xpack-openocd-0.12.0-7/bin:$PATH"
BOARD_DIR="C:/ncs/vendor/platform-seeedboards/zephyr/boards/arm/xiao_nrf54lm20a"

openocd -s "$BOARD_DIR/support" -f "$BOARD_DIR/support/openocd.cfg" \
  -c "cmsis-dap vid_pid 0x2886 0x0068" -c "cmsis-dap backend hid" -c "adapter speed 500" \
  -c "init" -c "reset halt" \
  -c "nrf54lm20a-load \"<dump>.hex\"" \
  -c "reset halt" \
  -c "verify_image \"<dump>.hex\"" \
  -c "reset" -c "exit"
```

`verify_image` doit confirmer exactement la même taille que le dump
d'origine — c'est la garantie que la cible est désormais byte-for-byte
identique à la source vérifiée.

## Étape 4 — Vérifier en fonctionnement réel

Le clonage seul ne suffit pas à conclure : **confirmer la consommation
au PPK2** (~20-22 µA moyenne attendue au repos, protocole établi du
projet — PPK2 et USB-C jamais connectés en même temps) avant de
considérer l'unité cible comme déployée avec succès. Ajouter le résultat
au tableau « Historique de clonage » ci-dessus.

---

## Notes de connexion SWD (pont CMSIS-DAP SAMD11)

- **`-c "cmsis-dap backend hid"` fait partie intégrante de toutes les
  commandes ci-dessus depuis le 2026-08-29 — ne jamais l'omettre.** Sans
  cette ligne, le pont SAMD11 utilise par défaut le backend WinUSB v2,
  qui peut s'énumérer correctement (visible dans Windows, serial lu par
  OpenOCD) tout en échouant sur **chaque** transaction réelle (`error
  submitting USB read/write: Entity not found`, `could not claim
  interface: Operation not supported`) — constaté sur l'unité #02,
  persistant après redémarrage des process `nrfutil`, reset PnP, cycle
  d'alimentation complet et changement de vitesse d'horloge ; seul le
  passage au backend HID (v1) a résolu le problème.
- Pont parfois intermittent même avec le backend HID (`unable to find a
  matching CMSIS-DAP device`, ou échec de connexion DP `cannot read
  IDR`) — relancer la même commande suffit systématiquement jusqu'ici,
  jusqu'à 5-10 tentatives. Pas besoin de débrancher/rebrancher pour ce
  symptôme précis (différent de celui ci-dessus : ici la commande
  échoue immédiatement ou pendant l'examen DP, sans jamais aboutir à une
  transaction applicative).
- **Instabilité après déconnexion/reconnexion USB-C côté PC** (constaté
  2026-08-30) : après un cycle déconnexion/reconnexion PC (carte non
  arrêtée proprement au préalable), la carte peut se retrouver dans un
  état où le flash échoue de façon persistante, au-delà du simple pont
  CMSIS-DAP intermittent décrit ci-dessus. **Méthode de récupération
  qui fonctionne, décrite par l'utilisateur** : déconnecter la carte du
  PC → connecter les fils PPK2 (BAT+/BAT-) → appliquer la tension
  depuis le PPK2 (mode Source meter) → faire une mesure → couper
  l'alimentation PPK2 → déconnecter les fils PPK2 → reconnecter la
  carte au port USB-C du PC. La LED rouge se rallume et la carte
  redevient joignable côté PC (SWD + énumération USB normales) à ce
  moment-là. Hypothèse non vérifiée à ce stade : un cycle
  d'alimentation complet (via PPK2, jamais simultané avec l'USB-C —
  voir protocole PPK2 du projet) réinitialise un état que la simple
  déconnexion/reconnexion USB-C ne réinitialise pas. **Cause exacte non
  encore investiguée** — ce point est consigné ici pour référence, à
  creuser séparément du travail de diagnostic consommation en cours.
- Identifier la carte branchée avant toute action :

```powershell
Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -like "*VID_2886*" } | Select-Object FriendlyName, InstanceId, Status
```
→ ligne `Périphérique USB composite` = numéro de série du pont USB↔SWD,
fixe par carte (comparer au tableau des unités déployées dans
`Configuration-nRF54LM20A-System-ON-IDLE.md`). Le log OpenOCD lui-même
affiche aussi `CMSIS-DAP: Serial# = ...` à la connexion — toujours
vérifier que ce numéro correspond bien à l'unité attendue avant d'écrire
quoi que ce soit.
- Toujours vérifier avec `verify_image` (jamais `dump_image`+`cmp` pour
  une vérification post-flash, faux positifs sur les trous RRAM — le
  dump en lecture seule de l'Étape 1 ci-dessus sert à un usage différent,
  copier tel quel, pas à comparer).
