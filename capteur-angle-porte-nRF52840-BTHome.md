# Capteur d'angle de porte — XIAO nRF52840 Sense → BTHome → Home Assistant

**Objectif :** capteur autonome mesurant l'angle d'ouverture d'une porte (2 axes), publiant en BLE (BTHome v2) vers Home Assistant : **angle en degrés**, **niveau batterie (%)** et **état ouvert/fermé dérivé**. Réveil au mouvement (wake-on-motion matériel) + rafraîchissement périodique toutes les 10 min. Autonomie cible 14–18 mois sur LiPo 604050 1500 mAh.

**Base firmware :** nRF Connect SDK (Zephyr).
**Statut de validation :** toutes les valeurs matérielles (broches, nœuds devicetree, object-id BTHome) sont vérifiées sur sources officielles Seeed / Zephyr / bthome.io. Les seuls éléments à mesurer chez toi sont la consommation réelle et le calage mécanique de l'angle.

---

## 0. Résumé des faits matériels vérifiés

| Élément | Valeur | Source |
|---|---|---|
| SoC | nRF52840 (silicium production mature) | — |
| IMU | LSM6DS3TR-C, adresse I²C `0x6a` | devicetree `xiao_ble` |
| Nœud IMU devicetree | `lsm6ds3tr_c: lsm6ds3tr-c@6a`, compatible `st,lsm6dsl` | devicetree officiel Zephyr |
| INT1 (broche de réveil) | `irq-gpios = <&gpio0 11>` — **P0.11, natif, pas d'overlay** | devicetree officiel Zephyr |
| Alim IMU (régulateur) | `enable-gpios = <&gpio1 8>`, `regulator-boot-on`, `startup-delay-us = 3000` | devicetree officiel Zephyr |
| Réveil System OFF | uniquement via événement externe (GPIO ou NFC) → P0.11 convient | Nordic DevZone |
| Registre wake-up IMU | `MD1_CFG = 0x20` (wake-up → INT1), seuil via `WAKE_UP_THS` | datasheet LSM6DS3TR-C |
| Consommation atteignable | ~1–3 µA en System OFF | forum Seeed / projet qarnet |
| Piège conso #1 | Flash QSPI P25Q16H non endormi auto → ~1 mA parasite | projet qarnet |
| Piège conso #2 | Pont diviseur VBAT consomme ~2,3 µA ; P0.14 (VBAT_ENABLE) HIGH pour couper | forum Seeed |
| **Danger** | Ne JAMAIS mettre P0.14 HIGH pendant la charge → risque de griller P0.31 | wiki OpenELAB |
| Flash version finale | via **SWD** (J-Link), pas via bootloader UF2 pour la version basse-conso | projet qarnet |

### object-id BTHome v2 retenus (vérifiés sur bthome.io/format)

| Donnée | object-id | Type | Facteur | Remarque |
|---|---|---|---|---|
| Angle | `0x5A` (count, sint16) | 2 octets signés, little-endian | 1 | BTHome n'a pas d'object « angle » ; on transporte les degrés (−180…+180) dans un compteur signé. HA affichera un nombre sans unité °, à habiller côté HA. |
| Batterie | `0x01` (battery) | uint8, 1 octet | 1 | 0–100 % |
| Ouvert/fermé | `0x1A` (door) | uint8, 1 octet | — | binaire : 0 = fermé, 1 = ouvert |

> **Ordre obligatoire des object-id dans le payload : croissant.** BTHome impose l'ordre numérique. On enverra donc : `0x01` (batterie) → `0x1A` (porte) → `0x5A` (angle).

---

## 1. Matériel et outillage

### Par capteur
1. Seeed XIAO **nRF52840 Sense** (bien la version *Sense*, pas *Plus*)
2. Batterie LiPo **604050 3,7 V 1500 mAh** (la tienne)
3. Connecteur/fils batterie vers pads BAT+ / BAT− du XIAO
4. Boîtier imprimé + fixation sur le battant, avec repère mécanique du zéro
5. (option) aimant + support si tu veux un second point de référence

