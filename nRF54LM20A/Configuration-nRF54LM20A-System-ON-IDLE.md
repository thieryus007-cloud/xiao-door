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
- **Consommation mesurée** : **~20-22 µA** en moyenne au repos (unité
  #01, PPK2, fenêtre 60 s — référence vérifiée) — contre 70-144 µA pour
  l'ancienne architecture (System OFF + redémarrage complet par cycle).
  #02 a été corrigée le 2026-08-30 par clonage direct de l'image d'or de
  #01 (voir `Procédure-Clonage-XIAO-nRF54LM20A.md`), suite à une anomalie
  (~80-200+ µA) causée par deux rebuilds successifs — consommation à
  reconfirmer par PPK2.
- **Fonctionnalités actives (parité de production atteinte le 2026-08-29)** :
  trame BTHome v2 santé (batterie %, tension, température die) toutes les
  15 min ; trame mouvement/orientation (pitch/roll/yaw, activité,
  bouton, tamper=0, vibration=0) sur événement + heartbeat 60 min ; trame
  IMU brut (magnitudes + accélération signée par axe) envoyée avec chaque
  trame mouvement. Sondage accéléromètre toutes les 1 s, gyroscope lu
  uniquement en rafale au moment d'un événement (jamais en continu). Voir
  § 3.3 pour le détail.
- **Unités déployées avec cette architecture** : #01 (firmware complet
  A/B/C, référence/image d'or vérifiée) et #02 (même firmware visé, en
  cours de reclonage depuis l'image d'or de #01 suite à l'anomalie de
  consommation). Intégrées dans Home Assistant (découverte BTHome),
  tests fonctionnels HA complets à reprendre séparément. #03 tourne
  toujours l'ancienne architecture (déjà toutes les trames), aucun flash
  de la nouvelle architecture prévu pour l'instant — voir § 7.

**Objectif final non atteint à ce jour** : 5-6 µA (référence : projet
frère XIAO nRF52840 Sense, ~10 µA avec détection de mouvement complète).
Le principal poste restant est documenté et fait l'objet d'une question
ouverte auprès du support Nordic (§ 8).

---

## 2. Architecture — principe

- `main()` est une boucle infinie unique : pas de `sys_poweroff()`, pas
  de redémarrage périodique.
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

/* Deferred-init : le chargeur ecrit ~12-15 transactions I2C a chaque
 * boot, meme sans lecture batterie -- seulement quand une trame sante
 * est due (voir read_battery() dans main.c). */
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
`Procédure-Clonage-XIAO-nRF54LM20A.md` pour la procédure complète. Ceci
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

**Voir le document dédié `Procédure-Clonage-XIAO-nRF54LM20A.md`** —
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

```bash
export PATH="/c/ncs/tools/xpack-openocd-0.12.0-7/bin:$PATH"
BOARD_DIR="C:/ncs/vendor/platform-seeedboards/zephyr/boards/arm/xiao_nrf54lm20a"
HEX="C:/ncs/projects/nRF54LM20A/xiao_door_sensor/build/xiao_door_sensor/zephyr/zephyr.hex"

openocd -s "$BOARD_DIR/support" -f "$BOARD_DIR/support/openocd.cfg" \
  -c "cmsis-dap vid_pid 0x2886 0x0068" -c "cmsis-dap backend hid" -c "adapter speed 500" \
  -c "init" -c "reset halt" \
  -c "nrf54lm20a-load \"$HEX\"" \
  -c "reset halt" \
  -c "verify_image \"$HEX\"" \
  -c "reset" -c "exit"
```

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
d'or de #01 le 2026-08-30 (`Procédure-Clonage-XIAO-nRF54LM20A.md`),
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

## 7. Déploiement actuel (2026-08-30)

| # | Adresse BLE | Pont USB↔SWD | Architecture | Statut |
|---|---|---|---|---|
| 01 | `D2:3A:F7:B1:E8:18` | `C5F0E209` | **System ON IDLE, firmware complet A/B/C — image d'or de référence** | Intégrée dans HA ; **~20 µA moyenne confirmée au PPK2** ; ne jamais reflasher depuis un rebuild sans re-vérification physique |
| 02 | `DE:F6:A3:A9:0F:0F` | `9C4A557D` | **System ON IDLE, firmware complet A/B/C — clonée depuis l'image d'or de #01 le 2026-08-30** | Intégrée dans HA ; `verify_image` OK (117396 octets identiques à #01) ; consommation à reconfirmer par PPK2 |
| 03 | `E6:C9:11:CE:6E:C6` | `4587B5C1` | **Ancienne** (System OFF + réveil IMU par interruption) | Inchangée ; déjà toutes les trames ; aucun flash de la nouvelle architecture prévu pour l'instant |

Détail complet de l'ancienne architecture (#03) : voir
`archive/docs-historique/` (document `xiao_nrf54lm20a_project_notes`
archivé) si besoin de la reprendre en main.

**~17 unités supplémentaires attendues** — à déployer par clonage de
l'image d'or (`Procédure-Clonage-XIAO-nRF54LM20A.md`) une fois #02
confirmée elle aussi à ~20 µA, jamais par rebuild individuel.

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

## 9. Historique complet

Tout le raisonnement, les tests intermédiaires, les hypothèses écartées
et les incidents (dont le pic ~200 mA de l'audit broches) sont
conservés dans `archive/docs-historique/` — notamment
`XIAO-nRF54LM20A-Solution-System-OFF.md` (tests #1 à #39) et
`Transition-nRF54LM20A-Optimisation-Consommation.md`. Ce document-ci
(§ 1-8) est la seule référence nécessaire pour reprendre le travail sans
avoir à rouvrir l'historique, sauf besoin spécifique de retrouver le
raisonnement détaillé derrière une décision.
