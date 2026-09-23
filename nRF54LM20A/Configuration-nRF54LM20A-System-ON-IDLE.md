# Configuration fonctionnelle — XIAO nRF54LM20A, architecture System ON IDLE

**Document de référence unique pour l'architecture actuellement en service.**
Ne contient que l'état qui fonctionne aujourd'hui — pas de narration de
tests, pas d'historique daté. Pour le détail complet du raisonnement et
des tests qui ont mené à cet état, voir `archive/docs-historique/`. Pour
la question ouverte auprès de Nordic, voir
`Nordic-Support-Report-XIAO-nRF54LM20A.md`.

---

## 1. Ce qui fonctionne aujourd'hui

- **Board** : Seeed Studio XIAO nRF54LM20A Sense (SoC nRF54LM20A, PMIC
  nPM1300, IMU LSM6DS3TR-C).
- **Firmware** : `xiao_door_sensor/` — architecture **System ON IDLE**
  (le SoC ne redémarre jamais en fonctionnement normal ; `CONFIG_PM=y`
  assure un vrai sommeil CPU tickless entre les cycles de sondage).
- **Consommation mesurée (PPK2)** : **#01 : 15,65 µA** (image d'or
  actuelle `golden-image/unit01-verified-2026-09-23-ABD-VBUSFIX-15uA`) ;
  **#02 : ~16-17 µA** (même firmware sans le correctif VBUS, mesures
  60 s/240 s/360 s — voir
  `archive/docs-historique/Plan-Reduction-Consommation-2026-09-22.md` et
  `archive/docs-historique/Journal-Travail-2026-09-22.md`). Étapes A
  (I2C PMIC 400 kHz), B (IMU 6→3 transactions) et D (accéléromètre
  833 Hz HP) appliquées. Contre 70-144 µA pour l'ancienne architecture
  (System OFF + redémarrage complet par cycle).
- **Fonctionnalités actives (parité de production atteinte le 2026-08-29)** :
  trame BTHome v2 santé (batterie %, tension, température die) toutes les
  15 min ; trame mouvement/orientation (pitch/roll/yaw, activité,
  bouton, tamper=0, vibration=0) sur événement + heartbeat 60 min ; trame
  IMU brut (magnitudes + accélération signée par axe) envoyée avec chaque
  trame mouvement. Sondage accéléromètre toutes les 1 s, gyroscope lu
  uniquement en rafale au moment d'un événement (jamais en continu). Voir
  § 3.3 pour le détail.
- **Unités déployées avec cette architecture** : #01 sur l'image d'or
  actuelle ; #02 et unit04-13 sur des images antérieures **sans le
  correctif de tempête USB**, à reflasher avec l'image actuelle. #03
  tourne toujours l'ancienne architecture, aucun flash prévu. Tableau
  complet § 7. Intégrées dans Home Assistant (découverte BTHome).

**Objectif final non atteint à ce jour** : 5-6 µA (référence : projet
frère XIAO nRF52840 Sense, ~10 µA avec détection de mouvement complète).
Progression significative le 2026-09-22/23 : 24,47 → 16-17 µA (−30 %,
plan `archive/docs-historique/Plan-Reduction-Consommation-2026-09-22.md`), dans la fourchette
cible du plan (13-15 µA) sans toutefois l'atteindre pleinement. Le
poste dominant restant (rail `imu_vdd`/LDO1, ~250-300 µA statique tant
qu'actif) reste documenté et fait l'objet d'une question ouverte
auprès du support Nordic (§ 8) ; la tentative de le couper par
commande GPIO (étape C du plan) a échoué (régression, voir Journal).

---

## 2. Architecture — principe

- `main()` est une boucle infinie unique : pas de `sys_poweroff()`, pas
  de redémarrage périodique.
- Tout premier appel de `main()` : `raise_vbus_current_limit()` initialise
  le chargeur et relève la limite d'entrée VBUS du nPM1300 à 500 mA
  (100 mA par défaut à chaque branchement, faute de batterie) **avant**
  tout allumage de LDO1 — sinon l'appel de courant de LDO1 fait
  redémarrer la carte en boucle (tempête USB, voir
  `Procédure-Test-Connexion-USB-Flash-XIAO-nRF54LM20A.md`).
- Bluetooth (`bt_enable()`) initialisé **une seule fois**, au vrai
  démarrage.
- L'IMU (`imu_vdd`/LDO1 + LSM6DS3TR-C) reste allumée en continu ~98 % du
  temps **coupée** — allumée brièvement (~15-20 ms) à chaque cycle d'1 s
  pour lire un échantillon, puis éteinte. Ce rail coûte
  ~250-300 µA tant qu'il est actif, indépendamment de la charge (cause
  non résolue, voir § 8) — d'où la nécessité de le couper entre chaque
  lecture plutôt que de le laisser actif pour un réveil par interruption
  matérielle.