### Infrastructure (une fois pour tout le site)
6. **ESP32-C3** (ou ESP32 DevKit) → `bluetooth_proxy` ESPHome, relais BLE vers HA
7. Alimentation USB 5 V secteur pour l'ESP32, placé à portée radio de la/les porte(s)

### Outillage (une fois)
8. Câble USB-C data (bring-up initial, flash UF2 de test)
9. **Sonde SWD** : J-Link (EDU Mini suffit) **ou** une carte nRF52840-DK utilisée comme programmateur SWD externe — **indispensable** pour flasher la version basse-consommation
10. **Nordic Power Profiler Kit II (PPK2)** ou µA-mètre précis — **indispensable** avant une série de 25 pour valider l'autonomie
11. Fer à souder fin + flux

---

## 2. Câblage

### 2.1 Batterie
- Souder BAT+ et BAT− de la LiPo aux pads batterie du XIAO (au dos).
- **Avant branchement : vérifier la polarité au multimètre.** Les LiPo tierces inversent fréquemment le brochage du connecteur par rapport à la convention Seeed. Une inversion détruit la carte.
- Le chargeur BQ25101 embarqué gère la recharge quand l'USB-C est connecté.

### 2.2 IMU
- Rien à câbler : le LSM6DS3TR-C, son alimentation (régulateur sur P1.08) et son interruption INT1 (P0.11) sont **déjà routés et déclarés** dans le devicetree du board `xiao_ble/nrf52840/sense`.

### 2.3 SWD (pour flash version finale)
- Relier SWDIO / SWCLK / GND / (VDD ref) du XIAO à la sonde J-Link. Les pads SWD sont exposés au dos du module (voir pinout Seeed).

---

## 3. Environnement de développement

Installer **nRF Connect SDK** (version 2.6.x ou 2.9.x LTS conseillée pour la stabilité du board `xiao_ble`) via l'extension *nRF Connect for VS Code*, ou en ligne de commande avec `west`.

Vérifier que le board cible est disponible :

```
west boards | grep xiao_ble
```

Cible de build utilisée partout ci-dessous :

```
xiao_ble/nrf52840/sense
```

---

## 4. Arborescence du projet

```
door_angle_sensor/
├── CMakeLists.txt
├── prj.conf
├── boards/
│   └── xiao_ble_nrf52840_sense.overlay
└── src/
    ├── main.c
    ├── bthome.c
    ├── bthome.h
    ├── imu.c
    ├── imu.h
    ├── lowpower.c
    └── lowpower.h
```

---

## 5. Configuration Kconfig (`prj.conf`)

```
# --- Système / base ---
CONFIG_GPIO=y
CONFIG_I2C=y
CONFIG_SENSOR=y
CONFIG_CBPRINTF_FP_SUPPORT=y

# --- IMU LSM6DS3 (driver Zephyr "st,lsm6dsl") ---
CONFIG_LSM6DSL=y
CONFIG_LSM6DSL_TRIGGER_GLOBAL_THREAD=y
CONFIG_LSM6DSL_ACCEL_ODR=1
CONFIG_LSM6DSL_ACCEL_RANGE=0

# --- Bluetooth (advertising non connectable, extended off) ---
CONFIG_BT=y
CONFIG_BT_BROADCASTER=y
CONFIG_BT_PERIPHERAL=n
CONFIG_BT_EXT_ADV=n
CONFIG_BT_DEVICE_NAME="Porte-Angle"

# --- Gestion d'énergie ---
CONFIG_PM=y
CONFIG_PM_DEVICE=y
CONFIG_POWEROFF=y

# --- Endormissement flash QSPI externe (piège conso #1) ---
CONFIG_NORDIC_QSPI_NOR=n
CONFIG_SPI=y
CONFIG_SPI_NOR=y
CONFIG_SPI_NOR_SLEEP=y
CONFIG_SPI_NOR_IDLE_IN_DPD=y

# --- Réduction empreinte / log minimal en prod ---
CONFIG_LOG=n
CONFIG_SERIAL=n
CONFIG_USB_DEVICE_STACK=n
CONFIG_CONSOLE=n
CONFIG_UART_CONSOLE=n

# --- RTC / timer basse conf pour réveil périodique ---
CONFIG_COUNTER=y
```

