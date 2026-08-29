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
  #01 et #02, PPK2, fenêtre 60 s) — contre 70-144 µA pour l'ancienne
  architecture (System OFF + redémarrage complet par cycle).
- **Fonctionnalités actives** : trame BTHome v2 santé (batterie %,
  tension) toutes les 15 min, sondage accéléromètre toutes les 1 s
  (aucune trame de mouvement envoyée pour l'instant — voir § 5, écart
  fonctionnel connu).
- **Unités déployées avec cette architecture** : #01 et #02, intégrées
  dans Home Assistant (découverte BTHome), tests fonctionnels HA complets
  à reprendre séparément. #03 tourne toujours l'ancienne architecture,
  aucun flash de la nouvelle prévu pour l'instant — voir § 6.

**Objectif final non atteint à ce jour** : 5-6 µA (référence : projet
frère XIAO nRF52840 Sense, ~10 µA avec détection de mouvement complète).
Le principal poste restant est documenté et fait l'objet d'une question
ouverte auprès du support Nordic (§ 7).

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
  non résolue, voir § 7) — d'où la nécessité de le couper entre chaque
  lecture plutôt que de le laisser actif pour un réveil par interruption
  matérielle.
- RAM inutilisée (au-delà de l'image liée, ~23,5 Ko sur 507 Ko retenus)
  coupée via la bibliothèque officielle NCS `RAM_POWER_DOWN_LIBRARY`.
- Trame santé BTHome envoyée seulement quand due (toutes les 15 min),
  jamais à chaque cycle.

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

CONFIG_REGULATOR=y

CONFIG_I2C=y
CONFIG_LSM6DSL=y
CONFIG_LSM6DSL_TRIGGER_GLOBAL_THREAD=y

CONFIG_MAIN_STACK_SIZE=4096
CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE=2048

CONFIG_REBOOT=y
```

**Règle absolue, ne jamais l'oublier** : `CONFIG_SERIAL=n` en dur, jamais
suspendu à l'exécution. Un bug driver UARTE documenté (fuite de
référence PM runtime) fait échouer la suspension à l'exécution dès
qu'un autre périphérique (BLE, régulateur+IMU) tourne en même temps —
le serial reste actif en idle, ~260-470 µA au lieu de quelques µA.

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
  I2C direct, ODR accéléromètre = **208 Hz**, attente de règlage
  6 ms (période réelle ODR ~4,8 ms), lit X/Y/Z en une seule transaction
  I2C groupée, désactive `imu_vdd`.
- `main()` : init une seule fois (LED déconnectées, broches SPI externe
  forcées à un niveau bas défini, identité BLE fixe dérivée du hardware
  ID, `bt_enable()`, `power_down_unused_ram()`), puis boucle infinie :
  `sample_motion()` → trame BTHome santé si échéance 15 min dépassée →
  `k_sleep(K_MSEC(1000))`.
- `MOTION_POLL_INTERVAL_MS` = **1000** (aligné sur la réactivité ~1 s de
  la référence nRF52840 Sense — un test à 1500 ms a mesuré 15,87 µA
  contre 21,52 µA à 1000 ms, gain refusé au profit de la réactivité).
- Aucun `sys_poweroff()` / `z_nrf_grtc_wakeup_prepare()` nulle part.

Code source complet dans `xiao_door_sensor/src/main.c` (ce fichier ne
duplique que la structure, pas le code entier).

---

## 4. Compiler, flasher, vérifier

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
  -c "cmsis-dap vid_pid 0x2886 0x0068" -c "adapter serial <numero-serie-pont>" -c "adapter speed 500" \
  -c "init" -c "reset halt" \
  -c "nrf54lm20a-load \"$HEX\"" \
  -c "reset halt" \
  -c "verify_image \"$HEX\"" \
  -c "reset" -c "exit"
```

Toujours vérifier avec `verify_image` (jamais `dump_image`+`cmp`, faux
positifs sur les trous RRAM). Identifier la carte branchée :

```powershell
Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -like "*VID_2886*" } | Select-Object FriendlyName, InstanceId, Status
```
→ ligne `Périphérique USB composite` = numéro de série du pont USB↔SWD,
fixe par carte (comparer au tableau § 6).

---

## 5. Écart fonctionnel connu

Cette architecture ne rejoue **pas encore** les fonctions de détection
de mouvement de l'ancienne (trames A/C : mouvement, chute/choc,
double-tap, bouton, pitch/roll/yaw) — seule la trame B (santé/batterie)
est implémentée. Le sondage accéléromètre (§ 2) lit un échantillon à
chaque cycle mais n'envoie aucune trame dessus pour l'instant. À faire
avant un déploiement en remplacement complet de l'ancienne architecture.

---

## 6. Déploiement actuel (2026-08-29)

| # | Adresse BLE | Pont USB↔SWD | Architecture | Statut |
|---|---|---|---|---|
| 01 | `D2:3A:F7:B1:E8:18` | `C5F0E209` | **System ON IDLE** (~20-22 µA mesuré) | Intégrée dans HA ; tests fonctionnels HA complets à reprendre |
| 02 | `DE:F6:A3:A9:0F:0F` | `9C4A557D` | **System ON IDLE** (~20-22 µA mesuré) | Intégrée dans HA ; tests fonctionnels HA complets à reprendre |
| 03 | `E6:C9:11:CE:6E:C6` | `4587B5C1` | **Ancienne** (System OFF + réveil IMU par interruption) | Inchangée ; aucun flash de la nouvelle architecture prévu pour l'instant |

Détail complet de l'ancienne architecture (#03) : voir
`archive/docs-historique/` (document `xiao_nrf54lm20a_project_notes`
archivé) si besoin de la reprendre en main.

**~17 unités supplémentaires attendues** — en attente que cette
architecture couvre aussi la détection de mouvement (§ 5) avant tout
flash de lot.

---

## 7. Point bloquant principal — question ouverte auprès de Nordic

Le rail `imu_vdd`/LDO1 (nPM1300, LOADSW1/LDO1) consomme ~250-300 µA dès
qu'il est activé, **quelle que soit la charge** (même sans IMU, sans
trafic I2C) — confirmé par 13+ tests d'isolation indépendants et
reproduit sur le code de référence Seeed lui-même. C'est le poste
dominant (~70-80 % du coût par cycle). Cause non identifiée à ce jour,
question posée au support Nordic — voir
`Nordic-Support-Report-XIAO-nRF54LM20A.md` pour le détail complet
(errata vérifiées, tests d'isolation, trace de mesure fournie).

---

## 8. Historique complet

Tout le raisonnement, les tests intermédiaires, les hypothèses écartées
et les incidents (dont le pic ~200 mA de l'audit broches) sont
conservés dans `archive/docs-historique/` — notamment
`XIAO-nRF54LM20A-Solution-System-OFF.md` (tests #1 à #39) et
`Transition-nRF54LM20A-Optimisation-Consommation.md`. Ce document-ci
(§ 1-7) est la seule référence nécessaire pour reprendre le travail sans
avoir à rouvrir l'historique, sauf besoin spécifique de retrouver le
raisonnement détaillé derrière une décision.