- RAM inutilisée (au-delà de l'image liée, ~23,7 Ko sur 507 Ko retenus)
  coupée via la bibliothèque officielle NCS `RAM_POWER_DOWN_LIBRARY`.
- Trame santé envoyée seulement quand due (15 min) ; trame
  mouvement/orientation envoyée seulement sur événement réel (delta
  d'accélération, franchissement d'angle) ou heartbeat (60 min) — jamais
  à chaque cycle de sondage.
- Gyroscope activé uniquement en rafale (ODR 12,5 Hz, ~200 ms de marge de
  démarrage) au moment précis d'envoyer un événement, dans la même
  fenêtre `imu_vdd` que l'accéléromètre — jamais laissé actif en continu
  (coûterait ~0,9 mA contre ~9 µA pour l'accéléromètre seul).

### Détail technique important : ré-initialisation IMU par cycle

Parce que le SoC ne redémarre plus, `device_init()` sur le driver
LSM6DSL ne réexécute son init bas niveau qu'**une seule fois** (Zephyr,
`kernel/device.c`) — alors que la puce physique perd son état à chaque
coupure de `imu_vdd`. `sample_motion()` (`main.c`) réécrit donc
explicitement par I2C, à chaque cycle, les deux registres que l'init ne
configure qu'une fois : `CTRL3_C` (BDU + auto-incrément d'adresse) et
`CTRL6_C` (mode bas-consommation). Sans ça, les lectures X/Y/Z
deviendraient incohérentes en silence à partir du 2ᵉ cycle.

---

## 3. Fichiers exacts (état actuel, vérifiés par flash + PPK2)

### 3.1 `xiao_door_sensor/prj.conf`

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

CONFIG_PM=y
CONFIG_PM_DEVICE=y
CONFIG_PM_DEVICE_RUNTIME=y
CONFIG_HWINFO=y

CONFIG_RAM_POWER_DOWN_LIBRARY=y

CONFIG_BT=y
CONFIG_BT_BROADCASTER=y
CONFIG_BT_DEVICE_NAME="XIAO-DOOR"

CONFIG_RETAINED_MEM=y
CONFIG_CRC=y

CONFIG_SENSOR=y
CONFIG_NPM13XX_CHARGER=y
CONFIG_MFD=y

CONFIG_LSM6DSL=y
CONFIG_LSM6DSL_ENABLE_TEMP=y

CONFIG_REGULATOR=y

CONFIG_MAIN_STACK_SIZE=4096
CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE=2048

CONFIG_REBOOT=y
```

**Règle absolue, ne jamais l'oublier** : `CONFIG_SERIAL=n` en dur, jamais
suspendu à l'exécution. Un bug driver UARTE documenté (fuite de
référence PM runtime) fait échouer la suspension à l'exécution dès
qu'un autre périphérique (BLE, régulateur+IMU) tourne en même temps —
le serial reste actif en idle, ~260-470 µA au lieu de quelques µA.

**`CONFIG_LSM6DSL_ENABLE_TEMP=y` requis (ajouté le 2026-08-29)** : sans
cette ligne, tout le code de lecture `SENSOR_CHAN_DIE_TEMP` du driver
LSM6DSL est compilé hors du binaire (`#if defined(CONFIG_LSM6DSL_ENABLE_TEMP)`
dans `lsm6dsl.c`) — la température de la trame B resterait silencieusement
figée à 0 sans erreur de build. `CONFIG_I2C` et `CONFIG_LSM6DSL_TRIGGER_*`
n'ont pas besoin de ligne explicite : auto-sélectionnés par le Kconfig du
driver capteur (`CONFIG_LSM6DSL_TRIGGER_NONE=y` de fait — aucun thread de
trigger, la détection de mouvement reste un sondage logiciel).

### 3.2 `xiao_door_sensor/boards/xiao_nrf54lm20a_nrf54lm20a_cpuapp.overlay`

