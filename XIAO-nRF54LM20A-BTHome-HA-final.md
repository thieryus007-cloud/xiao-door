# XIAO nRF54LM20A Sense → BLE Proxy → Home Assistant via BTHome v2

**Spécification technique de conception et de mise en œuvre**

Transmettre l'ensemble des données capteur de la carte Seeed XIAO nRF54LM20A Sense — hors microphone — vers Home Assistant, en BLE advertising au format BTHome v2, relayé par des Bluetooth Proxies ESPHome, avec une consommation optimisée pour un fonctionnement sur batterie de plusieurs années.

---

## 1. Matériel — Seeed XIAO nRF54LM20A Sense

| Élément | Valeur |
|---|---|
| SoC | Nordic **nRF54LM20A**, Cortex-M33 @128 MHz + coprocesseur RISC-V FLPR 128 MHz |
| Mémoire | 512 KB RAM, ~1,5 MB RRAM (2 MB NVM annoncés) |
| Flash externe | PY25Q64HA, 64 Mbit (8 MB), SPI |
| Radio | **Bluetooth LE 6.0** (+ Channel Sounding), Matter, Thread, Zigbee, 2,4 GHz propriétaire, NFC |
| IMU | **ST LSM6DS3TR-C** — accéléromètre 3 axes + gyroscope 3 axes, fonctions embarquées (wake-up, free-fall, tap/double-tap, activity/inactivity, 6D/4D orientation, tilt, significant motion), capteur de température interne |
| Micro | MSM261DGT006 (PDM) — **hors périmètre** |
| PMIC | Nordic **nPM1300** (charge Li-Po, LDO/buck, mesure VBAT, NTC, ship mode) |
| Consommation | System OFF **~4,76 µA**, Ship Mode **0,33 µA** (mesures Seeed, batterie 3,7 V) |
| Toolchain | **nRF Connect SDK v3.3.0** (recommandé) ou PlatformIO — Zephyr RTOS |

### 1.1 Alimentation de l'IMU — LDO1 du nPM1300

Sur les variantes **Sense**, l'IMU et le microphone PDM sont alimentés par le **LDO1 du nPM1300**, configuré à **1,8 V** dans les définitions de carte Zephyr standard. Les deux périphériques exigent **3,3 V**.

Sans correction dans le devicetree, le driver LSM6DS3TR-C échoue au `probe` (WHO_AM_I invalide) ou remonte des valeurs aberrantes. Il faut régler **LDO1 à 3,3 V** et **différer l'initialisation** du capteur (`zephyr,deferred-init`) pour que l'application alimente le rail avant que le driver ne sonde le bus. Voir §5.3.

### 1.2 Limites matérielles à intégrer dans la conception