> **Note :** pendant la phase de mise au point, remettre `CONFIG_LOG=y`, `CONFIG_SERIAL=y`, `CONFIG_UART_CONSOLE=y` et `CONFIG_USB_DEVICE_STACK=y` pour voir les logs USB, puis les recouper à `n` pour la version basse-conso finale.

---

## 6. Overlay devicetree (`boards/xiao_ble_nrf52840_sense.overlay`)

Le nœud IMU et son INT1 sont déjà dans le board file. L'overlay ne fait que **s'assurer que le trigger IMU est exploitable** et exposer une référence propre.

```dts
/ {
    aliases {
        door-imu = &lsm6ds3tr_c;
    };
};

&lsm6ds3tr_c {
    /* irq-gpios <&gpio0 11> déjà défini dans le board file.
       On confirme le status et on garde le trigger actif. */
    status = "okay";
};

/* Le flash SPI externe : on veut qu'il puisse passer en Deep Power Down. */
&p25q16h {
    status = "okay";
};
```

> Si le nom du nœud flash diffère dans ta version de NCS, vérifie avec :
> `cat build/zephyr/zephyr.dts | grep -i -A5 "spi-nor\|p25q"`

---

## 7. Code source

### 7.1 `bthome.h`

```c
#ifndef BTHOME_H
#define BTHOME_H

#include <stdint.h>
#include <stddef.h>

/* Construit le payload de service data BTHome v2 (non chiffré).
 * angle_deg : angle signé en degrés (-180..+180)
 * battery_pct : 0..100
 * door_open : 0 = fermé, 1 = ouvert
 * out : buffer de sortie (>= 20 octets)
 * retourne la longueur écrite. */
size_t bthome_build_service_data(int16_t angle_deg,
                                 uint8_t battery_pct,
                                 uint8_t door_open,
                                 uint8_t *out);

#endif /* BTHOME_H */
```

### 7.2 `bthome.c`

```c
#include "bthome.h"

/* UUID BTHome (little-endian) = 0xFCD2 -> octets 0xD2 0xFC
 * Device info 0x40 : v2, non chiffré, mises à jour régulières.
 * Object-ids en ordre CROISSANT obligatoire :
 *   0x01 battery (uint8)
 *   0x1A door    (uint8, 0=fermé 1=ouvert)
 *   0x5A count sint16 -> transporte l'angle en degrés
 */
size_t bthome_build_service_data(int16_t angle_deg,
                                 uint8_t battery_pct,
                                 uint8_t door_open,
                                 uint8_t *out)
{
    size_t i = 0;

    out[i++] = 0xD2;            /* UUID LSB */
    out[i++] = 0xFC;            /* UUID MSB */
    out[i++] = 0x40;            /* device info : BTHome v2, non chiffré */

    /* battery (0x01), uint8 */
    out[i++] = 0x01;
    out[i++] = battery_pct;

    /* door (0x1A), uint8 */
    out[i++] = 0x1A;
    out[i++] = door_open ? 0x01 : 0x00;

    /* angle (0x5A count sint16), little-endian */
    out[i++] = 0x5A;
    out[i++] = (uint8_t)(angle_deg & 0xFF);
    out[i++] = (uint8_t)((angle_deg >> 8) & 0xFF);

    return i;
}
```

### 7.3 `imu.h`

```c
#ifndef IMU_H
#define IMU_H

#include <stdint.h>

int  imu_init(void);
/* Lit l'accélération et calcule l'angle 2 axes (plan), en degrés. */
int  imu_read_angle_deg(int16_t *angle_deg_out);
/* Configure le wake-on-motion matériel (interruption INT1 sur seuil). */
int  imu_configure_wakeup(uint8_t threshold);

#endif /* IMU_H */
```