```dts
&power_en {
	/delete-property/ regulator-boot-on;
};

#include <zephyr/dt-bindings/regulator/npm13xx.h>

&pmic {
	regulators {
		imu_vdd: LDO1 {
			regulator-min-microvolt = <3300000>;
			regulator-max-microvolt = <3300000>;
		};
	};
};

/* Deferred-init : SYS_INIT tournerait avant que main() alimente imu_vdd. */
&lsm6ds3tr_c {
	zephyr,deferred-init;
};

/* Deferred-init : le chargeur est initialise explicitement en tout debut
 * de main() par raise_vbus_current_limit(), avant LDO1. */
&pmic_charger {
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

**Pistes tentées puis retirées sur cet overlay — ne pas réintroduire
sans revoir `archive/docs-historique/` d'abord** : la reconfiguration
UICR `nfct-pins-as-gpios` (broches NFC jamais utilisées sur ce board) a
été testée en même temps qu'un pull-down GPIO côté `main.c` et a produit
un pic de courant ~200 mA inexpliqué — retirée par précaution, jamais
isolée proprement depuis.

### 3.3 `xiao_door_sensor/src/main.c` — structure

- `sample_motion()` : active `imu_vdd`, réécrit `CTRL3_C`/`CTRL6_C` par
  I2C direct, ODR accéléromètre = **208 Hz**, attente de réglage 6 ms
  (période réelle ODR ~4,8 ms), lit X/Y/Z (via `sensor_value_to_float()`,
  pas de troncature entière), calcule pitch/roll et détection de
  mouvement/hystérésis d'angle, lit la température die si une trame santé
  est due, lit le gyroscope en rafale (et intègre le yaw) **seulement**
  si un événement va effectivement être envoyé — tout cela avant de
  désactiver `imu_vdd`, jamais après (l'advertising BLE qui suit peut
  bloquer jusqu'à ~700 ms–10 s, hors de question de garder le rail actif
  pendant ce temps).
- Boucle principale (`main()`, après l'init unique) : calcule
  `health_due`/`heartbeat_due` (échéances GRTC) → `sample_motion()` →
  envoie trame A + trame C si événement détecté et non plafonné par
  l'anti-rafale → machine à états mouvement/repos (trame repos répétée
  3× si retour au calme) → trame B si santé due → `retained_save()` →
  `k_sleep(K_MSEC(1000))`. Logique événementielle (seuils, anti-rafale,
  hystérésis, repos) reprise à l'identique de l'ancien firmware de
  référence (`archive/xiao_door_sensor-logs-et-backups/reference/
  main_full_2026-08-27.c.bak`), simplement remise à plat dans la boucle
  unique (plus de fenêtre active bornée séparée : la boucle tourne déjà
  en continu).
- `MOTION_POLL_INTERVAL_MS` = **1000** (aligné sur la réactivité ~1 s de
  la référence nRF52840 Sense — un test à 1500 ms a mesuré 15,87 µA
  contre 21,52 µA à 1000 ms, gain refusé au profit de la réactivité).
- Aucun `sys_poweroff()` / `z_nrf_grtc_wakeup_prepare()` nulle part. Aucun
  réveil GPIO/interruption matérielle — la boucle sonde déjà en logiciel.
- **Bouton** (`sw0`/`button0`) lu à chaque trame A, y compris son bug
  connu non résolu (toujours 0 en test réel) — repris tel quel de la
  production, pas corrigé dans ce portage.
- **Tamper (chute) et vibration (double-tap)** : toujours envoyés à 0,
  comme en production — le driver LSM6DSL n'expose pas ces événements
  matériels via l'API `sensor_trigger` standard.

#### Anomalie de consommation sur #02 et changement de méthode de déploiement (2026-08-30)

**Constat non résolu par simple relecture/correction de code — statut
factuel, pas une conclusion fermée** : le build initial du portage
trames A/C (2026-08-29) mesurait ~80 µA de moyenne sur l'unité #02
(contre ~20-22 µA attendu). Une première hypothèse — délai de
stabilisation accéléromètre insuffisant avant lecture (`sample_motion()`
n'attendait que 11 ms contre un Ton datasheet de 35 ms typique, ST
DocID030071 Rev 3 Table 4 p.24 — déjà correctement appliqué au gyroscope
via `GYRO_STARTUP_MS` mais oublié pour l'accéléromètre, lu lui à chaque
cycle) — a été corrigée dans le code (délai porté à 40 ms) puis reflashée
sur #02, mais la consommation mesurée est alors montée à ~200+ µA, donc
**cette hypothèse ne suffit pas à expliquer/corriger l'anomalie** et n'a
pas été validée par la mesure. Pendant ce temps, **l'unité #01 (même
génération de firmware A/B/C) est restée mesurée à ~20 µA**, sans jamais
présenter cette anomalie.

**Décision prise suite à cet échec de correction par rebuild successifs**
: cesser de rebuilder/deviner depuis les sources pour corriger #02, et
à la place **cloner octet pour octet la mémoire flash de #01** (l'image
qui fonctionne, vérifiée physiquement) directement sur #02 — voir
`Procedure-Clonage-XIAO-nRF54LM20A.md` pour la procédure complète. Ceci
fait, `verify_image` a confirmé 117396 octets identiques entre #01 et
#02 (2026-08-30). Ceci élimine le risque qu'un rebuild introduise un
nouveau bug non détecté avant flash réel, quelle que soit la qualité
apparente du raisonnement de code.

**Résolu (2026-08-30, confirmation finale)** : après une session de
diagnostic approfondie (registres PMIC, trace instrumentée SWD sur #01
et #02, cadence de boucle, erreurs I2C — tout revenu conforme des deux
côtés, voir historique complet dans `Procedure-Clonage-XIAO-nRF54LM20A.md`),
un firmware de diagnostic laissé par erreur sur #02 après un test
ponctuel s'est avéré être la cause des mesures élevées (~45-70 µA)
observées entre-temps — pas un défaut du clonage. Une fois #02
reclonée proprement depuis l'image d'or standard (sans instrumentation),
**la mesure PPK2 confirme une consommation moyenne identique à #01**.
La méthode de clonage elle-même n'a jamais été fautive. Voir §4 du
présent document, « Règle absolue : ne jamais laisser un firmware de
diagnostic flashé », pour la règle de process qui en découle.

| # | Statut (2026-08-30) |
|---|---|
| 01 | **Image d'or de référence** — jamais rebuildée depuis, ~20 µA confirmé |
| 02 | **Clonée depuis l'image d'or de #01** (`verify_image` : 117396 octets identiques) — **consommation PPK2 confirmée identique à #01** |
| 03 | Non concernée — ancienne architecture, code différent |

#### État retenu et correctif GRTC (2026-08-29)

`struct retained_state` porte désormais aussi `last_sent_pitch_dd`,
`last_sent_roll_dd`, `yaw_dd` (intégration gyroscopique cumulée, aucun
recalage anti-dérive) et `next_heartbeat_us`, en plus de `bthome_pid` et
`next_health_us`. Cette RAM retenue est de la **SRAM ordinaire, pas de la
RRAM** : elle peut survivre à un reset/reflash (CRC valide) alors que le
compteur GRTC, lui, repart de zéro — une échéance absolue chargée d'une
session précédente pourrait alors devenir inatteignable pendant une durée
indéterminée (symptôme observé le 2026-08-29 sur l'unité #01 : aucune
trame santé 18 min après un reflash). Au boot, les deux échéances
(`next_health_us`, `next_heartbeat_us`) sont désormais bornées à au plus
un intervalle complet après CE boot, jamais plus loin.

Code source complet dans `xiao_door_sensor/src/main.c` (ce fichier ne
duplique que la structure, pas le code entier).

---

## 4. Déployer une unité

**Voir le document dédié `Procedure-Clonage-XIAO-nRF54LM20A.md`** —
procédure complète (dump, conversion, flash, vérification), image d'or
actuelle, historique des clonages, et notes de connexion SWD. Depuis le
2026-08-30, une unité supplémentaire se déploie **par clonage d'une
unité déjà vérifiée en fonctionnement réel**, jamais par rebuild depuis
les sources (un rebuild a déjà introduit un bug de consommation réel non
détecté avant flash — voir ce même document dédié pour le détail).

### Règle absolue : ne jamais laisser un firmware de diagnostic flashé

**Incident du 2026-08-30** : lors du diagnostic de l'écart de
consommation #01/#02, un firmware de diagnostic (lecture registres
nPM1300 par I2C, `CONFIG_SERIAL=y`/logging UART actif en continu,
aucune optimisation d'énergie) a été flashé sur #02 pour une lecture
ponctuelle, puis **jamais reflashé avec l'image de référence ensuite**.
Une mesure PPK2 faite plus tard sur cette même unité a montré >300 µA —
non pas une nouvelle anomalie du clone, mais simplement la conséquence
attendue d'un firmware de debug laissé en place par oubli. Un deuxième
firmware de test (trace de diagnostic en RAM, lue par SWD) a également
dû être suivi puis retiré du code source pour ne pas laisser le dépôt
dans un état qui ne correspond plus à l'image réellement vérifiée.

**Conséquence directe, à respecter systématiquement** :
1. Tout firmware de diagnostic/test flashé sur une unité (lecture de
   registres, trace instrumentée, etc.) est **temporaire par
   construction** — la reflasher avec l'image de référence
   immédiatement après avoir récupéré les données nécessaires, avant
   toute mesure PPK2 ou toute remise en service, sans attendre une
   demande explicite.
2. Ne jamais laisser une mesure de consommation en cours ou prévue sans
   avoir vérifié au préalable, explicitement, quel firmware est
   réellement flashé sur l'unité testée à cet instant.
3. Le code source (`main.c`) ne doit contenir aucune instrumentation de
   diagnostic laissée en place après un test — la retirer dans la
   foulée si elle ne fait pas partie de l'image vérifiée, pour que le
   dépôt reflète toujours fidèlement ce qui est réellement déployé.

---

## 5. Régénérer l'image d'or depuis les sources (seulement après une vraie modification de code)

**Ne sert plus à déployer une unité supplémentaire (§ 4 ci-dessus) — sert
uniquement à produire une nouvelle version quand le firmware doit
réellement changer.** Toute nouvelle image issue d'un rebuild doit être
physiquement vérifiée en fonctionnement réel (mesure PPK2 conforme aux
attentes) **avant** de remplacer l'image d'or et d'être clonée sur
d'autres unités — ne jamais sauter cette vérification, quelle que soit la
confiance dans le changement de code.

```bash
export TCROOT="/c/ncs/toolchains/dcbdc366a1"
export PATH="$TCROOT/mingw64/bin:$TCROOT/bin:$TCROOT/opt/bin:$TCROOT/opt/bin/Scripts:$TCROOT/nrfutil/bin:$TCROOT/opt/zephyr-sdk/gnu/arm-zephyr-eabi/bin:$PATH"
export PYTHONPATH="C:/ncs/toolchains/dcbdc366a1/opt/bin;C:/ncs/toolchains/dcbdc366a1/opt/bin/Lib;C:/ncs/toolchains/dcbdc366a1/opt/bin/Lib/site-packages"
export NRFUTIL_HOME="C:/ncs/toolchains/dcbdc366a1/nrfutil/home"
export ZEPHYR_TOOLCHAIN_VARIANT="zephyr"
export ZEPHYR_SDK_INSTALL_DIR="C:/ncs/toolchains/dcbdc366a1/opt/zephyr-sdk"
export ZEPHYR_BASE="C:/ncs/v3.4.0/zephyr"