**Pas de capteur de température ambiante.** La température disponible est soit le die du SoC (chauffé par le CPU et la radio), soit `OUT_TEMP` du LSM6DS3TR-C (précision typique de l'ordre de ±10–15 °C absolus, résolution 1/256 °C). Aucune des deux ne constitue une mesure d'ambiance exploitable.
→ La publier comme **diagnostic interne** (`entity_category: diagnostic` côté HA), ou ajouter un SHT4x/BME280 en I²C si une température de pièce est requise.

**Pas de magnétomètre**, donc **le yaw absolu n'est pas observable.** Un LSM6DS3TR-C seul fournit pitch et roll absolus (via le vecteur gravité) ; le yaw dérive par intégration gyroscopique.
→ Pour une application d'angle d'ouvrant (porte/fenêtre), c'est sans conséquence : il suffit d'orienter le capteur de sorte que l'axe de rotation de l'ouvrant soit perpendiculaire à la gravité. L'angle devient alors un pitch ou un roll absolu, stable et sans dérive.

---

## 2. Format BTHome v2

### 2.1 Structure de la trame BLE

```
[ AD Flags ] [ AD Local Name (optionnel) ] [ AD Service Data 16-bit UUID ]
```

| Élément | Octets | Contenu |
|---|---|---|
| AD Flags | 3 | `02 01 06` — **indispensable**. BlueZ, utilisé par l'intégration Bluetooth de Home Assistant, ne parse pas l'advertising en scan passif sans ce champ. |
| AD Local Name | 2 + N | `LL 09 <ascii>` (complet) ou `LL 08 <ascii>` (abrégé) |
| AD Service Data | 4 + M | `LL 16 D2 FC <device_info> <mesures…>` — UUID `0xFCD2` en little-endian |

**Budget total en advertising legacy : 31 octets.** Cette limite est structurante (voir §6.1).

### 2.2 Octet Device Info

| Bit | Signification | Valeur retenue |
|---|---|---|
| 0 | Chiffrement | 0 (clair) ou 1 (AES-CCM) |
| 1 | Réservé | 0 |
| 2 | **Trigger-based device** | **1** (émission irrégulière) |
| 3-4 | Réservé | 0 |
| 5-7 | Version BTHome | `010` = v2 |

| Valeur | Sens |
|---|---|
| `0x40` | v2, clair, périodique |
| **`0x44`** | **v2, clair, trigger-based ← retenu pour toutes les trames** |
| `0x41` | v2, chiffré, périodique |
| `0x45` | v2, chiffré, trigger-based |

**Pourquoi `0x44` sur toutes les trames.** Le bit trigger-based indique au récepteur que les émissions sont irrégulières, ce qui l'empêche de marquer le device indisponible. Home Assistant apprend l'intervalle d'advertising par appareil et applique par défaut une fenêtre d'indisponibilité de **5 minutes** (`UNAVAILABLE_TRACK_SECONDS = 60 * 5`) aux devices non « sleepy ». Une trame périodique à 15 min annoncée en `0x40` ferait passer le capteur en `unavailable` la majorité du temps. Alterner `0x40` et `0x44` sur une même adresse MAC fait osciller l'état, ce qui est encore moins souhaitable. **Valeur constante `0x44` partout.**

### 2.3 Règles impératives du format

1. **Object IDs en ordre numérique strictement croissant** dans chaque trame. Un récepteur BTHome cesse de parser dès qu'il rencontre un ID hors ordre ou inconnu ; les mesures suivantes sont silencieusement perdues.
2. **Mesures multiples du même type autorisées** : elles sont suffixées côté HA (`rotation`, `rotation_2`, `rotation_3`) **dans l'ordre de la trame**. Cet ordre doit être identique à chaque advertising, sinon les valeurs se retrouvent sur les mauvaises entités.
3. **Little-endian** pour toutes les valeurs multi-octets.
4. **Binary sensors** : toujours 1 octet, valeur `0x00` / `0x01`.
5. **Packet ID (`0x00`)** : uint8, incrémenté à chaque changement de données. Les récepteurs ignorent l'advertising si le packet id est identique au précédent.

### 2.4 Object IDs utilisés dans ce projet

**Capteurs (sensor)**

| ID | Propriété | Type | Taille | Facteur | Plage | Unité |
|---|---|---|---|---|---|---|
| `0x00` | packet id | uint8 | 1 | 1 | 0–255 | — |
| `0x01` | battery | uint8 | 1 | 1 | 0–100 | % |
| `0x02` | temperature | sint16 | 2 | 0.01 | ±327,67 | °C |
| `0x0C` | voltage | uint16 | 2 | 0.001 | 0–65,535 | V |
| `0x3F` | **rotation** | **sint16** | 2 | 0.1 | **±3276,7** | ° |
| `0x45` | temperature | sint16 | 2 | 0.1 | ±3276,7 | °C |
| `0x51` | acceleration | **uint16** | 2 | 0.001 | **0–65,535** | m/s² |
| `0x52` | gyroscope | **uint16** | 2 | 0.001 | **0–65,535** | °/s |
| `0x5A` | count | sint16 | 2 | 1 | ±32767 | — |
| `0x63` | acceleration (signée) | sint32 | 4 | 0.000001 | ±2147,48 | m/s² |
| `0xF2` | firmware version | uint24 | 3 | — | x.y.z | — |

**Capteurs binaires (binary_sensor)** — toujours 1 octet

| ID | Propriété | 0 / 1 |
|---|---|---|
| `0x0F` | generic boolean | Off / On |
| `0x11` | opening | Closed / Open |
| `0x15` | battery (low) | Normal / Low |
| `0x16` | **battery charging** | Not charging / Charging |
| `0x1A` | door | Closed / Open |
| `0x21` | motion | Clear / Detected |
| `0x22` | moving | Not moving / Moving |
| `0x2B` | tamper | Off / On |
| `0x2C` | vibration | Clear / Detected |
| `0x2D` | window | Closed / Open |

**Événements (event)**

| ID | Type | Event id |
|---|---|---|
| `0x3A` | button | `0x00` none · `0x01` press · `0x02` double_press · `0x03` triple_press · `0x04` long_press · `0x05` long_double_press · `0x06` long_triple_press · `0x80` hold_press |

### 2.5 Correspondances retenues pour les événements IMU

BTHome ne définit pas d'Object ID natif pour « free fall », « double tap » ou « activity ». Mapping retenu, à documenter dans le nom d'entité Home Assistant :

| Événement LSM6DS3TR-C | Object ID BTHome | Entité HA | Renommage |
|---|---|---|---|
| Wake-up / motion | `0x21` motion | binary_sensor | *Mouvement* |
| Activity / Inactivity | `0x0F` generic boolean | binary_sensor | *Activité* |
| Free-fall / choc | `0x2B` tamper | binary_sensor | *Chute / Choc* |
| Double-tap | `0x2C` vibration | binary_sensor | *Double-tap* |
| Bouton utilisateur | `0x3A` button | event | *Bouton* |
| Pitch / Roll / Yaw | `0x3F` ×3 | sensor ×3 | *Pitch* / *Roll* / *Yaw* |

**Remise à zéro des binaires.** BTHome est sans état : un `motion = 1` envoyé une fois reste à `on` indéfiniment côté HA. Il faut émettre une trame de retour au repos, tous les binaires à `0`, après le timeout d'événement (typiquement 5 à 30 s).

---

## 3. Architecture de trames

Trois trames, toutes en `0x44`, avec un **compteur Packet ID unique partagé**.

```
                 ┌─────────────────────────────────────────┐
                 │  XIAO nRF54LM20A Sense (émetteur)       │
                 │  Adresse BLE statique, stable           │
                 └─────────────────────────────────────────┘
                        │                │              │
     ┌──────────────────┘                │              └──────────────────┐
     ▼                                   ▼                                 ▼
┌──────────────┐              ┌────────────────────┐          ┌───────────────────────┐
│ TRAME A      │              │ TRAME B            │          │ TRAME C  (optionnelle)│
│ Événement    │              │ Périodique 15 min  │          │ IMU brut              │
│ + angles     │              │ + nom de l'appareil│          │ (magnitudes)          │
│ 29 / 31 o.   │              │ 31 / 31 o.         │          │ 28 / 31 o.            │
└──────────────┘              └────────────────────┘          └───────────────────────┘
```

### 3.1 TRAME A — Événement de mouvement + orientation

Émise uniquement sur événement : wake-up IMU, franchissement de seuil angulaire, appui bouton, retour au repos.

| Ordre | Object ID | Donnée | Octets |
|---|---|---|---|
| 1 | `0x00` | Packet ID | 2 |
| 2 | `0x0F` | Activity (generic boolean) | 2 |
| 3 | `0x21` | Motion | 2 |
| 4 | `0x2B` | Free-fall / choc (tamper) | 2 |
| 5 | `0x2C` | Double-tap (vibration) | 2 |
| 6 | `0x3A` | Bouton (event) | 2 |
| 7 | `0x3F` | **Pitch** (sint16, 0.1°) | 3 |
| 8 | `0x3F` | **Roll** (sint16, 0.1°) | 3 |
| 9 | `0x3F` | **Yaw** (sint16, 0.1°) | 3 |
| | | **Total mesures** | **21** |

**Budget : 3 (flags) + 26 (service data) = 29 / 31 octets.**

Exemple — pitch = +12,5° · roll = −3,2° · yaw = +90,0° · motion actif · packet id 42 :

```
02 01 06                          ← Flags
19 16 D2 FC 44                    ← len=25, service data, UUID FCD2, device info 0x44
   00 2A                          ← packet id = 42
   0F 01                          ← activity = 1
   21 01                          ← motion = 1
   2B 00                          ← tamper = 0
   2C 00                          ← vibration = 0
   3A 00                          ← button = none
   3F 7D 00                       ← rotation_1 = 0x007D = 125 → +12.5 °
   3F E0 FF                       ← rotation_2 = 0xFFE0 = -32 → -3.2 °
   3F 84 03                       ← rotation_3 = 0x0384 = 900 → +90.0 °
```

### 3.2 TRAME B — Périodique (batterie / santé), 15 min

Cette trame porte également le **nom de l'appareil** : le nom n'entre pas dans la trame A, et Home Assistant conserve le nom appris pour une adresse MAC donnée.

| Ordre | Object ID | Donnée | Octets |
|---|---|---|---|
| 1 | `0x00` | Packet ID | 2 |
| 2 | `0x01` | Battery level (%) | 2 |
| 3 | `0x02` | Température interne (0.01 °C) | 3 |
| 4 | `0x0C` | Tension batterie (0.001 V) | 3 |
| 5 | `0x15` | Battery low | 2 |
| 6 | `0x16` | Battery charging | 2 |
| | | **Total mesures** | **14** |

**Budget : 3 (flags) + 9 (nom, 7 caractères) + 19 (service data) = 31 / 31 octets — limite exacte.**

Exemple — nom `SANT-01`, 87 %, 21,35 °C, 3,905 V, pas de charge, packet id 43 :

```
02 01 06                          ← Flags
08 09 53 41 4E 54 2D 30 31        ← Complete Local Name "SANT-01"
12 16 D2 FC 44                    ← len=18, service data, UUID FCD2, device info 0x44
   00 2B                          ← packet id = 43
   01 57                          ← battery = 87 %
   02 57 08                       ← temperature = 0x0857 = 2135 → 21.35 °C
   0C 41 0F                       ← voltage = 0x0F41 = 3905 → 3.905 V
   15 00                          ← battery low = 0
   16 00                          ← charging = 0
```

Le nom est limité à **7 caractères** dans cette configuration. Pour un nom plus long, alléger la trame B — par exemple supprimer `0x15`, dérivable côté HA d'un seuil sur `0x01`. Chaque objet binaire supprimé libère 2 caractères.

### 3.3 TRAME C — IMU brut (optionnelle)

Transmettre `accel_x/y/z` et `gyro_x/y/z` instantanés dans un beacon BLE apporte peu de valeur applicative : ce sont des échantillons ponctuels non corrélés temporellement, arrivant à intervalle irrégulier et sans horodatage. L'information utile — pitch/roll et événements — est déjà dans la trame A et est calculée à bord, où les données sont disponibles à pleine cadence.

Si la trame est néanmoins souhaitée, deux contraintes de format s'appliquent :

- **`0x51` et `0x52` sont non signés.** Ils ne peuvent porter que des **magnitudes** : ‖a‖ et ‖ω‖. Pour des composantes signées, utiliser `0x63` (sint32, 4 octets) ; il n'existe pas d'équivalent signé pour le gyroscope.
- **`0x52` sature à 65,535 °/s** alors que le LSM6DS3TR-C monte à ±2000 °/s. Il faut soit clamper, soit configurer le gyroscope en ±125 dps et accepter la saturation au-delà de 65 °/s.

| Ordre | Object ID | Donnée | Octets |
|---|---|---|---|
| 1 | `0x00` | Packet ID | 2 |
| 2–4 | `0x51` ×3 | \|a_x\|, \|a_y\|, \|a_z\| | 9 |
| 5–7 | `0x52` ×3 | \|ω_x\|, \|ω_y\|, \|ω_z\| | 9 |
| | | **Total mesures** | **20** |

**Budget : 3 + 25 = 28 / 31 octets.**

**Variante recommandée** si la trame C est conservée : n'envoyer que **‖a‖** (`0x51` ×1) et **‖ω‖** (`0x52` ×1), soit 6 octets de mesures. 14 octets restent libres pour le nom, et l'information reste directement interprétable.

### 3.4 Variante chiffrée (AES-CCM)

Le chiffrement ajoute **8 octets** (compteur 4 + MIC 4). Le compteur remplace fonctionnellement le Packet ID, qui est alors supprimé.

| | Clair | Chiffré (`0x45`) |
|---|---|---|
| Mesures max, sans nom | 23 o. | **15 o.** |
| Mesures max, nom 7 caractères | 14 o. | 6 o. |

**Trame A chiffrée réalisable** (15 octets) : `0x21` motion (2) + `0x2B` tamper (2) + `0x2C` vibration (2) + `0x3F` ×3 (9). Il faut renoncer à `0x0F` et `0x3A`.

Clé = **16 octets / 32 caractères hexadécimaux** — le *bindkey* demandé par Home Assistant à l'ajout du device.

Sur un réseau domestique maîtrisé, le chiffrement BTHome protège surtout contre l'**usurpation** : rejeu ou injection de faux angles. Compromis proposé pour un parc de 25 capteurs d'ouvrants : **clair pour la mise au point, chiffré en production sur les ouvrants donnant sur l'extérieur.**

---

## 4. Stratégie d'émission et politique de réveil

### 4.1 Événements déclencheurs (trame A)

| Déclencheur | Source | Mise en œuvre |
|---|---|---|
| Réveil sur mouvement | LSM6DS3TR-C `WAKE_UP` → INT1 → GPIO | Seuil `WK_THS`, durée `WAKE_DUR` |
| Franchissement angulaire | Calcul embarqué pitch/roll | Hystérésis logicielle, ex. ±2° |
| Free-fall / choc | LSM6DS3TR-C `FF` → INT1 | Registre `FREE_FALL` |
| Double-tap | LSM6DS3TR-C `TAP_SRC` → INT1 | `TAP_CFG` / `INT_DUR2` |
| Inactivity (retour repos) | LSM6DS3TR-C `SLEEP_CHANGE` | Remet tous les binaires à 0 |
| Bouton | GPIO | Anti-rebond ≥ 30 ms |

### 4.2 Train d'advertising

Un capteur qui émet un seul advertising event a une probabilité de réception faible : le proxy scanne un seul canal primaire (37, 38 ou 39) à un instant donné et alterne à chaque intervalle de scan.

Politique retenue par trame émise :

| Paramètre | Valeur | Justification |
|---|---|---|
| Intervalle d'advertising | **100 ms** | Chaque adv event balaie les 3 canaux primaires |
| Durée du train | **700 ms** | ≈ 7 adv events, packet id **constant** sur tout le train |
| Type de PDU | `ADV_NONCONN_IND` (non connectable, non scannable) | Pas de SCAN_REQ, pas de CONNECT_IND |
| PHY | **LE 1M uniquement** | LE Coded et 2M ne sont pas scannés par les proxies |
| Advertising | **Legacy** | Voir §6.1 |

Coût énergétique d'un train : ~7 events × 3 canaux × ~1 ms actif ≈ **21 ms de radio TX**, soit de l'ordre de **0,1 mAs** par événement à 0 dBm. Négligeable.

### 4.3 Anti-rebond et limitation de débit

Pour éviter qu'un ouvrant en mouvement continu ne sature le canal et la batterie :

| Règle | Valeur |
|---|---|
| Intervalle minimum entre deux trames A | **2 s** |
| Nombre max de trames A par minute | **10** (fenêtre glissante) |
| Envoi angulaire seulement si Δ > | **2,0 °** sur pitch ou roll |
| Trame « repos » après inactivité | **15 s** |
| Trame B | toutes les **15 min** (± 30 s de jitter aléatoire) |
| Heartbeat forcé (trame A état repos) | toutes les **60 min** si aucun événement |

Le **jitter** sur la trame B est important avec 25 capteurs : sans lui, les nœuds redémarrés simultanément après une coupure se synchronisent et émettent tous dans la même seconde toutes les 15 min, provoquant collisions et pertes.

### 4.4 Bilan énergétique prévisionnel

| Poste | Courant moyen estimé |
|---|---|
| nRF54LM20A, System ON idle (GRTC actif, RAM retenue) | 5 – 10 µA |
| LSM6DS3TR-C, accéléromètre seul en low-power @26 Hz, gyro coupé | 10 – 50 µA *(à mesurer)* |
| nPM1300, courant de repos | 5 – 15 µA *(à mesurer)* |
| Radio (trames A+B moyennées, usage domestique) | < 5 µA |
| **Total estimé** | **~25 – 80 µA** |

Sur une LiPo 1000 mAh : **1,5 à 4 ans** théoriques, à réduire d'environ 30 % pour l'autodécharge et le froid. Sur un ouvrant très sollicité, viser **1 an**.

**Régime d'alimentation retenu : System ON + Power Management Zephyr, pas System OFF.** Sur nRF54, la sortie de System OFF redémarre depuis `main()` sans état retenu : le compteur Packet ID, la calibration d'offset angulaire et le contexte BLE seraient perdus à chaque réveil. Le gain (~5 µA) est marginal face à l'IMU, qui doit de toute façon rester alimentée pour détecter le mouvement. System OFF et Ship Mode sont réservés au stockage et au transport.

---

## 5. Firmware — nRF Connect SDK / Zephyr

### 5.1 Adresse BLE

Home Assistant identifie un device BTHome **par son adresse MAC**. Si l'adresse change au redémarrage, HA crée un nouveau device à chaque reboot ; sur un parc de 25 capteurs, l'inventaire devient rapidement ingérable.

**Stratégie retenue — adresse statique aléatoire dérivée du FICR.** L'identifiant unique gravé en usine du SoC sert de graine : l'adresse est identique à chaque boot, reproductible, sans dépendance au stockage de configuration ni usure de la RRAM.

```c
#include <zephyr/bluetooth/bluetooth.h>
#include <hal/nrf_ficr.h>
#include <zephyr/sys/byteorder.h>

static int santuario_set_identity(void)
{
    bt_addr_le_t addr = { .type = BT_ADDR_LE_RANDOM };
    uint32_t id0 = NRF_FICR->INFO.DEVICEID[0];
    uint32_t id1 = NRF_FICR->INFO.DEVICEID[1];

    addr.a.val[0] = (uint8_t)(id0);
    addr.a.val[1] = (uint8_t)(id0 >> 8);
    addr.a.val[2] = (uint8_t)(id0 >> 16);
    addr.a.val[3] = (uint8_t)(id1);
    addr.a.val[4] = (uint8_t)(id1 >> 8);
    /* Adresse statique aléatoire : les 2 bits de poids fort de l'octet
       le plus significatif doivent valoir 0b11. */
    addr.a.val[5] = (uint8_t)(id1 >> 16) | 0xC0;

    return bt_id_create(&addr, NULL);
}
```

À appeler **avant** `bt_enable()`, avec `CONFIG_BT_PRIVACY=n`.

**Alternative** : persistance par settings (`CONFIG_BT_SETTINGS=y` + backend ZMS/NVS sur RRAM). Fonctionnelle, mais dépendante de l'intégrité du stockage et plus contraignante pour un reflashage de masse. À réserver aux cas où l'adresse doit pouvoir être régénérée.

### 5.2 `prj.conf`

```conf
# --- Bluetooth ---
CONFIG_BT=y
CONFIG_BT_PERIPHERAL=n
CONFIG_BT_BROADCASTER=y
CONFIG_BT_OBSERVER=n
CONFIG_BT_DEVICE_NAME="SANT-01"
CONFIG_BT_DEVICE_NAME_DYNAMIC=y
CONFIG_BT_DEVICE_NAME_MAX=8
CONFIG_BT_PRIVACY=n
CONFIG_BT_EXT_ADV=n            # advertising legacy imposé par la chaîne proxy (§6.1)
CONFIG_BT_CTLR_PHY_2M=n
CONFIG_BT_CTLR_PHY_CODED=n

# --- Capteurs ---
CONFIG_SENSOR=y
CONFIG_LSM6DSO=n
CONFIG_LSM6DS3TR_C=y
CONFIG_LSM6DS3TR_C_TRIGGER_OWN_THREAD=y
CONFIG_I2C=y

# --- PMIC ---
CONFIG_REGULATOR=y
CONFIG_MFD=y
CONFIG_SENSOR_NPM1300_CHARGER=y
CONFIG_ADC=y

# --- Power management ---
CONFIG_PM=y
CONFIG_PM_DEVICE=y

# --- Crypto (uniquement si chiffrement BTHome) ---
# CONFIG_PSA_WANT_ALG_CCM=y
# CONFIG_PSA_WANT_KEY_TYPE_AES=y
# CONFIG_MBEDTLS=y

# --- Divers ---
CONFIG_MAIN_STACK_SIZE=2048
CONFIG_LOG=y
CONFIG_LOG_DEFAULT_LEVEL=2
```

### 5.3 `app.overlay` — LDO1 à 3,3 V et initialisation différée de l'IMU

```dts
/*
 * XIAO nRF54LM20A Sense
 * LDO1 du nPM1300 alimente l'IMU (et le micro PDM) : 3,3 V requis.
 * La valeur par défaut de 1,8 V des définitions de carte standard
 * fait échouer le probe du LSM6DS3TR-C.
 */
&npm1300_ldo1 {
    regulator-min-microvolt = <3300000>;
    regulator-max-microvolt = <3300000>;
    regulator-initial-mode  = <NPM1300_LDSW_MODE_LDO>;
    regulator-boot-on;
};

&lsm6ds3tr_c {
    status = "okay";
    zephyr,deferred-init;      /* l'application alimente LDO1 puis appelle device_init() */
    irq-gpios = <&gpio1 12 GPIO_ACTIVE_HIGH>;   /* INT1 — à vérifier sur le DTS de carte */
};
```

Le mapping du GPIO INT1 dépend du fichier DTS fourni par Seeed / Zephyr. Le relever dans `boards/seeed/xiao_nrf54lm20a/` avant compilation.

Séquence d'initialisation dans l'application :

```c
const struct device *ldo1 = DEVICE_DT_GET(DT_NODELABEL(npm1300_ldo1));
const struct device *imu  = DEVICE_DT_GET(DT_NODELABEL(lsm6ds3tr_c));

regulator_enable(ldo1);
k_msleep(20);                  /* montée du rail + boot du LSM6DS3TR-C */
device_init(imu);              /* init différée */
```

### 5.4 Construction du payload BTHome

```c
#include <string.h>
#include <stdint.h>
#include <zephyr/bluetooth/bluetooth.h>

#define BTHOME_UUID_LO      0xD2
#define BTHOME_UUID_HI      0xFC
#define BTHOME_INFO_TRIG    0x44   /* v2, clair, trigger-based */

/* Object IDs BTHome v2 */
#define OBJ_PACKET_ID       0x00   /* uint8              */
#define OBJ_BATTERY         0x01   /* uint8   %          */
#define OBJ_TEMPERATURE     0x02   /* sint16  x0.01 °C   */
#define OBJ_VOLTAGE         0x0C   /* uint16  x0.001 V   */
#define OBJ_GENERIC_BOOL    0x0F   /* uint8   0/1        */
#define OBJ_BATTERY_LOW     0x15   /* uint8   0/1        */
#define OBJ_BATTERY_CHARGE  0x16   /* uint8   0/1        */
#define OBJ_MOTION          0x21   /* uint8   0/1        */
#define OBJ_TAMPER          0x2B   /* uint8   0/1        */
#define OBJ_VIBRATION       0x2C   /* uint8   0/1        */
#define OBJ_BUTTON          0x3A   /* uint8   event id   */
#define OBJ_ROTATION        0x3F   /* sint16  x0.1 °     */
#define OBJ_ACCELERATION    0x51   /* uint16  x0.001 m/s²*/
#define OBJ_GYROSCOPE       0x52   /* uint16  x0.001 °/s */

/* Compteur unique partagé par toutes les trames. */
static uint8_t bthome_pid;

struct bthome_buf {
    uint8_t data[24];   /* UUID(2) + info(1) + mesures(<=21) */
    uint8_t len;
};

static void bth_init(struct bthome_buf *b)
{
    b->data[0] = BTHOME_UUID_LO;
    b->data[1] = BTHOME_UUID_HI;
    b->data[2] = BTHOME_INFO_TRIG;
    b->len = 3;
}

static void bth_u8(struct bthome_buf *b, uint8_t id, uint8_t v)
{
    b->data[b->len++] = id;
    b->data[b->len++] = v;
}

static void bth_u16(struct bthome_buf *b, uint8_t id, uint16_t v)
{
    b->data[b->len++] = id;
    b->data[b->len++] = (uint8_t)(v & 0xFF);        /* little-endian */
    b->data[b->len++] = (uint8_t)(v >> 8);
}

static void bth_s16(struct bthome_buf *b, uint8_t id, int16_t v)
{
    bth_u16(b, id, (uint16_t)v);
}

/* --- TRAME A : événement + orientation (ordre croissant obligatoire) --- */
static void build_frame_a(struct bthome_buf *b,
                          bool activity, bool motion, bool freefall,
                          bool dbl_tap, uint8_t button_evt,
                          int16_t pitch_dd, int16_t roll_dd, int16_t yaw_dd)
{
    bth_init(b);
    bth_u8 (b, OBJ_PACKET_ID,    bthome_pid);
    bth_u8 (b, OBJ_GENERIC_BOOL, activity ? 1 : 0);
    bth_u8 (b, OBJ_MOTION,       motion   ? 1 : 0);
    bth_u8 (b, OBJ_TAMPER,       freefall ? 1 : 0);
    bth_u8 (b, OBJ_VIBRATION,    dbl_tap  ? 1 : 0);
    bth_u8 (b, OBJ_BUTTON,       button_evt);
    bth_s16(b, OBJ_ROTATION,     pitch_dd);   /* -> rotation    */
    bth_s16(b, OBJ_ROTATION,     roll_dd);    /* -> rotation_2  */
    bth_s16(b, OBJ_ROTATION,     yaw_dd);     /* -> rotation_3  */
    /* len = 3 + 21 = 24 */
}

/* --- TRAME B : périodique batterie / santé --- */
static void build_frame_b(struct bthome_buf *b,
                          uint8_t soc_pct, int16_t temp_cc,
                          uint16_t vbat_mv, bool low, bool charging)
{
    bth_init(b);
    bth_u8 (b, OBJ_PACKET_ID,       bthome_pid);
    bth_u8 (b, OBJ_BATTERY,         soc_pct);
    bth_s16(b, OBJ_TEMPERATURE,     temp_cc);
    bth_u16(b, OBJ_VOLTAGE,         vbat_mv);
    bth_u8 (b, OBJ_BATTERY_LOW,     low      ? 1 : 0);
    bth_u8 (b, OBJ_BATTERY_CHARGE,  charging ? 1 : 0);
    /* len = 3 + 14 = 17 */
}
```

### 5.5 Émission du train d'advertising

```c
#include <zephyr/bluetooth/hci.h>

/* 100 ms en unités de 0,625 ms */
#define ADV_INT_MIN  0x00A0
#define ADV_INT_MAX  0x00A0
#define TRAIN_MS     700

static const struct bt_le_adv_param adv_param = {
    .id           = BT_ID_DEFAULT,
    .options      = BT_LE_ADV_OPT_USE_IDENTITY,   /* pas de RPA : MAC stable */
    .interval_min = ADV_INT_MIN,
    .interval_max = ADV_INT_MAX,
    .peer         = NULL,
};

/* Envoie une trame BTHome, avec ou sans nom local. */
static int bthome_broadcast(const struct bthome_buf *b, bool with_name)
{
    struct bt_data ad[3];
    size_t n = 0;
    static const uint8_t flags = BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR;

    ad[n++] = (struct bt_data)BT_DATA(BT_DATA_FLAGS, &flags, 1);

    if (with_name) {
        ad[n++] = (struct bt_data)BT_DATA(BT_DATA_NAME_COMPLETE,
                                          CONFIG_BT_DEVICE_NAME,
                                          sizeof(CONFIG_BT_DEVICE_NAME) - 1);
    }

    ad[n++] = (struct bt_data)BT_DATA(BT_DATA_SVC_DATA16, b->data, b->len);

    int err = bt_le_adv_start(&adv_param, ad, n, NULL, 0);
    if (err) {
        return err;
    }

    k_msleep(TRAIN_MS);          /* packet id constant sur tout le train */
    return bt_le_adv_stop();
}

/* Le packet id n'est incrémenté qu'entre deux jeux de données distincts. */
static void bthome_next_packet(void) { bthome_pid++; }
```

Points de vigilance :

- `BT_LE_ADV_OPT_USE_IDENTITY` est indispensable : sans lui, Zephyr peut utiliser une adresse privée résolvable (RPA) tournante, ce qui créerait un nouveau device HA toutes les 15 minutes.
- Ne pas passer `BT_LE_ADV_OPT_CONNECTABLE` ni `BT_LE_ADV_OPT_SCANNABLE` : la PDU visée est `ADV_NONCONN_IND`.
- Ne pas activer `BT_LE_ADV_OPT_EXT_ADV`.
- `bt_le_adv_stop()` en fin de train est ce qui rend le montage économe : l'advertising ne tourne pas en continu.

### 5.6 Calcul pitch / roll

Avec accéléromètre seul, en statique — l'ouvrant à l'arrêt, ce qui correspond au moment où l'angle est pertinent :

```c
#include <math.h>

/* ax, ay, az en m/s² ; retour en dixièmes de degré (unité BTHome 0x3F). */
static void accel_to_angles(float ax, float ay, float az,
                            int16_t *pitch_dd, int16_t *roll_dd)
{
    float pitch = atan2f(-ax, sqrtf(ay * ay + az * az)) * (180.0f / (float)M_PI);
    float roll  = atan2f( ay, az)                       * (180.0f / (float)M_PI);

    *pitch_dd = (int16_t)lroundf(pitch * 10.0f);
    *roll_dd  = (int16_t)lroundf(roll  * 10.0f);
}
```

- Moyenner **8 à 16 échantillons** avant le calcul : le bruit de l'accéléromètre se traduit sinon par ±1° de gigue.
- Ne calculer l'angle qu'après stabilisation (‖a‖ ≈ 9,81 ± 0,3 m/s² pendant ≥ 200 ms), sinon l'accélération dynamique fausse l'estimation.
- Prévoir un **offset de calibration par capteur**, stocké en RRAM, réglable depuis HA (position fermée = 0°). Sur 25 ouvrants, aucun capteur ne sera posé parfaitement d'aplomb.
- Le **yaw** issu de l'intégration gyroscopique dérive : le remettre à zéro à chaque détection d'inactivité, ou ne pas l'exposer.

---

## 6. Chaîne de transmission — le BLE Proxy

```
XIAO nRF54LM20A ──BLE adv legacy 1M──▶ ESP32-S3 (ESPHome bluetooth_proxy)
                                              │ Wi-Fi / API ESPHome (advertisements bruts)
                                              ▼
                                    Home Assistant — intégration Bluetooth
                                              ▼
                                    bthome-ble (parseur) ──▶ intégration BTHome
                                              ▼
                                    Entités sensor / binary_sensor / event
```

### 6.1 Contrainte structurante : advertising legacy uniquement

**Les Bluetooth Proxies ESPHome ne reçoivent pas l'advertising étendu BLE 5.** ESPHome utilise l'API de scan *legacy* (`esp_ble_gap_start_scanning`). Même sur ESP32-S3, C3 ou C6, dont le contrôleur supporte matériellement l'extended scan, les paquets `ADV_EXT_IND` ne remontent pas. Le comportement est documenté (esphome/esphome#10626) et concerne également les adaptateurs BLE 5.0 côté hôte Home Assistant.

Conséquences pour le firmware :

- `CONFIG_BT_EXT_ADV=n`
- **31 octets maximum** — c'est ce qui impose le découpage en 3 trames
- **PHY LE 1M uniquement** : pas de LE Coded (longue portée), pas de LE 2M
- Le nRF54LM20A supporte BLE 6.0, mais cette capacité n'est pas exploitable ici. La limite est côté récepteur, pas côté capteur.

### 6.2 Configuration ESPHome — proxy dédié aux capteurs

```yaml
esphome:
  name: proxy-etage1
  friendly_name: Proxy BLE Étage 1

esp32:
  board: seeed_xiao_esp32s3
  framework:
    type: esp-idf        # requis : le framework arduino consomme trop de RAM
                         # et dégrade fortement les performances du proxy

logger:
  baud_rate: 0           # libère l'UART, réduit la charge CPU

api:
  encryption:
    key: !secret api_key

ota:
  - platform: esphome
    password: !secret ota_password

wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password
  power_save_mode: none  # évite les pertes d'advertisements pendant les sommeils Wi-Fi

esp32_ble_tracker:
  scan_parameters:
    active: false        # scan passif : pas de SCAN_REQ.
                         # Inutile ici (PDU non scannables) et réduit
                         # le bruit RF pour les autres capteurs du site.
    interval: 1000ms
    window: 900ms        # ~90 % de duty cycle. window = interval sature le Wi-Fi.
    continuous: true

bluetooth_proxy:
  active: false          # proxy dédié aux advertisements.
                         # Réserver active: true à 1 ou 2 proxies pour les
                         # connexions GATT (serrures, SwitchBot, etc.)
```

**Note de version.** Depuis **ESPHome 2026.8.0**, la couche d'advertisements BLE a été extraite dans un composant `ble_device_base` neutre : `bluetooth_proxy` n'est plus réservé à l'ESP32, et la clé `esp32_ble_id` est renommée **`ble_hub_id`** (l'ancien nom reste un alias déprécié jusqu'à 2027.2.0).

### 6.3 Dimensionnement du parc de proxies

| Contrainte | Valeur | Remarque |
|---|---|---|
| Capteurs passifs par proxy | 20 – 40 | Au-delà : `Too many BLE events to process` dans les logs |
| Connexions GATT actives par proxy | 3 par défaut, 9 max | Ne pas dépasser **5** pour la stabilité mémoire |
| Portée pratique intérieure | 8 – 15 m / 1 dalle | Béton et dalles armées : 1 proxy par niveau minimum |

Pour **25 capteurs répartis sur 4 niveaux**, **4 proxies (1 par étage)** sont cohérents. La redondance de couverture entre étages est un atout : Home Assistant sélectionne automatiquement la meilleure source par RSSI et déduplique via le Packet ID.

Pour les proxies critiques, privilégier une liaison **Ethernet** (ESP32 + LAN8720 ou W5500) plutôt que le Wi-Fi. BLE et Wi-Fi partagent la même radio 2,4 GHz sur l'ESP32 ; libérer la partie Wi-Fi augmente nettement le taux de capture d'advertisements et supprime la contrainte `window < interval`.

### 6.4 Limites de la chaîne proxy

| Attente | Réalité |
|---|---|
| Recevoir l'advertising étendu | Non (§6.1) |
| Recevoir la scan response en mode passif | Non — aucun SCAN_REQ n'est émis. Toute donnée placée en scan response est perdue. C'est pourquoi le nom est porté par l'advertising de la trame B. |
| Filtrer les advertisements avant transmission | Non : tout est relayé, le tri se fait dans Home Assistant |
| Décoder BTHome | Non : le décodage est réalisé par `bthome-ble` dans Home Assistant |

---

## 7. Intégration Home Assistant

### 7.1 Découverte

1. Intégration **Bluetooth** active, proxies ESPHome adoptés.
2. À la première réception d'une trame BTHome valide, le device apparaît en **Découvert** dans *Paramètres → Appareils et services*.
3. À défaut : *Ajouter une intégration → BTHome*.
4. Si le chiffrement est activé, Home Assistant demande le **bindkey** (32 caractères hexadécimaux).

### 7.2 Entités générées

| Trame | Object ID | Entité HA | Nom par défaut |
|---|---|---|---|
| A | `0x0F` | `binary_sensor` | Generic |
| A | `0x21` | `binary_sensor` | Motion |
| A | `0x2B` | `binary_sensor` | Tamper |
| A | `0x2C` | `binary_sensor` | Vibration |
| A | `0x3A` | `event` | Button |
| A | `0x3F` #1 | `sensor` | **Rotation** |
| A | `0x3F` #2 | `sensor` | **Rotation 2** |
| A | `0x3F` #3 | `sensor` | **Rotation 3** |
| B | `0x01` | `sensor` | Battery |
| B | `0x02` | `sensor` | Temperature |
| B | `0x0C` | `sensor` | Voltage |
| B | `0x15` | `binary_sensor` | Battery |
| B | `0x16` | `binary_sensor` | Battery charging |

Les entités **persistent entre les trames** : Home Assistant fusionne les mises à jour partielles, et une entité absente d'une trame conserve sa dernière valeur. Le découpage en 3 trames est donc transparent côté interface.

Renommage à effectuer à la mise en service de chaque capteur :

| Entité auto | Renommer en |
|---|---|
| Rotation | Pitch |
| Rotation 2 | Roll |
| Rotation 3 | Yaw |
| Generic | Activité |
| Tamper | Chute / Choc |
| Vibration | Double-tap |
| Temperature | Température interne *(catégorie : diagnostic)* |

### 7.3 Dériver l'état d'ouverture depuis l'angle

L'angle brut est peu exploitable directement en automatisation. Un template par ouvrant :

```yaml
# packages/ouvrants.yaml
template:
  - binary_sensor:
      - name: "Fenêtre Salon ouverte"
        unique_id: santuario_ouvrant_salon_open
        device_class: window
        state: >
          {{ (states('sensor.sant_01_pitch') | float(0)) | abs > 5 }}
        availability: >
          {{ states('sensor.sant_01_pitch') not in ['unknown','unavailable'] }}

  - sensor:
      - name: "Fenêtre Salon ouverture"
        unique_id: santuario_ouvrant_salon_angle
        unit_of_measurement: "°"
        state_class: measurement
        state: >
          {{ (states('sensor.sant_01_pitch') | float(0)) | abs | round(1) }}
```

Pour 25 capteurs, générer ce bloc depuis une boucle Jinja dans un package versionné (`HA-Santuario/config/packages/ouvrants.yaml`) plutôt qu'à la main.

### 7.4 Supervision de la chaîne

```yaml
template:
  - binary_sensor:
      - name: "SANT-01 silencieux"
        unique_id: santuario_sant01_stale
        device_class: problem
        delay_on: "01:30:00"      # > 1 h (heartbeat) + marge
        state: >
          {{ states('sensor.sant_01_battery') in ['unknown','unavailable'] }}
```

Le heartbeat horaire défini en §4.3 est ce qui rend cette supervision fiable : sans lui, il est impossible de distinguer « ouvrant immobile depuis 3 jours » de « capteur en panne ».

---

## 8. Procédure de mise en œuvre

### Phase 1 — Validation unitaire (1 capteur, 1 proxy)

1. Flasher le firmware avec **trame B uniquement**, en clair, toutes les **30 s** (cadence accélérée pour la mise au point).
2. Vérifier l'advertising avec **nRF Connect** (mobile) : présence de l'UUID `0xFCD2`, device info `0x44`, adresse **stable après reboot**.
3. Vérifier la découverte du device BTHome dans Home Assistant.
4. Redémarrer le capteur 3 fois : aucun nouveau device ne doit apparaître dans HA. Si un nouveau device apparaît à chaque boot, l'identité BLE n'est pas fixée (§5.1).
5. Passer la trame B à 15 min et confirmer que le device ne passe pas `unavailable` au bout de 5 min. Si c'est le cas, le bit trigger-based n'est pas positionné.

### Phase 2 — Trame A

6. Ajouter la trame A avec pitch/roll/yaw uniquement, déclenchée par un seuil angulaire.
7. Contrôler que `Rotation`, `Rotation 2` et `Rotation 3` correspondent bien à pitch, roll et yaw, et ne permutent jamais. Une permutation signale un ordre d'insertion variable dans le code.
8. Ajouter les binaires et l'événement bouton.
9. Vérifier explicitement le retour à `0` des binaires après le timeout d'inactivité.

### Phase 3 — Énergie

10. Mesurer la consommation moyenne sur 24 h (Nordic Power Profiler Kit II ou nPM1300 EK).
11. Ajuster les seuils IMU et le taux de rafraîchissement en fonction du résultat.
12. Valider l'écart entre le budget estimé (§4.4) et la mesure réelle avant de commander les 22 capteurs restants.

### Phase 4 — Déploiement sur 4 niveaux

13. Déployer 1 proxy par niveau, configuration §6.2.
14. Déployer les capteurs par lot de 5, avec un nom `SANT-XX` unique.
15. Vérifier le RSSI de chaque capteur dans HA (*Bluetooth → Advertisement Monitor*). Cible : **> −85 dBm** sur au moins un proxy.
16. Calibrer l'offset angulaire de chaque ouvrant en position fermée.
17. Générer les templates §7.3 et les pousser dans `thieryus007-cloud/HA-Santuario`.

### Phase 5 — Durcissement (optionnel)

18. Activer le chiffrement AES-CCM sur les ouvrants extérieurs, payload réduit (§3.4).
19. Mettre en place la supervision §7.4 sur les 25 capteurs.

---

## 9. Diagnostic

| Symptôme | Cause probable | Action |
|---|---|---|
| Device jamais découvert | Flags AD absents | Ajouter `02 01 06` — BlueZ ne parse pas sans, en scan passif |
| Device jamais découvert | Advertising étendu utilisé | `CONFIG_BT_EXT_ADV=n` |
| Découvert, mais aucune entité | Object IDs hors ordre croissant | Réordonner ; le parseur s'arrête au premier ID hors séquence |
| Mesures manquantes, toujours les mêmes en fin de trame | Un ID inconnu ou hors ordre plus tôt dans la trame | Réordonner ; les mesures postérieures sont perdues |
| Nouveau device HA à chaque reboot | Adresse BLE non fixée (RPA ou aléatoire par boot) | `bt_id_create()` + `BT_LE_ADV_OPT_USE_IDENTITY`, `CONFIG_BT_PRIVACY=n` |
| Device `unavailable` toutes les 5 min | Device info `0x40` au lieu de `0x44` | Bit 2 = 1 sur toutes les trames |
| État `unavailable` intermittent | Mélange `0x40` / `0x44` sur la même MAC | Uniformiser sur `0x44` |
| Motion reste `on` indéfiniment | Pas de trame de retour au repos | Émettre une trame A avec tous les binaires à `0` |
| Pitch / Roll / Yaw permutés | Ordre d'insertion variable selon le contexte | Insérer toujours les 3 `0x3F`, dans le même ordre |
| Accélération toujours positive ou valeurs absurdes | `0x51` est un uint16 | Envoyer une magnitude, ou utiliser `0x63` (sint32) |
| Gyroscope plafonne à 65,5 | Saturation `0x52` uint16 ×0.001 | Clamper, ou limiter le range gyro à ±125 dps |
| Événements manqués | Un seul adv event émis | Train de 700 ms, packet id constant (§4.2) |
| Événements manqués | `window` trop petit côté proxy | `window: 900ms` / `interval: 1000ms` |
| Doublons d'événements | Packet ID absent ou non incrémenté | Compteur unique, incrémenté à chaque nouveau jeu de données |
| `Too many BLE events to process` | Proxy saturé | Ajouter un proxy, ou passer en Ethernet |
| Proxy instable, Wi-Fi qui décroche | `window` = `interval` en Wi-Fi | Réduire à 90 % de duty cycle, ou passer en Ethernet |
| IMU non détectée / WHO_AM_I invalide | LDO1 du nPM1300 à 1,8 V | Overlay §5.3 : 3,3 V + `zephyr,deferred-init` |
| Angles bruités ±1° | Pas de moyennage | 8 à 16 échantillons + attente de stabilité (§5.6) |
| Tous les capteurs émettent en même temps | Pas de jitter sur la trame B | ±30 s d'aléa (§4.3) |

---

## 10. Décisions retenues — récapitulatif

| Sujet | Décision |
|---|---|
| Protocole | BLE advertising **legacy**, BTHome v2, UUID `0xFCD2` |
| Device info | **`0x44`** (v2, clair, trigger-based) sur toutes les trames |
| Nombre de trames | **3** — A (événement + angles), B (batterie/santé + nom, 15 min), C (IMU brut, optionnelle) |
| Packet ID | Compteur unique partagé, constant sur un train, incrémenté entre deux jeux de données |
| Angles | `0x3F` rotation ×3 (sint16, 0.1°) — pitch, roll, yaw dans cet ordre |
| Accéléro / gyro bruts | Optionnels et déconseillés. Si conservés : magnitudes uniquement (`0x51` / `0x52`, non signés) |
| Adresse BLE | Statique aléatoire dérivée du FICR, `BT_LE_ADV_OPT_USE_IDENTITY` |
| Régime d'alimentation | System ON + PM Zephyr, réveil sur INT1 du LSM6DS3TR-C |
| Train d'advertising | 100 ms d'intervalle, 700 ms de durée, PHY 1M |
| Chiffrement | Optionnel — 8 octets de surcoût, payload réduit à 15 o. sans nom |
| Proxies | 4 × ESP32-S3 ESPHome, esp-idf, scan passif, `bluetooth_proxy: active: false` |
| Microphone PDM | Exclu du périmètre |

---

## 11. Points à valider sur banc

Trois éléments dépendent de mesures ou de relevés sur la carte réelle :

1. **Le GPIO d'INT1 du LSM6DS3TR-C** — à relever dans le DTS de carte Seeed/Zephyr (`boards/seeed/xiao_nrf54lm20a/`) avant compilation.
2. **La consommation réelle du LSM6DS3TR-C et du nPM1300** en régime de veille — à mesurer au PPK II. La fourchette §4.4 (25–80 µA) est une estimation.
3. **La persistance du nom local** appris via la trame B côté Home Assistant. Si le nom ne s'applique pas au device, le renommer une fois dans l'interface — l'impact est cosmétique, et le renommage est de toute façon prévu en phase 4.

Le reste de la spécification (Object IDs, tailles, facteurs, signes, ordre, budgets d'octets, flags, contraintes proxy) est établi à partir des sources normatives, avec des budgets calculés octet par octet.

---

## Sources

- BTHome v2 — spécification officielle : `https://bthome.io/format/`
- Home Assistant, intégration BTHome : `https://www.home-assistant.io/integrations/bthome/`
- Seeed Studio Wiki — XIAO nRF54LM20A Sense (getting started, BLE, low power, nRF Connect SDK)
- Zephyr Project — board `seeed/xiao_nrf54lm20a`
- ESPHome — `bluetooth_proxy`, `esp32_ble_tracker`, changelog 2026.8.0
- esphome/esphome#10626 — Bluetooth Proxy et advertising étendu
- home-assistant/core#78702 / #79669 — `UNAVAILABLE_TRACK_SECONDS` et devices « sleepy »