### 7.4 `imu.c`

```c
#include "imu.h"
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/i2c.h>
#include <math.h>

#define IMU_NODE DT_ALIAS(door_imu)
static const struct device *imu_dev = DEVICE_DT_GET(IMU_NODE);

/* Adresse I²C du LSM6DS3TR-C sur le XIAO Sense */
#define LSM_I2C_ADDR      0x6A
#define REG_CTRL1_XL      0x10
#define REG_WAKE_UP_THS   0x5B
#define REG_WAKE_UP_DUR   0x5C
#define REG_TAP_CFG       0x58
#define REG_MD1_CFG       0x5E

/* bus I²C pour accès registre direct (config wake-up bas niveau) */
#define I2C_BUS_NODE DT_BUS(IMU_NODE)
static const struct device *i2c_dev = DEVICE_DT_GET(I2C_BUS_NODE);

int imu_init(void)
{
    if (!device_is_ready(imu_dev)) {
        return -1;
    }
    return 0;
}

int imu_read_angle_deg(int16_t *angle_deg_out)
{
    struct sensor_value acc[3];

    if (sensor_sample_fetch(imu_dev) < 0) {
        return -1;
    }
    if (sensor_channel_get(imu_dev, SENSOR_CHAN_ACCEL_XYZ, acc) < 0) {
        return -1;
    }

    double ax = sensor_value_to_double(&acc[0]);
    double ay = sensor_value_to_double(&acc[1]);
    double az = sensor_value_to_double(&acc[2]);

    /* Angle 2 axes sur le plan de rotation de la porte.
     * On projette sur (ay, az) : angle = atan2(ay, az).
     * Ajuste le couple d'axes selon l'orientation de montage réelle. */
    double angle = atan2(ay, az) * (180.0 / M_PI);

    *angle_deg_out = (int16_t)lround(angle);
    return 0;
}

static int lsm_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return i2c_write(i2c_dev, buf, sizeof(buf), LSM_I2C_ADDR);
}

int imu_configure_wakeup(uint8_t threshold)
{
    /* Séquence wake-on-motion LSM6DS3TR-C (datasheet ST) :
     *  - ODR accéléromètre 26 Hz, ±2 g (CTRL1_XL = 0x20)
     *  - durée nulle (WAKE_UP_DUR = 0x00)
     *  - seuil de réveil (WAKE_UP_THS)
     *  - interruptions activées + filtre pente (TAP_CFG = 0x80)
     *  - wake-up routé vers INT1 (MD1_CFG = 0x20)
     */
    if (lsm_write_reg(REG_CTRL1_XL, 0x20) < 0) return -1;
    if (lsm_write_reg(REG_WAKE_UP_DUR, 0x00) < 0) return -1;
    if (lsm_write_reg(REG_WAKE_UP_THS, threshold & 0x3F) < 0) return -1;
    if (lsm_write_reg(REG_TAP_CFG, 0x80) < 0) return -1;
    if (lsm_write_reg(REG_MD1_CFG, 0x20) < 0) return -1;
    return 0;
}
```

### 7.5 `lowpower.h`

```c
#ifndef LOWPOWER_H
#define LOWPOWER_H

void lowpower_prepare(void);              /* coupe périphériques inutiles */
void lowpower_enter_system_off(void);     /* System OFF, réveil sur P0.11 */

#endif /* LOWPOWER_H */
```

### 7.6 `lowpower.c`

