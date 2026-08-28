# Transition — démarrage XIAO nRF52840 Sense

**Document de démarrage pour une nouvelle conversation**, dédiée au projet
XIAO nRF52840 Sense (distinct de la nRF54LM20A). Contient uniquement des
faits vérifiés au 2026-08-25 — aucune supposition non signalée comme
telle. Lire en premier `C:\ncs\CLAUDE.md` (règles de travail, valables
pour tout `C:\ncs\`, y compris ce projet).

## État validé sur matériel réel (2026-08-25) — à relire en premier

Cette section résume uniquement ce qui **fonctionne et a été confirmé sur
matériel réel**, pour repartir directement de là sans revalider tout
l'historique de tentatives ci-dessous (conservé plus bas pour référence,
notamment pour ne PAS reproduire ce qui a été abandonné).

**Firmware** : `C:\ncs\projects\xiao_nrf52840_door_sensor\`
**Cible de build** : `xiao_ble/nrf52840/sense`

**Les 3 unités XIAO nRF52840 Sense — flashées avec le même firmware
validé, adresses BLE fixes (dérivées du FICR, propres à chaque puce)** :

| Unité | Adresse MAC BLE | Nom HA (auto, 4 derniers chiffres hex de la MAC) |
|---|---|---|
| #1 (test initial) | `F2:1A:DF:BC:F9:7B` | `XIAO-DOOR F97B` |
| #2 | `D3:29:C5:12:5F:51` | `XIAO-DOOR 5F51` |
| #3 | `DC:07:4F:94:1C:F3` | `XIAO-DOOR 1CF3` |

Les trois à ajouter à l'allowlist du proxy BLE ESPHome côté Home
Assistant pour apparaître (fait le 2026-08-25 par l'utilisateur pour les
trois). Convention de nommage HA confirmée cohérente entre les trois
unités (même schéma automatique, aucune incohérence observée).

**Architecture validée — System ON, PAS System OFF** :
- `CONFIG_PM=y` (gestion automatique de l'inactivité CPU par l'idle
  thread Zephyr standard) ; **volontairement PAS de `CONFIG_PM_DEVICE=y`**
  (a causé une régression sévère lors d'un essai — change le comportement
  de tous les drivers du projet, pas seulement celui visé).
- `main()` initialise une fois puis boucle indéfiniment (`k_sleep()`
  entre les cycles de sondage) — pas de `sys_poweroff()`, pas de
  `retained_mem`, pas de redémarrage en fonctionnement normal.
- Flash QSPI externe (`p25q16h`) désactivée au niveau devicetree
  (`&qspi { status = "disabled"; };`), pas par suspension runtime.
- IMU : `zephyr,deferred-init` sur `lsm6ds3tr_c` (nécessaire, cause
  indépendante de l'architecture d'alimentation — régulateur
  `regulator-boot-on` pas assez rapide pour l'init auto du driver) +
  `CTRL6_C` bit4 (`XL_HM_MODE=1`, mode low-power à 12,5 Hz).
- Batterie : ADC direct P0.31/AIN7, activé par P0.14, diviseur 1510/510 Ω
  — validé (37%/3712mV, valeur plausible).
- **`POLL_INTERVAL_MS = 1000`** (1s) — réactivité au mouvement confirmée
  bien meilleure qu'à 2000ms, validée par l'utilisateur. Aucune limite
  basse connue autre que l'ODR accéléromètre (12,5 Hz, ~80ms/échantillon).
- Trames BTHome A/B/C envoyées et reçues correctement (mouvement,
  pitch/roll/yaw, batterie, température) — plusieurs mouvements
  consécutifs détectés et transmis sans blocage ni redémarrage.

**Ce qui a été essayé et ABANDONNÉ — ne pas reproduire** : architecture
System OFF + réveil par seuil watermark de la FIFO IMU (simulant un
"tick" périodique en l'absence de GRTC sur nRF52840). Non fiable sur
matériel réel quelle que soit l'ODR testée (1,6 Hz comme 12,5 Hz) après
le tout premier cycle suivant un flash — voir détail complet plus bas
(§ « Cinquième révision »). Aucun exemple Zephyr/ST/Nordic ne combine ce
mécanisme avec un réveil sur mouvement IMU sur une même broche.

**Prochaine étape prévue** : mesure de consommation réelle au PPK II
(Power Profiler Kit II) pour décider si `POLL_INTERVAL_MS` peut encore
descendre (ex. 200-500ms) sans impact significatif sur l'autonomie.

## Objectif

Reproduire sur la XIAO nRF52840 Sense le même profil BTHome v2 (trames A/B/C,
mêmes Object IDs, mêmes entités Home Assistant) déjà validé et déployé sur
la XIAO nRF54LM20A Sense — voir `xiao_nrf54lm20a_project_notes.md` pour la
référence complète du firmware `xiao_door_sensor` (§ « Référence BTHome v2 »
et § « Entités Home Assistant générées ») à reproduire à l'identique côté
contenu des trames.

## Matériel disponible

- **3× XIAO nRF52840 Sense** en test.
- **1× nRF52840 DK** commandée en secours/débogage (sonde SWD/J-Link
  standard, utile si le flux UF2 pose problème) — fichiers matériels déjà
  présents dans `C:\ncs\projects\nRF52840\` (guides utilisateur PDF,
  firmware J-Link OB, fichiers de conception Altium/Gerber). Aucun projet
  firmware XIAO nRF52840 n'existe encore dans ce dossier — à créer.
- **Démarrage proposé par l'utilisateur** : commencer par **une seule
  unité en BLE**, même approche incrémentale que pour la nRF54LM20A.

## Ce qui est déjà vérifié (2026-08-25)

- **Board cible nativement supporté par NCS 3.4.0**, aucun clone vendor
  nécessaire (contrairement à la nRF54LM20A) :
  `C:\ncs\v3.4.0\zephyr\boards\seeed\xiao_ble\xiao_ble_nrf52840_sense.dts`
  existe déjà dans ce checkout. Cible de build probable :
  `xiao_ble/nrf52840/sense` (nom exact à confirmer via
  `west boards | grep xiao_ble` ou équivalent au moment de configurer le
  projet).
- **Même IMU que la nRF54LM20A Sense** : LSM6DS3TR-C, driver Zephyr
  `st,lsm6dsl`, même adresse I2C (`lsm6ds3tr-c@6a`) — vérifié directement
  dans `xiao_ble_nrf52840_sense.dts:40-41`. Implique que la logique de
  lecture accéléromètre/gyroscope et les registres de réveil matériel bruts
  (`WAKE_UP_THS`/`WAKE_UP_DUR`/`MD1_CFG`/`TAP_CFG`, déjà documentés et
  vérifiés pour la nRF54LM20A dans `xiao_nrf54lm20a_project_notes.md` §
  « Registres IMU ») sont vraisemblablement réutilisables tels quels — **à
  confirmer sur le matériel réel avant de considérer ça acquis**.

## Ce qui n'est PAS encore vérifié — à rechercher avant de coder

- **Mécanisme bas-conso du nRF52840** : le firmware `xiao_door_sensor`
  actuel dépend fortement d'éléments spécifiques à la génération nRF54L
  (`retained_mem` avec CRC, GRTC pour les réveils périodiques et la RAM
  retenue à travers le System OFF). Le nRF52840 est une architecture
  différente (Cortex-M4F vs M33, pas de GRTC) — **son mécanisme de
  réveil bas-conso et de rétention mémoire doit être recherché
  spécifiquement**, sur le même modèle que `Recherche-Reveil-Materiel-XIAO.md`
  (déjà fait pour la nRF54LM20A). Ne pas supposer que le code System OFF
  actuel se porte tel quel.
- **Procédure de flash réelle** : bootloader UF2 probable (drag-and-drop,
  pas de sonde SWD requise a priori, comme la plupart des cartes XIAO BLE
  Seeed) — **jamais testé sur ce matériel précis**, à valider dès la
  première tentative. La DK reste le filet de sécurité si le flux UF2
  pose problème.
- **Broches exactes** (bouton utilisateur, IRQ IMU) sur ce devicetree —
  ne pas supposer qu'elles correspondent à celles de la nRF54LM20A
  (`gpio0 9` pour le bouton, etc.) : à relire dans
  `xiao_ble_nrf52840_sense.dts`/`xiao_ble_common.dtsi` au moment de coder.
- **PMIC / gestion batterie** : la nRF54LM20A utilise un nPM1300 lu en
  I2C pour la tension batterie (`SENSOR_CHAN_GAUGE_VOLTAGE`) — à vérifier
  si la XIAO nRF52840 Sense a un circuit de charge équivalent et
  comparable, ou un mécanisme différent (ex. lecture ADC directe de la
  tension batterie, plus courant sur les anciennes XIAO BLE).

## Réutilisation prévue depuis `xiao_door_sensor`

**Probablement réutilisable tel quel ou presque** :
- Format des trames BTHome v2 A/B/C (structure des octets, Object IDs) —
  à garder strictement identique pour la cohérence des entités Home
  Assistant entre les deux familles de capteurs.
- Logique de détection de mouvement (delta accéléromètre), hystérésis
  d'angle, anti-rafale (fenêtre glissante `FRAME_A_MAX_PER_MIN`) — logique
  indépendante du SoC.
- Lecture des registres IMU bruts en I2C direct (même puce, même adresse).
- Calcul pitch/roll par projection du vecteur gravité.

**À NE PAS copier aveuglément, à réécrire pour ce SoC** :
- Tout ce qui touche au réveil matériel System OFF, à `retained_mem`/CRC,
  et aux échéances GRTC (`z_nrf_grtc_wakeup_prepare`, `z_nrf_grtc_timer_read`)
  — spécifique nRF54L, sans équivalent direct sur nRF52840.
- La configuration `arm_gpio_wake()`/`configure_imu_wakeup()` — les
  mécanismes de réveil GPIO diffèrent probablement entre les deux puces.

## Résultats de la recherche bas-conso (étape 2, 2026-08-25)

Recherche faite par lecture directe du code source NCS 3.4.0 local
(`zephyr/soc/nordic/`, `modules/hal/nordic/nrfx/`) et confirmation Nordic
DevZone (forum support officiel) pour les points non vérifiables dans le
code seul.

**Corrige l'hypothèse initiale ci-dessus** — rétention RAM (`retained_mem`
+ CRC) réutilisable sur nRF52840, contrairement à ce qui était supposé :
- `zephyr/soc/nordic/nrf52/Kconfig:14` — la série nRF52 fait
  `select HAS_NORDIC_RAM_CTRL`.
- `zephyr/drivers/retained_mem/Kconfig.nrf:12-16` — le driver
  `RETAINED_MEM_NRF_RAM_CTRL` (celui qui porte le nœud devicetree
  `zephyr,retained-ram` + CRC) ne dépend que de `HAS_NORDIC_RAM_CTRL` +
  `POWEROFF`, pas d'une série précise.
- `modules/hal/nordic/nrfx/helpers/nrfx_ram_ctrl.h:121-133` — sur nRF52
  (`POWER_PRESENT`), la rétention passe par les bits
  `POWER->RAM[n].POWERSET/POWERCLR` (registre `RAM[9]` vu dans
  `nrf52840.h:1050`) au lieu du périphérique `MEMCONF` de la nRF54L —
  mécanisme différent, même résultat fonctionnel.
- `zephyr/soc/nordic/common/poweroff.c:33-90` — `z_sys_poweroff()` gère
  les deux familles dans la même fonction générique : applique la
  rétention RAM puis appelle `nrf_power_system_off(NRF_POWER)` pour nRF52
  (ligne 79-80) au lieu de `nrf_regulators_system_off()`.

**Confirme l'hypothèse initiale** — pas de réveil périodique par
minuterie depuis System OFF sur nRF52840 (contrairement au GRTC nRF54L) :
- Confirmé par Nordic DevZone : depuis System OFF, seules trois sources
  peuvent réveiller la nRF52840 — signal **DETECT** du GPIO, **ANADETECT**
  du comparateur bas-conso LPCOMP, ou **SENSE** du champ NFC. Aucune
  source RTC/minuterie.
- **Conséquence architecture** : le réveil périodique (heartbeat/lecture
  batterie) piloté par GRTC sur la nRF54LM20A ne peut pas être reproduit
  en sortant de System OFF sur cette puce. Alternative standard : rester
  en **System ON** avec veille basse conso pilotée par RTC (idle CPU en
  WFE/WFI entre les ticks RTC) pour la partie périodique, et ne descendre
  en System OFF que pour les longues périodes de silence — vrai choix
  d'architecture à trancher avant de coder, pas une simple portabilité de
  fonction.

**Point matériel nouveau, spécifique à cette puce** (fiche technique
Nordic §6.10.2 "Port event", confirmé applicable à la XIAO nRF52840 via
forum Seeed) : des événements PORT parasites peuvent se déclencher pendant
la configuration du GPIO en mode SENSE si les interruptions GPIOTE ne sont
pas masquées pendant la configuration. Séquence requise avant d'entrer en
System OFF : `NRF_GPIOTE->INTENCLR` avant de configurer la broche de
réveil, effacer `EVENTS_PORT`, puis `INTENSET`. Concerne directement la
configuration de l'IRQ IMU comme source de réveil.

**Diagnostic du réveil** : registre `RESETREAS` bit 16 (`0x00010000`)
indique un réveil par DETECT GPIO — registre et mécanisme différents de
celui utilisé côté nRF54L (`nrfx_reset_reason_clear`, spécifique à cette
série d'après `poweroff.c:73-78`).

## Procédure de flash — validée sur matériel réel (2026-08-25)

Testée sur l'unité nRF52840 branchée en USB (identifiée COM10 en
firmware d'usine Seeed, VID/PID `2886:8045` — distincte de l'unité #01
nRF54LM20A, VID/PID `2886:0068`, pont CMSIS-DAP SAMD11 documenté dans
`xiao_nrf54lm20a_project_notes.md`).

**Séquence confirmée fonctionnelle** :
1. `west build -b xiao_ble/nrf52840/sense -d <dir> --pristine always <app>`
   → génère `zephyr.uf2` (confirmé par le build system, pas de config
   supplémentaire nécessaire).
2. **Double-tap du bouton RESET physique** sur la carte → LED se met en
   veille bootloader, un lecteur amovible `XIAO-SENSE` (FAT) apparaît
   (lettre observée : `D:`).
3. Copier `zephyr.uf2` à la racine de ce lecteur → la carte redémarre
   automatiquement sur le nouveau firmware, le lecteur `XIAO-SENSE`
   disparaît.
4. Un nouveau port série apparaît sous un VID/PID différent de celui du
   firmware d'usine Seeed si l'appli utilise le stack USB Zephyr par
   défaut (observé : `2FE3:0004`, VID par défaut du projet Zephyr — sera
   probablement différent si un VID/PID custom est configuré plus tard).

**Test réalisé** : `zephyr/samples/basic/blinky` flashé avec succès sur
la cible `xiao_ble/nrf52840/sense` — LED clignote (rouge) après flash,
confirmé visuellement par l'utilisateur. Flash = 48776 B / 788 KB (6.04%),
RAM = 15160 B / 256 KB (5.78%) pour ce firmware minimal.

**Non testé/observé, à surveiller** : au moment du double-tap, les deux
ports COM présents (unité #01 nRF54LM20A *et* la nRF52840 elle-même) ont
brièvement disparu de la liste Windows avant de réapparaître après le
flash — cause non investiguée (pas bloquant, à noter si un problème
similaire réapparaît).

## Firmware `xiao_nrf52840_door_sensor` créé et validé sur matériel réel (2026-08-25)

Projet créé dans `C:\ncs\projects\xiao_nrf52840_door_sensor\` (`CMakeLists.txt`,
`prj.conf`, `boards/xiao_ble_nrf52840_sense.overlay`, `src/main.c`), porté
depuis `xiao_door_sensor` (nRF54LM20A). Build : `west build -b
xiao_ble/nrf52840/sense -d build --pristine always`, Flash 154 KB/788 KB
(19,1%), RAM 33,6 KB/252 KB (13,0%), région `RetainedMem` 4 KB reconnue.

**Différences de contenu par rapport au firmware nRF54LM20A** (décidées
avec l'utilisateur) :
- Trame A sans objet bouton (`OBJ_BUTTON`) — aucun GPIO bouton sur cette
  carte (seul le bouton RESET physique existe, réservé au bootloader UF2).
- Trame B : tension batterie lue par ADC direct (P0.31/AIN7, activé par
  P0.14, diviseur 1510/510 Ω) au lieu du nPM1300 (pas de PMIC sur cette
  carte) — pattern confirmé via le code source de la lib communautaire
  `honvl/Seeed-Xiao-NRF52840-Battery`.
- Réveil périodique (santé/heartbeat) remplacé par un tick ~5 min basé sur
  le seuil watermark de la FIFO de l'IMU (`FIFO_CTRL1/2/3/5`, INT1_FTH),
  puisque System OFF sur nRF52840 n'a pas de source de réveil par
  minuterie (voir § recherche bas-conso ci-dessus). Motion et tick étant
  tous deux routés sur la même broche INT1/DETECT, désambiguïsés au
  réveil par lecture I2C de `WAKE_UP_SRC` (bit WU_IA) et `FIFO_STATUS2`
  (bit WaterM) — logique nouvelle, absente du firmware nRF54LM20A.

**Bug trouvé et corrigé lors du premier test matériel** : `LSM6DSL: Failed
to initialize chip` au boot (avant même `main()`) — le nœud
`lsm6ds3tr_c` n'a pas de propriété `vdd-supply` reliant le capteur à son
régulateur (`lsm6ds3tr-c-en`, `regulator-boot-on`) dans le devicetree de
cette carte, donc rien ne fait attendre l'init automatique du driver
jusqu'à ce que l'alimentation soit stable. Corrigé par
`&lsm6ds3tr_c { zephyr,deferred-init; };` dans l'overlay (même correctif
que l'overlay nRF54LM20A, cause différente : là-bas c'était le LDO1 du
PMIC, activé par du code applicatif, pas `regulator-boot-on`). Diagnostiqué
via un délai de boot temporaire (retiré depuis) + capture série
automatisée (script PowerShell, `SerialPort` .NET).

**Validé sur matériel réel après correction** (log complet capturé) :
- IMU : init OK, réveil configuré (THS=0x01 DUR=0x00, watermark=1440 mots).
- **Batterie (ADC P0.14/P0.31)** : lecture réussie, 37% (3712 mV) — valeur
  plausible, aucun signe de dommage matériel.
- Bluetooth : contrôleur SoftDevice initialisé, **adresse BLE fixe de
  cette unité : `F2:1A:DF:BC:F9:7B`** (à ajouter à l'allowlist du proxy
  BLE ESPHome côté Home Assistant avant toute vérification via HA).
- Trame B envoyée : battery=37% (3712mV) temp=24,42°C.
- Trame A (heartbeat) envoyée : pitch=0.3° roll=-89.6°.
- Trame C envoyée : accel_mag≈9,81 m/s² (cohérent avec 1g au repos),
  gyro_mag=3475.

**Non encore testé** : le cycle de tick périodique réel (~5 min, watermark
FIFO) et la désambiguïsation mouvement/tick sur un vrai réveil depuis
System OFF (le test ci-dessus couvre uniquement le tout premier boot,
`fresh_session=true`, qui n'emprunte pas ce chemin) — dérive de l'ODR
1,6 Hz (oscillateur IMU, pas un quartz) toujours non mesurée.

### Deuxième bug trouvé et corrigé (2026-08-25, après ajout dans HA) : réveil sur mouvement inopérant

Appareil visible dans Home Assistant (entités BTHome créées, valeurs du
premier boot cohérentes), mais **bouger la carte ne produisait plus aucune
nouvelle trame**. Cause trouvée dans la datasheet locale (Table 52,
"Accelerometer ODR register setting") : le code ODR_XL=1011 utilisé par
`arm_imu_tick()` pour les 1,6 Hz de sommeil (nécessaire pour un tick de
~5 min, cf. § recherche bas-conso) n'a de définition valide ("1.6 Hz (low
power only)") que sous **XL_HM_MODE=1** (bit4 de CTRL6_C, 0x15) — rien
d'équivalent n'existe sous XL_HM_MODE=0, qui est la valeur par défaut du
registre et que rien dans le firmware ne modifiait. Sans ce bit, la valeur
ODR_XL=1011 est indéfinie : l'accéléromètre cessait probablement
d'échantillonner correctement pendant le sommeil, cassant à la fois le
réveil sur mouvement (WAKE_UP_THS n'a plus de données fraîches à comparer)
et le tick (la FIFO ne se remplit plus).

**Pourquoi le firmware nRF54LM20A n'avait pas ce problème** : il utilise
uniquement l'ODR_XL=0001 (12,5 Hz), qui est définie et fonctionnelle *quel
que soit* XL_HM_MODE (Table 52 : "12,5 Hz (low power)" vs "12,5 Hz (high
performance)" — même fréquence réelle dans les deux cas, seule la
consommation change). Seul le code spécial 1,6 Hz, propre à ce portage,
est concerné.

**Correctif** : écriture `CTRL6_C bit4 (XL_HM_MODE) = 1`, une seule fois
dans `configure_imu_wakeup()` (comme les autres registres statiques) —
sans conséquence sur les 12,5 Hz utilisées en fenêtre active (valides dans
les deux cas). Revérifié sur matériel réel : le réveil sur mouvement
fonctionne à nouveau, **mais avec une latence perceptible** par rapport à
la nRF54LM20A (confirmé par l'utilisateur) — attendu, cf. § suivant.

### Troisième révision (2026-08-25) : latence de réveil sur mouvement, ODR ramenée à 12,5 Hz permanent

Le réveil sur mouvement fonctionnait après le correctif XL_HM_MODE, mais
avec un délai notable par rapport à la nRF54LM20A. Cause : l'accéléromètre
n'échantillonne qu'à 1,6 Hz pendant le sommeil (une lecture ~625 ms),
contre 12,5 Hz en continu côté nRF54LM20A (~80 ms) — l'IMU n'a qu'une
seule ODR à la fois, partagée entre le seuil de réveil mouvement et le
remplissage FIFO du tick, donc l'un ne peut être amélioré sans dégrader
l'autre.

**Décision utilisateur** : revenir à 12,5 Hz en permanence (réactivité
mouvement identique à la nRF54LM20A), au prix d'un tick périodique
beaucoup plus court — plafonné à ~54 s par la limite 11 bits du registre
watermark FTH (2047 mots max), pas par la capacité totale de la FIFO
(4096 mots). Nouveau `TICK_PERIOD_MS = 50 s` (divise proprement les
intervalles santé/heartbeat : 18 et 72 ticks respectivement), watermark
= 1875 mots (625 échantillons XYZ × 3). Plus de bascule d'ODR entre
éveil et sommeil — `arm_imu_tick()` ne fait plus que réarmer la FIFO.
Bénéfice induit : XL_HM_MODE=1 s'applique maintenant en continu, donc les
12,5 Hz tournent en mode "low power" (~9 µA visé, Table 4) au lieu de
"high performance" par défaut.

**Contrepartie non mesurée** : réveils ~6x plus fréquents (toutes les
~50 s au lieu de ~5 min) — impact réel sur l'autonomie de la batterie
inconnu, à surveiller en usage réel.

### Quatrième bug (2026-08-25) : plus aucun cycle après le premier ne fonctionne, depuis le passage à 12,5 Hz permanent

Après le passage à 12,5 Hz, plus rien ne remontait dans HA après le tout
premier cycle (celui juste après un flash), quel que soit le mouvement.
Diagnostiqué par capture série instrumentée (points de contrôle,
`CONFIG_LOG_MODE_IMMEDIATE=y` temporaire pour éviter les pertes de
messages en mode différé) : le second cycle s'arrêtait net juste après le
message de boot, avant même `set_fixed_ble_identity()`.

**Deux hypothèses testées sans effet** (pistes de recherche initiales,
non confirmées comme cause réelle) :
- `bt_disable()` avant `sys_poweroff()` (pratique documentée Nordic
  DevZone pour les transactions EasyDMA en cours) — implémenté, seul,
  sans effet observable.
- Suspension de la flash QSPI externe (`p25q16h`) via
  `pm_device_action_run()` — nécessite `CONFIG_PM_DEVICE=y`, qui a causé
  une **régression bien plus grave** (plus aucun cycle, même le premier)
  en changeant le comportement de tous les drivers du projet. Retiré
  immédiatement. `bt_disable()` seul (sans `CONFIG_PM_DEVICE`) conservé
  par prudence, cause non confirmée mais fixe documentée sans risque connu.

**Cause racine identifiée** (recherche approfondie, deux sources
indépendantes) :
- Nordic DevZone (comportement documenté nRF52, plusieurs fils de
  discussion concordants) : si le "Detect latch" du signal GPIO SENSE est
  déjà actif au moment d'entrer en System OFF, la puce se réveille
  instantanément, en boucle continue -- trop rapide pour que l'énumération
  USB ait le temps de se stabiliser.
- STMicroelectronics (documentation applicative officielle du
  LSM6DS3TR-C) : avec `WAKE_UP_DUR=0x00` (réglage repris tel quel de la
  nRF54LM20A), l'interruption wake-up se redéclenche à CHAQUE échantillon
  dépassant le seuil, sans débounce.
- Lien de cause à effet propre à ce portage : à 1,6 Hz (réglage initial),
  la fenêtre de risque était étroite. Passé à 12,5 Hz en continu (8x plus
  d'échantillons/s), le seuil très sensible (~31 mg, `WAKE_UP_THS=0x01`)
  est presque toujours dépassé par du bruit résiduel -- le signal DETECT
  reste quasi continuellement actif, donc chaque `sys_poweroff()` déclenche
  un réveil immédiat en boucle.

**Correctifs implémentés** (compilés, test matériel en cours) :
1. `WAKE_UP_DUR` : `0x00` → `0x40` (WAKE_DUR=2, débounce ~160 ms à
   12,5 Hz) -- recommandation ST directe pour ce symptôme exact.
2. Nouvelle fonction `wait_for_wake_src_clear()`, appelée juste avant
   `sys_poweroff()` : relit `WAKE_UP_SRC` et attend (max 500 ms, poll
   50 ms) que le bit `WU_IA` retombe s'il est encore actif -- traite
   directement le mécanisme documenté par Nordic DevZone.

## Plan de démarrage proposé

1. Nouvelle conversation dédiée à ce projet (celle-ci sert de point de
   départ).
2. **Recherche du mécanisme bas-conso du nRF52840 en premier**, avant
   d'écrire du code — même démarche que pour la nRF54LM20A
   (`Recherche-Reveil-Materiel-XIAO.md`), pour ne pas découvrir des
   surprises en cours de route.
3. Créer le projet firmware (nouveau dossier, ex.
   `C:\ncs\projects\xiao_nrf52840_door_sensor\` — nom à confirmer avec
   l'utilisateur) en repartant de la structure BTHome/logique métier de
   `xiao_door_sensor`, adaptée au SoC.
4. Flasher et valider sur **une seule unité** avant de dupliquer sur les
   deux autres — même précaution que pour la nRF54LM20A.
5. Rappel des règles de travail (`CLAUDE.md`, déjà applicable à ce
   dossier) : communiquer tous les détails avant chaque action, vérifier
   l'état réel avant de supposer qu'il tient toujours, ne jamais reposer
   une question déjà répondue, ne jamais remettre en cause un fait énoncé
   par l'utilisateur ni chercher une cause hors du code, vocabulaire
   technique précis (non câblé / non configuré / non lu / non calculé).

## Cinquième révision (2026-08-25) : abandon de System OFF, réécriture en architecture System ON

Après quatre correctifs successifs sur l'architecture System OFF + réveil
FIFO watermark (deferred-init, XL_HM_MODE, débounce WAKE_UP_DUR,
vérification WAKE_UP_SRC avant sommeil, `bt_disable()`) sans résoudre le
problème de fond (plus aucun cycle après le premier ne fonctionnait, sur
~8 essais), recherche approfondie demandée explicitement par
l'utilisateur plutôt que de continuer à corriger à l'aveugle.

**Trouvailles déterminantes** :
- Aucun exemple Zephyr/ST/Nordic ne combine System OFF + watermark FIFO
  + seuil de mouvement sur une même broche d'interruption IMU —
  combinaison sans précédent connu, probable source de l'instabilité.
- Nordic DevZone : en veille System ON avec `CONFIG_PM=y`, ~2 µA visés,
  contre ~1 µA en System OFF — écart négligeable pour l'autonomie d'un
  capteur de porte sur pile.
- Le composant BTHome-ESPHome pour nRF52/Zephyr (projet publié) reste en
  System ON en permanence (`bt_enable()` + boucle Zephyr standard), sans
  aucune trace de System OFF, `retained_mem` ou registre de réveil
  matériel.
- Dépôt de référence dédié à cette carte précise —
  [github.com/qarnet/Zephyr-XIAO-nRF52840-Ultra-Low-Power](https://github.com/qarnet/Zephyr-XIAO-nRF52840-Ultra-Low-Power)
  — confirme le patron standard : `CONFIG_PM=y` (gestion automatique de
  l'inactivité CPU) + `k_sleep()`, sans `sys_poweroff()`. Identifie aussi
  la flash QSPI externe (même puce `p25q16h` que notre carte) comme
  principale source de consommation résiduelle si non désactivée.

**Réécriture appliquée** (`src/main.c`, `prj.conf`, l'overlay) :
- Suppression complète : `retained_mem`/CRC, région RAM retenue,
  `sys_poweroff()`/`sys_reboot()` par cycle, registres IMU de réveil
  matériel (`WAKE_UP_THS/DUR/MD1_CFG`, `FIFO_CTRL*`, `INT1_CTRL`),
  distinction mouvement/tick/cold-boot, journal de diagnostic persistant.
- `main()` initialise une fois puis boucle indéfiniment (`k_sleep()`
  entre les cycles de sondage, 2s) — le CPU s'endort automatiquement
  grâce à `CONFIG_PM=y`, la RAM et l'état applicatif (packet_id, derniers
  angles, yaw) redeviennent de simples variables statiques.
- Gardé inchangé : structure des trames BTHome A/B/C, détection de
  mouvement/hystérésis d'angle/anti-rafale, calcul pitch/roll,
  intégration gyroscopique du yaw, lecture batterie ADC, identité BLE
  fixe, correctif `zephyr,deferred-init` (toujours nécessaire,
  indépendant de l'architecture d'alimentation), `CTRL6_C`/`XL_HM_MODE`
  (toujours bénéfique pour le courant à 12,5 Hz).
- Nouveau dans l'overlay : `&qspi { status = "disabled"; };` (flash
  externe jamais utilisée, coupée au niveau devicetree plutôt que par
  suspension runtime — évite `CONFIG_PM_DEVICE=y`, qui avait causé une
  régression sévère lors d'un essai précédent).

**Compilé avec succès** (Flash 150 KB/788 KB 18,6%, RAM 32,9 KB/256 KB
12,6%, plus de région RAM retenue séparée). **Pas encore testé sur
matériel réel** au moment de l'écriture de cette note.