cd "/c/ncs/projects/nRF54LM20A/xiao_door_sensor"
west build -b xiao_nrf54lm20a/nrf54lm20a/cpuapp -d build --pristine \
  -- -DBOARD_ROOT="C:/ncs/vendor/platform-seeedboards/zephyr"
```

**`openocd` s'exécute TOUJOURS via l'outil PowerShell, jamais Bash/
Git-Bash, sur cette machine — confirmé le 2026-09-22 après ~20
répétitions du même diagnostic (voir `C:\ncs\CLAUDE.md`, règle « PowerShell
pour tout outil USB/série/HID »). Sous Bash, la lecture du numéro de
série échoue de façon quasi systématique (`unable to find a matching
CMSIS-DAP device`, ou erreurs `WriteFile` en cours de transaction) ; la
commande strictement identique réussit sous PowerShell. Les blocs
ci-dessous sont donc en PowerShell — ne pas les convertir en Bash.**

Lecture seule (numéro de série + cible détectée, sans flasher) — à
lancer avant TOUT flash, comparer le `Serial#` affiché au tableau § 7 :

```powershell
$env:Path = "C:\ncs\tools\xpack-openocd-0.12.0-7\bin;" + $env:Path
$BOARD_DIR = "C:/ncs/vendor/platform-seeedboards/zephyr/boards/arm/xiao_nrf54lm20a"
openocd -s "$BOARD_DIR/support" -f "$BOARD_DIR/support/openocd.cfg" `
  -c "cmsis-dap vid_pid 0x2886 0x0068" -c "cmsis-dap backend hid" -c "adapter speed 500" `
  -c "init" -c "exit"
```