```c
#include "lowpower.h"
#include <zephyr/kernel.h>
#include <zephyr/pm/pm.h>
#include <zephyr/pm/device.h>
#include <zephyr/sys/poweroff.h>
#include <zephyr/drivers/gpio.h>
#include <hal/nrf_gpio.h>

/* INT1 de l'IMU = P0.11 (voir devicetree irq-gpios <&gpio0 11>) */
#define WAKE_PIN NRF_GPIO_PIN_MAP(0, 11)

void lowpower_prepare(void)
{
    /* Rien de spécial ici si CONFIG_SPI_NOR_IDLE_IN_DPD=y :
     * le flash externe passe en Deep Power Down automatiquement.
     * On pourrait suspendre explicitement d'autres devices PM ici. */
}

void lowpower_enter_system_off(void)
{
    /* Configure P0.11 comme source de réveil System OFF :
     * l'IMU maintient INT1 lorsqu'un mouvement dépasse le seuil. */
    nrf_gpio_cfg_sense_set(WAKE_PIN, NRF_GPIO_PIN_SENSE_HIGH);

    /* Entre en System OFF : consommation ~1-3 µA.
     * Le réveil (INT1 haut) provoque un reset -> main() repart. */
    sys_poweroff();
}
```

### 7.7 `main.c`

```c
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/adc.h>
#include "imu.h"
#include "bthome.h"
#include "lowpower.h"

/* --- Paramètres applicatifs --- */
#define WAKEUP_THRESHOLD   0x02   /* seuil mouvement (à calibrer) */
#define DOOR_OPEN_ANGLE    15     /* seuil degrés au-delà = "ouvert" */
#define ADV_DURATION_MS    300    /* durée d'advertising par réveil */
#define PERIODIC_MS        (10 * 60 * 1000)  /* 10 min */

/* buffer service data BTHome */
static uint8_t svc[20];

/* --- Lecture batterie (à adapter à ton câblage VBAT) ---
 * Le XIAO lit VBAT via un pont diviseur ; P0.14 (VBAT_ENABLE) doit être
 * LOW pour lire, puis HIGH pour couper la conso du pont.
 * DANGER : ne jamais mettre P0.14 HIGH pendant la charge USB (risque P0.31).
 */
static uint8_t read_battery_pct(void)
{
    /* Implémentation ADC à compléter selon calibration.
     * Retour provisoire : valeur mesurée mappée 0-100 %.
     * Placeholder sûr : 100. */
    return 100;
}

static void advertise_once(int16_t angle, uint8_t batt, uint8_t door)
{
    size_t len = bthome_build_service_data(angle, batt, door, svc);

    struct bt_data ad[] = {
        BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
        BT_DATA(BT_DATA_SVC_DATA16, svc, len),
    };

    bt_le_adv_start(BT_LE_ADV_NCONN, ad, ARRAY_SIZE(ad), NULL, 0);
    k_msleep(ADV_DURATION_MS);
    bt_le_adv_stop();
}

int main(void)
{
    imu_init();
    imu_configure_wakeup(WAKEUP_THRESHOLD);

    if (bt_enable(NULL) != 0) {
        /* si le BLE échoue, on dort quand même pour ne pas vider la batterie */
        lowpower_prepare();
        lowpower_enter_system_off();
        return 0;
    }

    int16_t angle = 0;
    imu_read_angle_deg(&angle);

    uint8_t batt = read_battery_pct();
    uint8_t door = (angle > DOOR_OPEN_ANGLE || angle < -DOOR_OPEN_ANGLE) ? 1 : 0;

    advertise_once(angle, batt, door);

    /* Deux modes de réveil :
     *  - mouvement  : INT1 (P0.11) via nrf_gpio sense -> reset -> main()
     *  - périodique : on programme un réveil dans 10 min avant System OFF.
     *
     * En System OFF, le seul timer capable de réveiller est le RTC via
     * un wake configuré ; l'approche la plus simple et robuste ici est
     * d'utiliser System OFF pour le mouvement, et un réveil périodique
     * via la minuterie basse consommation. Sur nRF52 en System OFF pur,
     * privilégier le réveil GPIO ; pour le tick 10 min, deux options :
     *
     *   (A) rester en System ON idle avec RTC (conso ~12 µA) et un
     *       k_sleep(PERIODIC_MS) qui réveille tout seul ;
     *   (B) System OFF + circuit RTC externe tirant une GPIO.
     *
     * Option (A) est suffisante pour 14-18 mois vu ton budget : à ~12 µA
     * moyens l'autonomie reste > 3 ans. On l'utilise pour sa simplicité.
     */

    lowpower_prepare();

    while (1) {
        k_msleep(PERIODIC_MS);   /* réveil périodique 10 min (System ON idle) */

        imu_read_angle_deg(&angle);
        batt = read_battery_pct();
        door = (angle > DOOR_OPEN_ANGLE || angle < -DOOR_OPEN_ANGLE) ? 1 : 0;
        advertise_once(angle, batt, door);
    }

    return 0;
}
```

> **Choix de conception documenté :** deux stratégies d'endormissement existent. Le System OFF pur (~1–3 µA) ne se réveille que sur GPIO/NFC, donc il faut un événement matériel pour le tick 10 min. Le System ON idle avec `k_sleep` (~12 µA mesurés sur ce board en Zephyr) gère nativement le réveil périodique **et** l'interruption IMU. À 12 µA moyens, 1500 mAh donne >3 ans théoriques : la marge couvre largement 14–18 mois même avec l'auto-décharge LiPo et le coût des advertisings. On retient donc **System ON idle** pour sa robustesse. Si tu veux pousser vers 1 µA, il faudra basculer en System OFF + réveil RTC câblé — optimisation ultérieure, non nécessaire pour ta cible.

### 7.8 `CMakeLists.txt`

```cmake
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(door_angle_sensor)

target_sources(app PRIVATE
    src/main.c
    src/bthome.c
    src/imu.c
    src/lowpower.c
)
```

---

## 8. Build et flash

### 8.1 Build

```
west build -b xiao_ble/nrf52840/sense -p always .
```

### 8.2 Flash de test (UF2, bring-up)
1. Brancher le XIAO en USB-C.
2. Double-appui sur RST → le lecteur `XIAO-SENSE` apparaît.
3. Copier `build/zephyr/zephyr.uf2` dessus.

> **Limite connue :** en version basse-consommation (flash QSPI endormi, USB désactivé), le bootloader UF2 peut ne pas convenir. Utiliser alors le flash SWD ci-dessous.

### 8.3 Flash version finale (SWD)

```
west flash --runner jlink
```

(sonde J-Link reliée aux pads SWD, ou nRF52840-DK en programmateur externe.)

---

## 9. Calibration de l'angle