Flash + vérification, unité ciblée explicitement par `adapter serial`
(remplacer `<SERIAL>` par le S/N confirmé à l'étape précédente,
`<HEX>` par le chemin du `.hex`) :

```powershell
$env:Path = "C:\ncs\tools\xpack-openocd-0.12.0-7\bin;" + $env:Path
$BOARD_DIR = "C:/ncs/vendor/platform-seeedboards/zephyr/boards/arm/xiao_nrf54lm20a"
$HEX = "<HEX>"
openocd -s "$BOARD_DIR/support" -f "$BOARD_DIR/support/openocd.cfg" `
  -c "cmsis-dap vid_pid 0x2886 0x0068" -c "cmsis-dap backend hid" -c "adapter serial <SERIAL>" -c "adapter speed 500" `
  -c "init" -c "reset halt" `
  -c "nrf54lm20a-load `"$HEX`"" `
  -c "reset halt" `
  -c "verify_image `"$HEX`"" `
  -c "reset" -c "exit"
```

Ne pas rediriger stderr (`2>&1`) sur ces commandes depuis PowerShell —
ça enveloppe chaque ligne dans un `NativeCommandError` qui rend le code
de sortie peu fiable même en cas de succès réel ; PowerShell capture déjà
stderr sans ça.

**`-c "cmsis-dap backend hid"` fait partie intégrante de la commande
standard depuis le 2026-08-29 — ne pas l'omettre.** Sans cette ligne, le
pont SAMD11 utilise par défaut le backend WinUSB v2, qui peut s'énumérer
correctement (visible dans Windows, serial lu par OpenOCD) tout en
échouant sur **chaque** transaction réelle (`error submitting USB
read/write: Entity not found`, `could not claim interface: Operation not
supported`) — constaté sur l'unité #02, persistant après redémarrage des
process `nrfutil`, reset PnP, cycle d'alimentation complet et changement
de vitesse d'horloge ; seul le passage au backend HID (v1) a résolu le
problème. Si ce symptôme précis réapparaît malgré cette ligne déjà
présente, ce n'est pas la même cause — ne pas re-diagnostiquer depuis
zéro, relire d'abord ce paragraphe.

Toujours vérifier avec `verify_image` (jamais `dump_image`+`cmp`, faux
positifs sur les trous RRAM).

Identifier la carte branchée :

```powershell
Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -like "*VID_2886*" } | Select-Object FriendlyName, InstanceId, Status
```
→ ligne `Périphérique USB composite` = numéro de série du pont USB↔SWD,
fixe par carte (comparer au tableau § 7).

---

## 6. Écart fonctionnel connu

**Résolu le 2026-08-29** : les trames A (mouvement/orientation/bouton) et
C (IMU brut) sont désormais portées sur cette architecture (§ 3.3),
parité fonctionnelle atteinte avec l'ancien firmware de production (#03).
Écarts volontaires, identiques à la production : tamper et vibration
toujours à 0 (non implémentés côté driver), bouton physique toujours à 0
(bug connu non résolu).

**Reste à faire** : confirmer par PPK2 que #02, clonée depuis l'image
d'or de #01 le 2026-08-30 (`Procedure-Clonage-XIAO-nRF54LM20A.md`),
revient bien à ~20 µA — le rebuild du 2026-08-29/30 depuis les sources
avait introduit un bug réel (delta accéléromètre bruité, ~80-200+ µA
mesuré sur #02 selon la version) qui n'a jamais été observé sur #01 ;
tant que la cause exacte de cet écart entre les deux unités n'est pas
comprise avec certitude, l'image d'or physiquement vérifiée sur #01
(~20 µA) reste la seule source de confiance pour déployer #02 et les
unités suivantes. Tests fonctionnels HA complets (mouvement, angle,
bouton, IMU brut) à reprendre sur #01 et #02 au-delà de la simple
présence des entités.

---

## 7. Déploiement actuel

**Vérifier le numéro de série du pont SWD avant tout flash** (voir règle
absolue dans `C:\ncs\CLAUDE.md`) — plusieurs unités peuvent être
branchées simultanément, `vid_pid` seul ne les distingue pas.

Image actuelle = `golden-image/unit01-verified-2026-09-23-ABD-VBUSFIX-15uA`.
Les unités encore sur une image antérieure n'ont pas le correctif de
tempête USB : à reflasher avec `deploy-scripts/Flash-XiaoUnit.ps1`
(procédure : `Procédure-Test-Connexion-USB-Flash-XIAO-nRF54LM20A.md`).

| # | S/N pont SWD | Adresse BLE | Image | Statut |
|---|---|---|---|---|
| 01 | `C5F0E209` | `D2:3A:F7:B1:E8:18` | actuelle | 15,65 µA au PPK2 ; 15 branchements sans tempête |
| 02 | `9C4A557D` | `DE:F6:A3:A9:0F:0F` | actuelle | 3 branchements sans tempête |
| 03 | `4587B5C1` | `E6:C9:11:CE:6E:C6` | ancienne architecture (System OFF + réveil IMU) | aucun flash prévu |
| 04 | `D8E37DD8` | `E5:0A:38:B7:12:FF` ¹ | actuelle | 4 branchements sans tempête |
| 05 | `89757F76` | `D2:50:B2:FD:BF:66` ¹ | actuelle | 3 branchements sans tempête |
| 06 | `4EDD8A75` | `D3:7F:04:96:41:F9` ¹ | actuelle | 4 branchements sans tempête |
| 07 | `BF948013` | `D9:6A:B3:EC:F7:DF` ¹ | actuelle | 3 branchements sans tempête |
| 08 | `59775734` | `FF:02:34:82:42:FB` ¹ | actuelle | 3 branchements sans tempête |
| 09 | `DB541E5C` | `CA:EE:33:34:49:9C` ¹ | actuelle | 3 branchements sans tempête |
| 10 | `5662D48F` | `C8:95:C4:33:A6:F1` ¹ | actuelle | 3 branchements sans tempête |
| 11 | `0E1FA1DD` | `CC:FD:4E:FD:FB:CA` ¹ | actuelle | 3 branchements sans tempête |
| 12 | `1B073FBF` | `C7:D7:93:C1:EB:FF` ¹ | actuelle | 3 branchements sans tempête |
| 13 | `C655C476` | `F8:EC:81:E8:3F:9F` ¹ | actuelle | 3 branchements sans tempête |
| ? | — (connecteur USB-C cassé) | à relever | à identifier | **reste à faire** : flash par J-Link externe, identité par la MAC BLE (`Procédure-Test-Connexion-USB-Flash-XIAO-nRF54LM20A.md` § « Unité sans USB ») |

**Adresse BLE** : fixe par puce — le firmware la dérive de
`FICR.INFO.DEVICEID` (`set_fixed_ble_identity()`, `main.c`).
`Flash-XiaoUnit.ps1` lit ce registre (lecture seule) et affiche l'adresse
avant chaque flash.
¹ Calculée depuis le registre FICR ; à confirmer une fois dans Home
Assistant (valide alors la méthode pour toutes les unités).
² Relevée automatiquement au reflash.

~7 unités supplémentaires attendues : à flasher avec
`Flash-XiaoUnit.ps1`, jamais par rebuild individuel.

---

## 8. Point bloquant principal — question ouverte auprès de Nordic

Le rail `imu_vdd`/LDO1 (nPM1300, LOADSW1/LDO1) consomme ~250-300 µA dès
qu'il est activé, **quelle que soit la charge** (même sans IMU, sans
trafic I2C) — confirmé par 13+ tests d'isolation indépendants et
reproduit sur le code de référence Seeed lui-même. C'est le poste
dominant (~70-80 % du coût par cycle). Cause non identifiée à ce jour,
question posée au support Nordic — voir
`Nordic-Support-Report-XIAO-nRF54LM20A.md` pour le détail complet
(errata vérifiées, tests d'isolation, trace de mesure fournie).

---

## 9. Étude alimentation par pile non rechargeable

**Correction (2026-09-22)** : la conclusion « désactiver la charge
augmente la consommation » ci-dessous n'est **pas établie**. Le firmware
`archive/projets-test/xiao_no_charge_test/` utilisé pour ce test contient `k_msleep(40)` dans
`sample_motion()` (vérifié : `archive/projets-test/xiao_no_charge_test/src/main.c:952`), le
même bug de délai accéléromètre identifié et corrigé en
`archive/docs-historique/Plan-Reduction-Consommation-2026-09-22.md` §1 (+34 ms de rail par cycle
≈ +11 µA). Les ~32 µA mesurés s'expliquent donc en grande partie par ce
délai, pas par la désactivation de la charge elle-même. **À re-tester
sur une base saine (k_msleep(6)) si l'option pile non rechargeable
revient à l'ordre du jour** — ce test n'a pas encore été refait.

**Contexte** : étude de la possibilité d'alimenter le XIAO par une pile
non rechargeable (au lieu du LiPo actuel), ce qui impose de désactiver
la fonctionnalité de charge du nPM1300 (charger une pile non
rechargeable est à proscrire).

**Test effectué (2026-08-30, exclusif #02, jamais sur #01)** : firmware
de test dans un dossier séparé (`archive/projets-test/xiao_no_charge_test/`), overlay
identique à `xiao_door_sensor` avec un seul changement :
`/delete-property/ charging-enable;` sur `&pmic_charger`. Mécanisme
exact (vérifié dans le driver,
`zephyr/drivers/sensor/nordic/npm13xx_charger/npm13xx_charger.c:668-674`) :
sans cette propriété, le driver n'écrit jamais le bit
`CHGR_BASE`/`CHGR_OFFSET_EN_SET` qui active la machine à états de charge
du nPM1300 — la lecture tension/courant batterie (jauge, trame B)
n'est pas affectée, seule la charge active est coupée.

**Résultat mesuré (PPK2)** : **~32 µA de moyenne — plus élevé que les
~20-22 µA confirmés avec la charge activée (image d'or standard).**
Désactiver la charge n'a donc **pas** réduit la consommation dans ce
test ; au contraire, elle est remontée à un niveau proche des mesures
dégradées observées avant la confirmation propre de #02 (§7).

**Observation corrélée** : LED0 (nPM1300, mode `error` — indicateur
autonome piloté directement par le PMIC, pas par le firmware, voir
`nordic,npm1300-led.yaml:8` : « chaque LED peut afficher automatiquement
le statut erreur ou charge ») trouvée éteinte pendant ce test, alors
qu'elle est normalement allumée au repos sur ce projet (observé sur #01
et sur #02 en fonctionnement normal). Cohérent avec un changement d'état
du même bloc chargeur que celui modifié par le test — pas confirmé par
lecture directe de registre à ce stade, à vérifier si le sujet est
repris.

**Conclusion à ce stade** : ne pas désactiver `charging-enable` comme
méthode de réduction de consommation — l'hypothèse est infirmée par la
mesure. Si l'alimentation par pile non rechargeable est retenue, la
désactivation de la charge resterait nécessaire pour la sécurité
(éviter de tenter de charger une pile non rechargeable), mais il
faudra accepter/expliquer le surcoût de consommation mesuré, ou
investiguer plus avant pourquoi la désactivation l'augmente au lieu de
la réduire.

#02 reflashée avec l'image d'or immédiatement après cette mesure —
firmware de test jamais laissé en place (voir §4, règle absolue).

---

## 10. Historique complet

Tout le raisonnement, les tests intermédiaires, les hypothèses écartées
et les incidents (dont le pic ~200 mA de l'audit broches) sont
conservés dans `archive/docs-historique/` — notamment
`XIAO-nRF54LM20A-Solution-System-OFF.md` (tests #1 à #39) et
`Transition-nRF54LM20A-Optimisation-Consommation.md`. Ce document-ci
(§ 1-9) est la seule référence nécessaire pour reprendre le travail sans
avoir à rouvrir l'historique, sauf besoin spécifique de retrouver le
raisonnement détaillé derrière une décision.

---

## 11. Bug de reconstruction non reproductible — RÉSOLU (2026-09-22)

**Résolu, ticket Nordic à clore.** La prémisse de toute cette section
était fausse : il n'y a jamais eu de non-déterminisme du compilateur.
Preuve (`archive/docs-historique/Plan-Reduction-Consommation-2026-09-22.md` §1, reproduite
indépendamment le 2026-09-22) : un rebuild `--pristine` du commit
`c63908d` + H_LACTIVE (`xiao_door_sensor/src/main.c` réaligné, voir
commit `630b32a`) redonne `zephyr.bin` octet pour octet identique à
`archive/golden-image-anciennes/unit02-verified-2026-09-01-H_LACTIVE.bin` (SHA-256
`0da087ae45d087afdc334828e95a8c44283b1182b1cce5dc1525d7133853f778`).
`west build` (NCS 3.4.0, toolchain `dcbdc366a1`) **est déterministe.**

La vraie cause des rebuilds « ~33 µA » : le commit `ccbe1b5` (2026-08-29
18:55, **après** le flash de l'image d'or 18:01) a changé
`k_msleep(6)` → `k_msleep(40)` dans `sample_motion()` (délai de
stabilisation accéléromètre) — présenté à l'époque comme une correction
nécessaire (voir commentaire encore présent dans l'historique git),
alors qu'il n'a jamais été appliqué à l'image d'or elle-même. +34 ms de
rail `imu_vdd` actif par cycle ≈ +11 µA, ce qui explique intégralement
l'écart ~22 → ~33 µA. La divergence de désassemblage observée sur
`angle_crossed` (ci-dessous) est réelle mais n'était pas la cause
principale : c'est un effet secondaire du même source modifié (logique
de confirmation sur 2 cycles ajoutée en `1994112`, absente de l'image
d'or), pas une preuve de non-déterminisme du compilateur sur du code
identique.

**Conséquence** : le plan de résilience (`Audit-Resilience-2026-09-19.md`)
n'était pas en cause non plus — sa « régression » venait de la même base
à 40 ms. À ré-appliquer séparément sur la base réalignée, avec sa propre
mesure PPK2. Voir `xiao_nrf54lm20a_project_notes.md` item 9 et
`archive/docs-historique/Suivi-Nordic-Reproductibilite-Build-2026-09-19.md` (mis à jour) pour le
suivi. Le paragraphe ci-dessous (décision du 2026-09-19) est conservé
pour l'historique mais **superseded**.

---

**Décision (2026-09-19, historique, superseded ci-dessus) : #02 reste sur l'image d'or vérifiée (`golden-image/unit01-
verified-2026-08-30.bin`, ~23 µA), le plan de résilience
(`Audit-Resilience-2026-09-19.md`) n'est PAS déployé pour l'instant.**

Tentative d'implémentation complète du plan de résilience sur #02 le
2026-09-19 : rebuild propre depuis les sources, mesure PPK2 à ~35-38 µA
au lieu des ~20-22 µA attendus. Investigation approfondie (isolation du
watchdog, de `suspend_external_flash()`, comparaison ccache on/off,
vérification de la dérive toolchain/SDK/board-files, `NCS_TOOLCHAIN_
VERSION`, `ZEPHYR_TOOLCHAIN_VARIANT`, Bash vs PowerShell, coupure
d'alimentation complète du PMIC) — **tout innocenté**. Un rebuild du
commit d'origine SANS aucun changement de résilience reproduit la même
anomalie (~30-33 µA), et reflasher l'image d'or archivée sur #02, le
même jour, avec le même PPK2, redonne fiablement ~23 µA — ce qui
innocente définitivement l'unité #02 et la méthode de mesure.

Localisation précise par désassemblage + `addr2line` (infos DWARF) :
le code compilé diverge du binaire d'or à l'intérieur de `main()`
(avec `sample_motion()` inlinée), sur la ligne calculant `angle_
crossed` (comparaison `abs()` sur deux `int16_t` — voir `main.c`,
calcul de `angle_crossed`). Pour une source strictement identique, le
compilateur (`arm-zephyr-eabi-gcc` 14.3.0, `-Os`) génère une séquence
compacte (`ite`/mouvement conditionnel) dans un cas et une séquence
branchue nettement plus longue dans l'autre — code exécuté à chaque
cycle de boucle (1×/seconde, en continu), cohérent avec l'ampleur de
l'écart mesuré. Une réécriture de contrôle (variables intermédiaires
explicites) n'a pas reproduit la forme compacte — a produit un
résultat encore plus gros (117444 octets) — confirmant que ce n'est
pas maîtrisable de façon fiable depuis le code source.

**Conclusion** : tout ce qui est sous notre contrôle (source, Kconfig,
environnement, toolchain, matériel) est prouvé identique ; seul le
code réellement compilé diffère. Ticket ouvert auprès du support
Nordic avec le détail complet (voir mail archivé si besoin). **Ne pas
retenter un rebuild pour production tant qu'une réponse n'a pas
clarifié la reproductibilité de `west build` sur ce toolchain
(`dcbdc366a1`, NCS 3.4.0).**