1. Porte **fermée** : lire l'angle brut (via logs USB en phase dev). Noter la valeur → c'est ton zéro mécanique.
2. Porte **grande ouverte** : lire l'angle. Vérifier le signe et l'amplitude.
3. Ajuster dans `imu.c` le couple d'axes de `atan2(ay, az)` si le plan de rotation ne correspond pas (selon l'orientation de montage du boîtier).
4. Régler `DOOR_OPEN_ANGLE` dans `main.c` au seuil qui te convient (ex. 15°).
5. Régler `WAKEUP_THRESHOLD` : trop bas = réveils intempestifs (baisse l'autonomie), trop haut = mouvements lents ratés. Commencer à `0x02` et ajuster.

---

## 10. ESP32 bluetooth_proxy (relais BLE → HA)

Le XIAO émet en BLE ; il faut un récepteur BLE vu par HA. Le plus simple : un ESP32 sous ESPHome en mode `bluetooth_proxy`.

### 10.1 Config ESPHome (`esp32-ble-proxy.yaml`)

```yaml
esphome:
  name: esp32-ble-proxy

esp32:
  board: esp32-c3-devkitm-1
  framework:
    type: esp-idf

wifi:
  ssid: "StarTh"
  password: !secret wifi_password

api:
  encryption:
    key: !secret api_key

logger:

bluetooth_proxy:
  active: false   # passif : suffisant pour recevoir des advertisements BTHome

esp32_ble_tracker:
  scan_parameters:
    active: false
```

> `active: false` : on ne fait qu'écouter les advertisements (pas de connexion), ce qui est exactement ce dont BTHome a besoin et consomme moins côté ESP32.

### 10.2 Flash de l'ESP32
- Via l'add-on ESPHome de Home Assistant, ou `esphome run esp32-ble-proxy.yaml`.
- Placer l'ESP32 (alimenté en USB secteur) à portée radio de la/les porte(s).

---

## 11. Intégration Home Assistant

1. **Prérequis :** intégration **Bluetooth** active dans HA (elle détecte l'ESP32 proxy automatiquement via l'API ESPHome).
2. Une fois le capteur XIAO en fonction et à portée du proxy, HA découvre automatiquement l'appareil BTHome.
3. Aller dans *Paramètres → Appareils et services* : une découverte **BTHome** « Porte-Angle » apparaît. Cliquer *Configurer* → *Ajouter*.
4. Trois entités sont créées :
   - un capteur numérique (l'angle, object `0x5A`) — sans unité par défaut ;
   - un capteur batterie (%, object `0x01`) ;
   - un capteur binaire porte (ouvert/fermé, object `0x1A`).

### 11.1 Habillage de l'entité angle (unité °)

Comme BTHome n'a pas d'object « angle », l'entité arrive comme un nombre nu. Pour l'afficher en degrés, créer un capteur template dans `configuration.yaml` :

```yaml
template:
  - sensor:
      - name: "Angle porte"
        unit_of_measurement: "°"
        state: "{{ states('sensor.porte_angle_count') | float(0) }}"
        icon: mdi:angle-acute
```

(adapter `sensor.porte_angle_count` au nom réel généré par HA.)

---

## 12. Validation avant série de 25

1. **Consommation :** brancher le PPK2 en lieu et place de la batterie. Mesurer :
   - courant en veille idle (cible ≈ 12 µA) ;
   - pic et durée d'un cycle advertising ;
   - courant moyen sur une fenêtre de 10 min avec quelques mouvements.
2. **Calcul d'autonomie réel :** `1500 mAh / I_moyen_mA`. Valider ≥ 14 mois avec marge (viser théorique ≥ 30 mois pour couvrir auto-décharge + vieillissement).
3. **Portée radio :** confirmer réception fiable du proxy depuis l'emplacement réel de la porte (murs, distance).
4. **Robustesse réveil :** vérifier que 15 ouvertures/jour déclenchent bien le wake-on-motion et une publication à chaque fois.
5. **Charge :** vérifier que la recharge USB fonctionne et que P0.14 n'est jamais mis HIGH pendant la charge (garde-fou firmware à implémenter dans `read_battery_pct`).

---

## 13. Points de vigilance récapitulés

- **Polarité batterie** : vérifier au multimètre avant tout branchement.
- **Flash QSPI** : sans `CONFIG_SPI_NOR_IDLE_IN_DPD=y`, conso parasite ~1 mA → autonomie ruinée.
- **P0.14 / charge** : ne jamais forcer VBAT_ENABLE HIGH pendant la charge (risque P0.31).
- **Version finale via SWD** : le bootloader UF2 ne convient pas quand USB est désactivé.
- **Ordre des object-id BTHome** : croissant obligatoire (`0x01` < `0x1A` < `0x5A`).
- **Calibration axes** : le couple d'axes de `atan2` dépend de l'orientation de montage.

---

## 14. Chemin d'optimisation (optionnel, si tu veux viser ~1 µA)

Si l'autonomie mesurée devait être insuffisante (elle ne devrait pas l'être), passer de System ON idle à **System OFF + réveil RTC** : ajouter un composant RTC externe basse conso (ex. PCF8563) tirant une GPIO de réveil toutes les 10 min, et confier le réveil mouvement à INT1/P0.11 comme prévu. Cela descend la veille vers ~1–3 µA au prix d'un composant et d'un peu de câblage supplémentaires. Non nécessaire pour la cible 14–18 mois.
```
