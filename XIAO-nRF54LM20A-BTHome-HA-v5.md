# XIAO nRF54LM20A Sense → BLE Proxy → Home Assistant via BTHome v2

**Spécification technique de conception et de mise en œuvre**

Transmettre les données capteur de la carte Seeed XIAO nRF54LM20A Sense vers Home Assistant, en BLE advertising au format BTHome v2, relayé par un Bluetooth Proxy, avec une consommation optimisée pour un fonctionnement sur batterie de plusieurs années.

**Périmètre** : IMU (accéléromètre, gyroscope, événements, orientation), batterie, température interne, bouton. Transmission en clair.

Le document décrit **deux profils d'émission** :

- **Profil L — Legacy**, 31 octets, 3 trames. Profil de production, opérationnel avec le parc de proxies ESPHome existant. Sections §3 à §5.
- **Profil E — Étendu**, trame unique, jusqu'à 255 octets. Banc de test sur 1 XIAO et 1 proxy **ESP32-U** dédiés. Section §6.

**Méthode de vérification.** La table des Object IDs, les formats, longueurs, facteurs et le comportement du décodage ont été extraits du code de **`bthome-ble` version 3.9.2**, la bibliothèque exécutée par l'intégration BTHome de Home Assistant. Le comportement du Bluetooth Proxy est établi à partir du dépôt ESPHome. Les points à confirmer sur banc sont regroupés en §14.

---

## 1. Matériel — Seeed XIAO nRF54LM20A Sense

| Élément | Valeur |
|---|---|
| SoC | Nordic **nRF54LM20A**, Cortex-M33 @128 MHz + coprocesseur RISC-V FLPR 128 MHz |
| Mémoire | 512 KB RAM, ~1,5 MB RRAM (2 MB NVM annoncés) |
| Flash externe | PY25Q64HA, 64 Mbit (8 MB), SPI |
| Radio | **Bluetooth LE 6.0** (+ Channel Sounding), Matter, Thread, Zigbee, 2,4 GHz propriétaire, NFC |
| IMU | **ST LSM6DS3TR-C** — accéléromètre + gyroscope 3 axes, fonctions embarquées (wake-up, free-fall, tap/double-tap, activity/inactivity, 6D/4D orientation, tilt, significant motion), capteur de température interne |
| Micro | MSM261DGT006 (PDM) |
| PMIC | Nordic **nPM1300** (charge Li-Po, LDO/buck, mesure VBAT, NTC, ship mode) |
| Consommation | System OFF **~4,76 µA**, Ship Mode **0,33 µA** (mesures Seeed, batterie 3,7 V) |
| Toolchain | **nRF Connect SDK v3.3.0** ou PlatformIO — Zephyr RTOS |

### 1.1 Alimentation de l'IMU — LDO1 du nPM1300

Sur les variantes **Sense**, l'IMU et le microphone PDM sont alimentés par le **LDO1 du nPM1300**, réglé à **1,8 V** dans les définitions de carte Zephyr standard. Les deux périphériques fonctionnent à **3,3 V** : l'overlay §7.4 porte LDO1 à cette tension et garantit que le rail est établi avant que le driver ne sonde le bus.

### 1.2 Grandeurs de température disponibles

Deux sources de température existent à bord : le die du SoC, et `OUT_TEMP` du LSM6DS3TR-C (résolution 1/256 °C). Ce sont des **températures internes de la carte**, utiles au diagnostic thermique du nœud. Les publier en `entity_category: diagnostic` côté Home Assistant.

Pour une **température ambiante de pièce**, ajouter un SHT4x ou un BME280 sur le bus I²C : le nœud remonte alors une mesure d'ambiance en plus de ses grandeurs internes.

### 1.3 Mesure d'angle

L'accéléromètre fournit **pitch et roll absolus** par projection du vecteur gravité : ces deux angles sont stables dans le temps et se recalent en permanence sur la verticale. Le yaw est obtenu par intégration gyroscopique et se recale à chaque détection d'inactivité.

Pour une application d'angle d'ouvrant, monter le capteur de sorte que l'**axe de rotation de l'ouvrant soit perpendiculaire à la gravité**. L'angle d'ouverture devient alors directement un pitch ou un roll — mesure absolue, stable, sans dérive, sans recalage.

---

## 2. Format BTHome v2

### 2.1 Structure de la trame BLE

```
[ AD Flags ] [ AD Local Name ] [ AD Service Data 16-bit UUID ]
```

| Élément | Octets | Contenu |
|---|---|---|
| AD Flags | 3 | `02 01 06` — requis. BlueZ, utilisé par l'intégration Bluetooth de HA, s'appuie sur ce champ pour parser l'advertising en scan passif. |
| AD Local Name | 2 + N | `LL 09 <ascii>` (complet) ou `LL 08 <ascii>` (abrégé) |
| AD Service Data | 4 + M | `LL 16 D2 FC <device_info> <mesures…>` — UUID `0xFCD2` en little-endian |

| Profil | Charge utile totale |
|---|---|
| **L — Legacy** | **31 octets** |
| **E — Étendu** | **255 octets** par PDU |

### 2.2 Octet Device Info

| Bit | Signification | Valeur |
|---|---|---|
| 0 | Chiffrement | **0** — transmission en clair |
| 1 | MAC incluse dans le payload | **0** — l'adresse est portée par l'en-tête BLE |
| 2 | **Trigger-based device** | **1** |
| 3-4 | Réservé | 0 |
| 5-7 | Version BTHome | `010` = v2 |

| Valeur | Sens | Emploi |
|---|---|---|
| `0x40` | v2, clair, périodique | — |
| **`0x44`** | **v2, clair, trigger-based** | **Toutes les trames, profils L et E** |

**Pourquoi `0x44` partout.** Le parseur exécute `self.sleepy_device = bthome_data.is_sleepy_device()` à **chaque** advertising reçu : l'indicateur « sleepy » reflète donc la **dernière trame reçue**. Une valeur constante `0x44` maintient l'indicateur stable, et Home Assistant conserve le device disponible entre deux émissions espacées de 15 minutes.

### 2.3 Comportement du décodage — trois règles relevées dans `parser.py`

**a) Object ID inconnu → arrêt du décodage.**
```python
if obj_meas_type not in MEAS_TYPES:
    _LOGGER.debug("Invalid Object ID found in payload: %s", ...)
    break
```
Les mesures situées après un ID inconnu ne sont pas décodées, et le message est émis en niveau `debug` seulement. N'employer que des Object IDs de la table §2.5.

**b) Object IDs hors ordre croissant → avertissement, le décodage se poursuit.**
```python
if prev_obj_meas_type > obj_meas_type:
    _LOGGER.warning("BTHome device is not sending object ids in numerical order ...")
```
L'ordre croissant est **exigé par la spécification** et doit être respecté : d'autres récepteurs BTHome s'appuient dessus pour interrompre le décodage. Côté Home Assistant, l'effet immédiat se limite à une ligne de journal.

**c) Déduplication par Packet ID — règle exacte.**

| Condition | Résultat |
|---|---|
| Premier paquet reçu | Accepté |
| Plus de **4 s** depuis le dernier advertising | **Accepté**, même packet id identique |
| Écart croissant < 64 (rollover 255→0 géré) | Accepté |
| Packet id identique, ou plus ancien, à moins de 4 s | Écarté |

Le code pose l'hypothèse d'un capteur émettant au plus 16 jeux de données par seconde. Deux conséquences :

- **Le train de répétitions à packet id constant fonctionne comme voulu** : les copies sont écartées, c'est le principe de la redondance de réception.
- **Deux jeux de données distincts espacés de moins de 4 s portent des packet id différents** (§4.3).

### 2.4 Autres règles du format

1. **Little-endian** pour toutes les valeurs multi-octets.
2. **Mesures multiples du même type** : suffixées côté HA (`rotation`, `rotation_2`, `rotation_3`) **dans l'ordre de la trame**. Cet ordre est identique à chaque advertising.
3. **Binary sensors** : 1 octet, `0x00` / `0x01`.
4. **Aucune borne de longueur de payload** dans le parseur : la boucle de décodage itère sur `len(payload)`. C'est ce qui rend le profil E décodable sans adaptation côté Home Assistant.

### 2.5 Object IDs — table extraite de `bthome-ble` 3.9.2

**Capteurs**

| ID | Grandeur | Format | Taille | Facteur | Unité |
|---|---|---|---|---|---|
| `0x00` | packet id | non signé | 1 | 1 | — |
| `0x01` | battery | non signé | 1 | 1 | % |
| `0x02` | temperature | **signé** | 2 | 0.01 | °C |
| `0x0C` | voltage | non signé | 2 | 0.001 | V |
| `0x3D` | count | non signé | 2 | 1 | — |
| `0x3F` | **rotation** | **signé** | 2 | 0.1 | ° |
| `0x45` | temperature | signé | 2 | 0.1 | °C |
| `0x4A` | voltage | non signé | 2 | 0.1 | V |
| `0x50` | timestamp | epoch | 4 | 1 | s |
| `0x51` | acceleration | non signé | 2 | 0.001 | m/s² |
| `0x52` | gyroscope | non signé | 2 | 0.001 | °/s |
| `0x57` | temperature | signé | 1 | 1 | °C |
| `0x5A` | count | signé | 2 | 1 | — |
| `0x5B` | count | signé | 4 | 1 | — |
| `0x62` | speed | signé | 4 | 0.000001 | m/s |
| `0x63` | **acceleration (signée)** | signé | 4 | 0.000001 | m/s² |
| `0x64` | light level | non signé | 1 | 1 | — |
| `0x65` | settings revision | non signé | 1 | 1 | — |
| `0xF2` | firmware version | 3 octets | 3 | — | x.y.z |

**Plages utiles** (énumération complète de `MEAS_TYPES`) :

- **Angle** — `0x3F`, signé, ±3276,7 ° : couvre toute course d'ouvrant.
- **Accélération** — `0x51` porte une magnitude sur 0 → 65,535 m/s². `0x63` couvre ±2147,48 m/s² sur 4 octets pour des composantes signées.
- **Vitesse angulaire** — `0x52` porte une magnitude sur 0 → 65,535 °/s. Configurer le gyroscope en ±125 dps ou clamper la valeur maintient la mesure dans cette plage.

**Capteurs binaires** — 1 octet chacun

| ID | Grandeur | 0 / 1 |
|---|---|---|
| `0x0F` | generic | Off / On |
| `0x11` | opening | Closed / Open |
| `0x15` | battery (low) | Normal / Low |
| `0x16` | **battery charging** | Repos / En charge |
| `0x1A` | door | Closed / Open |
| `0x21` | motion | Clear / Detected |
| `0x22` | moving | Immobile / En mouvement |
| `0x2B` | tamper | Off / On |
| `0x2C` | vibration | Clear / Detected |
| `0x2D` | window | Closed / Open |

**Événements**

| ID | Type | Valeurs |
|---|---|---|
| `0x3A` | button | `0x00` none · `0x01` press · `0x02` double_press · `0x03` triple_press · `0x04` long_press · `0x05` long_double_press · `0x06` long_triple_press · `0x80` hold_press |
| `0x3C` | dimmer | 2 octets |

### 2.6 Correspondances retenues pour les événements IMU

| Événement LSM6DS3TR-C | Object ID | Entité HA | Renommage |
|---|---|---|---|
| Wake-up / motion | `0x21` motion | binary_sensor | *Mouvement* |
| Activity / Inactivity | `0x0F` generic | binary_sensor | *Activité* |
| Free-fall / choc | `0x2B` tamper | binary_sensor | *Chute / Choc* |
| Double-tap | `0x2C` vibration | binary_sensor | *Double-tap* |
| Bouton | `0x3A` button | event | *Bouton* |
| Pitch / Roll / Yaw | `0x3F` ×3 | sensor ×3 | *Pitch* / *Roll* / *Yaw* |

**Remise à zéro des binaires.** BTHome transmet des états, que le récepteur conserve jusqu'à la valeur suivante. Le capteur émet donc une trame de retour au repos, tous les binaires à `0`, après le timeout d'événement (5 à 30 s).

---

## 3. Profil L — Architecture de trames Legacy (production)

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

Émise sur événement : wake-up IMU, franchissement de seuil angulaire, appui bouton, retour au repos.

| Ordre | Object ID | Donnée | Octets |
|---|---|---|---|
| 1 | `0x00` | Packet ID | 2 |
| 2 | `0x0F` | Activity | 2 |
| 3 | `0x21` | Motion | 2 |
| 4 | `0x2B` | Free-fall / choc | 2 |
| 5 | `0x2C` | Double-tap | 2 |
| 6 | `0x3A` | Bouton | 2 |
| 7 | `0x3F` | **Pitch** (signé, 0.1°) | 3 |
| 8 | `0x3F` | **Roll** | 3 |
| 9 | `0x3F` | **Yaw** | 3 |
| | | **Total mesures** | **21** |

**Budget : 3 (flags) + 26 (service data) = 29 / 31 octets.**

Exemple — pitch +12,5° · roll −3,2° · yaw +90,0° · motion actif · packet id 42 :

```
02 01 06                          ← Flags
19 16 D2 FC 44                    ← len=25, service data, UUID FCD2, device info 0x44
   00 2A                          ← packet id = 42
   0F 01                          ← activity = 1
   21 01                          ← motion = 1
   2B 00                          ← tamper = 0
   2C 00                          ← vibration = 0
   3A 00                          ← button = none
   3F 7D 00                       ← rotation   = 0x007D = 125 → +12.5 °
   3F E0 FF                       ← rotation_2 = 0xFFE0 = -32 → -3.2 °
   3F 84 03                       ← rotation_3 = 0x0384 = 900 → +90.0 °
```

### 3.2 TRAME B — Périodique (batterie / santé), 15 min

Porte également le **nom de l'appareil**.

| Ordre | Object ID | Donnée | Octets |
|---|---|---|---|
| 1 | `0x00` | Packet ID | 2 |
| 2 | `0x01` | Battery level (%) | 2 |
| 3 | `0x02` | Température interne | 3 |
| 4 | `0x0C` | Tension batterie | 3 |
| 5 | `0x15` | Battery low | 2 |
| 6 | `0x16` | Battery charging | 2 |
| | | **Total mesures** | **14** |

**Budget : 3 (flags) + 9 (nom, 7 caractères) + 19 (service data) = 31 / 31 octets — limite exacte.**

Exemple — nom `SANT-01`, 87 %, 21,35 °C, 3,905 V, packet id 43 :

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

Le nom tient sur **7 caractères** dans cette configuration. Pour un nom plus long, retirer `0x15` — l'alerte batterie faible se dérive côté HA d'un seuil sur `0x01` — ce qui libère 2 caractères.

### 3.3 TRAME C — IMU brut (optionnelle)

L'information exploitable en automatisation — angles et événements — est calculée à bord et portée par la trame A. La trame C répond à un besoin de télémétrie brute, typiquement pour caractériser un ouvrant en phase de mise au point.

Deux points de format (§2.5) : `0x51` et `0x52` portent des **magnitudes** ; `0x52` couvre 0 → 65,535 °/s, donc gyroscope en ±125 dps ou valeur clampée.

| Ordre | Object ID | Donnée | Octets |
|---|---|---|---|
| 1 | `0x00` | Packet ID | 2 |
| 2–4 | `0x51` ×3 | \|a_x\|, \|a_y\|, \|a_z\| | 9 |
| 5–7 | `0x52` ×3 | \|ω_x\|, \|ω_y\|, \|ω_z\| | 9 |
| | | **Total mesures** | **20** |

**Budget : 3 + 25 = 28 / 31 octets.**

**Variante recommandée** : porter **‖a‖** et **‖ω‖** seules (`0x51` ×1, `0x52` ×1), soit 6 octets de mesures. 14 octets restent disponibles pour le nom, et les deux grandeurs sont directement interprétables.

---

## 4. Stratégie d'émission et politique de réveil

### 4.1 Événements déclencheurs (trame A)

| Déclencheur | Source | Mise en œuvre |
|---|---|---|
| Réveil sur mouvement | LSM6DS3TR-C `WAKE_UP` → INT1 | Seuil `WK_THS`, durée `WAKE_DUR` |
| Franchissement angulaire | Calcul embarqué | Hystérésis ±2° |
| Free-fall / choc | LSM6DS3TR-C `FREE_FALL` → INT1 | — |
| Double-tap | LSM6DS3TR-C `TAP_SRC` → INT1 | `TAP_CFG` / `INT_DUR2` |
| Inactivity | LSM6DS3TR-C `SLEEP_CHANGE` | Remet tous les binaires à 0 |
| Bouton | GPIO | Anti-rebond ≥ 30 ms |

### 4.2 Train d'advertising

Le proxy écoute un canal primaire à la fois et alterne à chaque intervalle de scan. Un train de répétitions maximise la probabilité de réception.

| Paramètre | Valeur | Justification |
|---|---|---|
| Intervalle d'advertising | **100 ms** | Chaque event balaie les 3 canaux primaires |
| Durée du train | **700 ms** | ≈ 7 events, packet id **constant** sur le train |
| Type de PDU | `ADV_NONCONN_IND` | Émission pure, sans scan request ni connexion |
| PHY | **LE 1M** | PHY scanné par les proxies ESPHome |

Coût radio : ~7 events × 3 canaux × ~1 ms ≈ **21 ms de TX**, de l'ordre de **0,1 mAs** par événement à 0 dBm.

### 4.3 Anti-rebond et limitation de débit

| Règle | Valeur |
|---|---|
| Intervalle minimum entre deux trames A | **4 s** |
| Trames A par minute | **10 maximum** (fenêtre glissante) |
| Envoi angulaire si Δ > | **2,0 °** sur pitch ou roll |
| Trame « repos » après inactivité | **15 s** |
| Trame B | **15 min** ± 30 s de jitter aléatoire |
| Heartbeat (trame A état repos) | **60 min** en l'absence d'événement |

L'intervalle minimum est fixé à **4 s** pour s'aligner sur la fenêtre de déduplication du parseur (§2.3c) : au-delà de 4 s, chaque jeu de données est accepté indépendamment de son packet id, ce qui rend le comportement déterministe et réduit la charge radio.

Le **jitter** sur la trame B compte avec 25 capteurs : il désynchronise les nœuds redémarrés en même temps après une coupure.

### 4.4 Bilan énergétique prévisionnel

| Poste | Courant moyen estimé |
|---|---|
| nRF54LM20A, System ON idle (GRTC actif) | 5 – 10 µA |
| LSM6DS3TR-C, accéléromètre seul low-power @26 Hz | 10 – 50 µA *(à mesurer)* |
| nPM1300, courant de repos | 5 – 15 µA *(à mesurer)* |
| Radio (trames A+B moyennées) | < 5 µA |
| **Total estimé** | **~25 – 80 µA** |

Sur une LiPo 1000 mAh : **1,5 à 4 ans** théoriques, à réduire d'environ 30 % pour l'autodécharge et le froid. Sur un ouvrant très sollicité, viser **1 an**.

**Régime retenu : System ON + Power Management Zephyr.** Ce régime conserve la RAM, donc le compteur Packet ID, la calibration d'offset et le contexte BLE d'un réveil à l'autre, pour un surcoût d'environ 5 µA face à System OFF — marginal à côté de l'IMU, qui reste alimentée en permanence pour la détection de mouvement. System OFF et Ship Mode servent au stockage et au transport.

---

## 5. Chaîne de transmission — proxies de production

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

### 5.1 Configuration ESPHome — proxy dédié aux capteurs

```yaml
esphome:
  name: proxy-etage1
  friendly_name: Proxy BLE Étage 1

esp32:
  board: seeed_xiao_esp32s3
  framework:
    type: esp-idf        # framework recommandé pour le proxy :
                         # empreinte mémoire réduite et meilleures performances

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
  power_save_mode: none  # maintient la réception pendant les cycles Wi-Fi

esp32_ble_tracker:
  scan_parameters:
    active: false        # scan passif, adapté aux PDU non scannables,
                         # et économe en occupation RF pour le reste du site
    interval: 1000ms
    window: 900ms

bluetooth_proxy:
  active: false          # proxy dédié aux advertisements.
                         # Réserver active: true à 1 ou 2 proxies pour
                         # les connexions GATT (serrures, SwitchBot, etc.)
```

**Notes de version — ESPHome 2026.8.0 :**

- La couche d'advertisements BLE est extraite dans un composant neutre `ble_device_base` ; `bluetooth_proxy` s'exécute désormais sur d'autres familles de puces que l'ESP32.
- La clé `esp32_ble_id` devient **`ble_hub_id`** (l'ancien nom reste un alias déprécié jusqu'à 2027.2.0).
- **Le défaut de `window` a changé** : avec la coexistence Wi-Fi sur ESP-IDF 5.5.5 ou plus récent, `window` prend la valeur de `interval`, conformément à la recommandation d'Espressif. Un `window` explicite est conservé tel quel. La valeur `900ms` ci-dessus est un choix délibéré (90 % de rapport cyclique) ; laisser le champ vide donne 100 %.

### 5.2 Dimensionnement du parc

| Contrainte | Valeur | Remarque |
|---|---|---|
| Capteurs passifs par proxy | 20 – 40 | Le message `Too many BLE events to process` signale l'atteinte du plafond |
| Connexions GATT par proxy | 3 par défaut, 9 max | Rester à **5 maximum** pour la stabilité mémoire |
| Portée pratique intérieure | 8 – 15 m / 1 dalle | Béton armé : 1 proxy par niveau |

Pour **25 capteurs sur 4 niveaux**, **4 proxies (1 par étage)** sont cohérents. La redondance de couverture entre étages est un atout : Home Assistant retient la meilleure source par RSSI et déduplique via le Packet ID.

Pour les proxies critiques, privilégier une liaison **Ethernet**. BLE et Wi-Fi partagent la radio 2,4 GHz de l'ESP32 ; libérer la partie Wi-Fi augmente le taux de capture d'advertisements et laisse le champ libre sur le paramètre `window`.

### 5.3 Répartition des rôles

| Fonction | Assurée par |
|---|---|
| Réception des advertisements | Proxy (ESPHome ou SLZB-OS) |
| Transport vers Home Assistant | API ESPHome / intégration SMLIGHT, sur Wi-Fi ou Ethernet |
| Sélection de la meilleure source par RSSI, déduplication | Intégration Bluetooth de Home Assistant |
| Décodage BTHome, création des entités | `bthome-ble` + intégration BTHome |
| Filtrage métier, seuils, automatisations | Templates et automatisations HA (§9.3) |

Le nom de l'appareil voyage dans l'advertising de la trame B (§3.2), là où le scan passif le capte avec le reste de la trame.

### 5.4 Alternative matérielle — SMLIGHT SLZB série U

Depuis **Home Assistant 2026.8**, les SLZB série U sous SLZB-OS servent d'adaptateur Bluetooth distant **nativement**, tout en restant coordinateur Zigbee ou Thread Border Router, sur Ethernet. Le mode de scan se choisit dans les options de l'intégration SMLIGHT, sans reflashage en ESPHome.

Deux points à confirmer : la fonction est attachée à la série SLZB U — vérifier la référence exacte des unités SMLIGHT déployées sur le site ; et ces appareils relaient les advertisements, les connexions GATT actives restant du ressort des proxies ESPHome configurés en `active: true`.

---

## 6. Profil E — Banc de test Publicité Étendue

### 6.1 Objectif et matériel dédié

Valider une chaîne **XIAO nRF54LM20A → ESP32-U → Home Assistant** en Publicité Étendue, avec une **trame unique portant l'intégralité des données**. Le banc est isolé de la production : il mobilise **1 XIAO** et **1 proxy ESP32-U**, le parc des 4 proxies de production restant en profil L.

| Rôle | Matériel | Firmware |
|---|---|---|
| Émetteur de test | 1 × XIAO nRF54LM20A Sense | Zephyr / NCS, `CONFIG_BT_EXT_ADV=y` (§6.4) |
| Récepteur de test | 1 × **ESP32-U** | Étage 1 : sketch BLE5 extended scan · Étage 2 : ESPHome + composant externe (§6.5) |
| Témoin de portée | Le même XIAO en profil L | Firmware de production |
| Analyseur | Smartphone + **nRF Connect** | — |

### 6.2 Ce qui est acquis, maillon par maillon

| Maillon | Publicité Étendue | Base |
|---|---|---|
| **nRF54LM20A + Zephyr / NCS** | **Disponible** | SoC BLE 6.0 ; Zephyr expose `bt_le_ext_adv_create()` / `bt_le_ext_adv_start()` sous `CONFIG_BT_EXT_ADV`. |
| **Protocole BTHome v2** | **Compatible** | Enchaînement d'objets sans borne de longueur dans la spécification. |
| **Parseur `bthome-ble` 3.9.2** | **Compatible** | Vérifié dans le code : boucle de décodage sur `len(payload)`, sans borne. Un payload de plusieurs centaines d'octets est décodé normalement. |
| **Contrôleur BLE de l'ESP32-U** | **Disponible** | Les familles ESP32-S3 / C3 / C6 exposent l'extended scan sous `CONFIG_BT_BLE_50_FEATURES_SUPPORTED`. L'exemple `BLE5_extended_scan` d'arduino-esp32 s'appuie sur cette API. |
| **Composant `esp32_ble_tracker` d'ESPHome** | **Scan legacy** | Vérifié sur le dépôt ESPHome : la PR #18047 documente un `grep` de l'arbre montrant que le proxy utilise l'API de scan legacy, et fixe `CONFIG_BT_BLE_50_FEATURES_SUPPORTED` à l'état désactivé dans la configuration IDF. |

**Conséquence pour le banc** : le lien radio et le décodage sont acquis de bout en bout. L'élément à fournir est le **chemin de code d'extended scan côté ESP32-U**, d'où le découpage en deux étages.

### 6.3 Trame unique — profil E

Toutes les données du périmètre dans un seul advertising, Object IDs en ordre croissant.

| Ordre | Object ID | Donnée | Octets |
|---|---|---|---|
| 1 | `0x00` | Packet ID | 2 |
| 2 | `0x01` | Battery level (%) | 2 |
| 3 | `0x02` | Température interne (0.01 °C) | 3 |
| 4 | `0x0C` | Tension batterie (0.001 V) | 3 |
| 5 | `0x0F` | Activity | 2 |
| 6 | `0x15` | Battery low | 2 |
| 7 | `0x16` | Battery charging | 2 |
| 8 | `0x21` | Motion | 2 |
| 9 | `0x22` | Moving | 2 |
| 10 | `0x2B` | Free-fall / choc | 2 |
| 11 | `0x2C` | Double-tap | 2 |
| 12 | `0x3A` | Bouton | 2 |
| 13–15 | `0x3F` ×3 | Pitch, Roll, Yaw (0.1°) | 9 |
| 16–18 | `0x51` ×3 | \|a_x\|, \|a_y\|, \|a_z\| | 9 |
| 19–21 | `0x52` ×3 | \|ω_x\|, \|ω_y\|, \|ω_z\| | 9 |
| 22 | `0xF2` | Version firmware | 4 |
| | | **Total mesures** | **57** |

| Élément | Octets |
|---|---|
| AD Flags | 3 |
| AD Local Name `SANT-01` | 9 |
| AD Service Data (len + type + UUID + device info + 57) | 62 |
| **Total** | **74 / 255 octets** |

Il reste **181 octets** de marge : de quoi ajouter un capteur d'ambiance I²C (§1.2), des compteurs d'événements, un horodatage `0x50`, ou passer les accélérations en `0x63` signé sur 4 octets.

### 6.4 Firmware émetteur — Zephyr, advertising étendu

Différences avec §7.6 (profil L) : un jeu d'advertising explicite, et le drapeau `BT_LE_ADV_OPT_EXT_ADV`.

```conf
# prj.conf — surcharges du profil E
CONFIG_BT_EXT_ADV=y
CONFIG_BT_EXT_ADV_MAX_ADV_SET=1
CONFIG_BT_CTLR_ADV_EXT=y
CONFIG_BT_CTLR_ADV_DATA_LEN_MAX=255
```

```c
#include <zephyr/bluetooth/bluetooth.h>

#define ADV_INT      0x00A0   /* 100 ms, unités de 0,625 ms */
#define TRAIN_MS     700

static struct bt_le_ext_adv *adv_set;

/* Étendu, non connectable, non scannable, adresse d'identité. */
static const struct bt_le_adv_param ext_param =
    BT_LE_ADV_PARAM_INIT(BT_LE_ADV_OPT_EXT_ADV | BT_LE_ADV_OPT_USE_IDENTITY,
                         ADV_INT, ADV_INT, NULL);

int bthome_ext_init(void)
{
    return bt_le_ext_adv_create(&ext_param, NULL, &adv_set);
}

/* b->data contient UUID(2) + device info(1) + mesures(57) = 60 octets */
int bthome_ext_broadcast(const struct bthome_buf *b)
{
    static const uint8_t flags = BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR;
    struct bt_data ad[3];
    int err;

    ad[0] = (struct bt_data)BT_DATA(BT_DATA_FLAGS, &flags, 1);
    ad[1] = (struct bt_data)BT_DATA(BT_DATA_NAME_COMPLETE,
                                    CONFIG_BT_DEVICE_NAME,
                                    sizeof(CONFIG_BT_DEVICE_NAME) - 1);
    ad[2] = (struct bt_data)BT_DATA(BT_DATA_SVC_DATA16, b->data, b->len);

    err = bt_le_ext_adv_set_data(adv_set, ad, 3, NULL, 0);
    if (err) {
        return err;
    }

    err = bt_le_ext_adv_start(adv_set, BT_LE_EXT_ADV_START_DEFAULT);
    if (err) {
        return err;
    }

    k_msleep(TRAIN_MS);
    return bt_le_ext_adv_stop(adv_set);
}
```

Le buffer `bthome_buf` du profil L passe de 24 à 64 octets pour accueillir les 57 octets de mesures :

```c
struct bthome_buf {
    uint8_t data[64];   /* UUID(2) + info(1) + mesures(<=57) */
    uint8_t len;
};
```

Les fonctions `bth_init()`, `bth_u8()`, `bth_u16()` et `bth_s16()` de §7.5 sont réutilisées telles quelles.

**PHY.** Laisser le PHY primaire en LE 1M pour le premier essai : l'annonce reste captable par un scanner legacy, ce qui simplifie le diagnostic. Le PHY secondaire (LE 2M) se règle ensuite via les paramètres du jeu d'advertising, une fois l'étage 1 validé.

### 6.5 Récepteur ESP32-U — deux étages

**Étage 1 — Preuve du lien radio.**

Flasher l'ESP32-U avec l'exemple **`BLE5_extended_scan`** d'arduino-esp32, ou son équivalent ESP-IDF utilisant `esp_ble_gap_set_ext_scan_params()` et `esp_ble_gap_start_ext_scan()`. Cet étage sort les advertisements étendus sur la console série. Il valide le lien XIAO → ESP32-U et la réception intégrale des 74 octets, sans dépendre d'ESPHome.

Critères de réussite :
- l'adresse du XIAO apparaît dans les résultats de scan ;
- la longueur de payload rapportée vaut 74 octets ;
- les octets `D2 FC 44` sont présents et le contenu correspond à la trame §6.3.

**Étage 2 — Remontée vers Home Assistant.**

L'API d'extended scan n'est pas câblée dans `esp32_ble_tracker` (§6.2). Deux voies pour la franchir :

| Voie | Description |
|---|---|
| **Composant externe ESPHome** | Un composant `external_components` implémentant le scan étendu et publiant les advertisements dans la file du `bluetooth_proxy`. La refonte 2026.8 autour de `ble_device_base` fournit le point d'accroche. Chemin le plus direct pour conserver l'intégration native BTHome de HA. |
| **Passerelle MQTT** | L'ESP32-U décode le service data `0xFCD2` à bord et publie en MQTT Discovery. Contourne la couche proxy ; les entités sont créées par l'intégration MQTT plutôt que par BTHome. |

Sur l'ESP32-U, la configuration IDF doit activer BLE 5.0 :

```yaml
esp32:
  board: <carte de l'ESP32-U>
  framework:
    type: esp-idf
    sdkconfig_options:
      CONFIG_BT_BLE_50_FEATURES_SUPPORTED: y
      CONFIG_BT_BLE_42_FEATURES_SUPPORTED: n
```

> Les jeux de fonctionnalités 4.2 et 5.0 de la pile Bluedroid s'excluent mutuellement : ce proxy de test opère donc **en extended scan seul**. C'est la raison pour laquelle il est dédié au banc et reste hors du parc de production, qui continue de recevoir les 24 autres capteurs en profil L.

### 6.6 Protocole de validation

| # | Étape | Critère de réussite |
|---|---|---|
| 1 | XIAO en profil E, observé avec **nRF Connect** | Advertising signalé comme étendu, 74 octets, `0xFCD2` présent |
| 2 | ESP32-U en étage 1, XIAO à 2 m | Adresse et payload complet en console série |
| 3 | Éloigner le XIAO jusqu'à −85 dBm | Payload toujours intègre, taux de réception relevé |
| 4 | Basculer le même XIAO en profil L, ESP32-U en ESPHome standard | Réception confirmée : référence de portée pour comparaison |
| 5 | Comparer les taux de réception étendu / legacy à distance égale | Écart mesuré, décision de généralisation |
| 6 | ESP32-U en étage 2 | Device visible dans *Bluetooth → Advertisement Monitor*, entités BTHome créées |
| 7 | Consommation du XIAO en profil E, au PPK II | Comparaison au budget §4.4 |

L'étape 4 est déterminante : elle sépare un problème de chemin de code d'un problème de propagation radio.

### 6.7 Ce que le profil E change sur le nœud

| Aspect | Profil L | Profil E |
|---|---|---|
| Nombre de trames | 3 | 1 |
| Compteur Packet ID | Partagé entre trames | Unique par nature |
| Ordonnancement | Événementiel + périodique 15 min | Le même, avec un contenu complet à chaque émission |
| Charge utile par émission | 21 à 23 octets de mesures | 57 octets de mesures |
| Temps d'antenne par event | 1 canal primaire | Canal primaire + canal secondaire |
| Entités HA | Identiques | Identiques |

Le passage de 3 trames à 1 supprime le besoin de faire coïncider les compteurs entre trames et rend chaque advertising autoportant : un seul paquet reçu suffit à rafraîchir l'ensemble des entités.

---

## 7. Firmware — nRF Connect SDK / Zephyr

### 7.1 Driver de l'IMU

Le LSM6DS3TR-C est piloté sous Zephyr par le driver **LSM6DSL**, via la chaîne `compatible = "st,lsm6dsl"` — c'est la déclaration qu'emploie la documentation Zephyr de Seeed pour le XIAO nRF52840 Sense.

```conf
CONFIG_LSM6DSL=y
CONFIG_LSM6DSL_TRIGGER_OWN_THREAD=y
```

Relever dans `boards/seeed/xiao_nrf54lm20a/` le **label du nœud IMU**, sa **chaîne `compatible`** et le **GPIO d'INT1** tels que livrés par Seeed / Zephyr : ils déterminent le label à référencer dans l'overlay et le symbole Kconfig à activer. Première étape avant compilation (phase 0, §10).

### 7.2 Adresse BLE

Home Assistant identifie un device BTHome **par son adresse MAC**. Une adresse constante d'un redémarrage à l'autre garantit qu'un capteur reste le même appareil dans l'inventaire, ce qui compte sur un parc de 25 unités.

L'identifiant unique de la puce sert de graine, via l'API **`hwinfo`** de Zephyr, portable et indépendante du SoC :

```c
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/drivers/hwinfo.h>

/* À appeler AVANT bt_enable(). CONFIG_HWINFO=y, CONFIG_BT_PRIVACY=n. */
static int santuario_set_identity(void)
{
    uint8_t devid[16];
    bt_addr_le_t addr = { .type = BT_ADDR_LE_RANDOM };

    ssize_t n = hwinfo_get_device_id(devid, sizeof(devid));
    if (n < 6) {
        return -ENODEV;
    }

    memcpy(addr.a.val, devid, 6);
    /* Adresse statique aléatoire : les 2 bits de poids fort de l'octet
       le plus significatif valent 0b11. */
    addr.a.val[5] |= 0xC0;

    return bt_id_create(&addr, NULL);
}
```

Déterministe, reproductible, indépendant du stockage de configuration et sans écriture en RRAM. Variante possible : `CONFIG_BT_SETTINGS=y` avec un backend NVS/ZMS, qui persiste l'adresse générée par la pile.

### 7.3 `prj.conf` — profil L

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
CONFIG_BT_EXT_ADV=n            # profil L ; voir §6.4 pour le profil E

# --- Identité ---
CONFIG_HWINFO=y

# --- Capteurs ---
CONFIG_SENSOR=y
CONFIG_I2C=y
CONFIG_LSM6DSL=y               # driver du LSM6DS3TR-C (§7.1)
CONFIG_LSM6DSL_TRIGGER_OWN_THREAD=y

# --- PMIC ---
CONFIG_REGULATOR=y
CONFIG_MFD=y
CONFIG_ADC=y

# --- Power management ---
CONFIG_PM=y
CONFIG_PM_DEVICE=y

# --- Divers ---
CONFIG_MAIN_STACK_SIZE=2048
CONFIG_LOG=y
CONFIG_LOG_DEFAULT_LEVEL=2
```

### 7.4 `app.overlay` — LDO1 à 3,3 V

Structure reprise d'un projet publié fonctionnant sur cette carte (XIAO nRF54LM20A Sense sous Zephyr) :

```dts
/* Bus I2C du PMIC */
&pmic_i2c {
    sda-gpios = <&gpio1 18 GPIO_ACTIVE_HIGH>;
    scl-gpios = <&gpio1 17 GPIO_ACTIVE_HIGH>;
    status = "okay";
};

/* LDO1 alimente l'IMU et le micro PDM : 3,3 V */
&pmic {
    regulators {
        LDO1 {
            regulator-min-microvolt = <3300000>;
            regulator-max-microvolt = <3300000>;
            regulator-boot-on;
        };
    };
};
```

Le rail est établi **avant** que le driver du capteur ne sonde le bus. Deux moyens, selon ce que permet le DTS de carte :

- priorité d'initialisation du régulateur supérieure à celle du capteur (`CONFIG_SENSOR_INIT_PRIORITY`), ou
- initialisation différée du nœud capteur (`zephyr,deferred-init`) et appel explicite de `device_init()` après `regulator_enable()`.

```c
const struct device *imu = DEVICE_DT_GET(DT_NODELABEL(lsm6ds3tr_c));  /* label à confirmer, §7.1 */

/* variante init différée */
k_msleep(20);          /* montée du rail + boot du LSM6DS3TR-C */
device_init(imu);
```

### 7.5 Construction du payload BTHome

```c
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#define BTHOME_UUID_LO      0xD2
#define BTHOME_UUID_HI      0xFC
#define BTHOME_INFO_TRIG    0x44   /* v2, clair, trigger-based */

/* Object IDs — extraits de bthome-ble 3.9.2 */
#define OBJ_PACKET_ID       0x00   /* uint8              */
#define OBJ_BATTERY         0x01   /* uint8   %          */
#define OBJ_TEMPERATURE     0x02   /* sint16  x0.01 °C   */
#define OBJ_VOLTAGE         0x0C   /* uint16  x0.001 V   */
#define OBJ_GENERIC_BOOL    0x0F   /* uint8   0/1        */
#define OBJ_BATTERY_LOW     0x15   /* uint8   0/1        */
#define OBJ_BATTERY_CHARGE  0x16   /* uint8   0/1        */
#define OBJ_MOTION          0x21   /* uint8   0/1        */
#define OBJ_MOVING          0x22   /* uint8   0/1        */
#define OBJ_TAMPER          0x2B   /* uint8   0/1        */
#define OBJ_VIBRATION       0x2C   /* uint8   0/1        */
#define OBJ_BUTTON          0x3A   /* uint8   event id   */
#define OBJ_ROTATION        0x3F   /* sint16  x0.1 °     */
#define OBJ_ACCELERATION    0x51   /* uint16  x0.001 m/s²*/
#define OBJ_GYROSCOPE       0x52   /* uint16  x0.001 °/s */
#define OBJ_FW_VERSION      0xF2   /* 3 octets           */

/* Compteur unique partagé par toutes les trames. */
static uint8_t bthome_pid;

/* 24 octets suffisent au profil L ; 64 couvrent aussi le profil E. */
struct bthome_buf {
    uint8_t data[64];
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

/* --- PROFIL L, TRAME A : événement + orientation --- */
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

/* --- PROFIL L, TRAME B : périodique batterie / santé --- */
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

/* --- PROFIL E : trame unique (§6.3) --- */
struct node_state {
    uint8_t  soc_pct;   int16_t  temp_cc;   uint16_t vbat_mv;
    bool     low, charging, activity, motion, moving, freefall, dbl_tap;
    uint8_t  button_evt;
    int16_t  pitch_dd, roll_dd, yaw_dd;
    uint16_t ax, ay, az;      /* magnitudes, milli-m/s²  */
    uint16_t gx, gy, gz;      /* magnitudes, milli-°/s   */
    uint8_t  fw_major, fw_minor, fw_patch;
};

static void build_frame_e(struct bthome_buf *b, const struct node_state *s)
{
    bth_init(b);
    bth_u8 (b, OBJ_PACKET_ID,       bthome_pid);
    bth_u8 (b, OBJ_BATTERY,         s->soc_pct);
    bth_s16(b, OBJ_TEMPERATURE,     s->temp_cc);
    bth_u16(b, OBJ_VOLTAGE,         s->vbat_mv);
    bth_u8 (b, OBJ_GENERIC_BOOL,    s->activity ? 1 : 0);
    bth_u8 (b, OBJ_BATTERY_LOW,     s->low      ? 1 : 0);
    bth_u8 (b, OBJ_BATTERY_CHARGE,  s->charging ? 1 : 0);
    bth_u8 (b, OBJ_MOTION,          s->motion   ? 1 : 0);
    bth_u8 (b, OBJ_MOVING,          s->moving   ? 1 : 0);
    bth_u8 (b, OBJ_TAMPER,          s->freefall ? 1 : 0);
    bth_u8 (b, OBJ_VIBRATION,       s->dbl_tap  ? 1 : 0);
    bth_u8 (b, OBJ_BUTTON,          s->button_evt);
    bth_s16(b, OBJ_ROTATION,        s->pitch_dd);
    bth_s16(b, OBJ_ROTATION,        s->roll_dd);
    bth_s16(b, OBJ_ROTATION,        s->yaw_dd);
    bth_u16(b, OBJ_ACCELERATION,    s->ax);
    bth_u16(b, OBJ_ACCELERATION,    s->ay);
    bth_u16(b, OBJ_ACCELERATION,    s->az);
    bth_u16(b, OBJ_GYROSCOPE,       s->gx);
    bth_u16(b, OBJ_GYROSCOPE,       s->gy);
    bth_u16(b, OBJ_GYROSCOPE,       s->gz);
    b->data[b->len++] = OBJ_FW_VERSION;
    b->data[b->len++] = s->fw_patch;
    b->data[b->len++] = s->fw_minor;
    b->data[b->len++] = s->fw_major;
    /* len = 3 + 57 = 60 */
}
```

### 7.6 Émission du train d'advertising — profil L

```c
#include <zephyr/bluetooth/bluetooth.h>

/* 100 ms en unités de 0,625 ms */
#define ADV_INT      0x00A0
#define TRAIN_MS     700

/* Non connectable, non scannable, adresse d'identité. */
static const struct bt_le_adv_param adv_param =
    BT_LE_ADV_PARAM_INIT(BT_LE_ADV_OPT_USE_IDENTITY, ADV_INT, ADV_INT, NULL);

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

/* Incrémenté entre deux jeux de données distincts. */
static void bthome_next_packet(void) { bthome_pid++; }
```

Points de vigilance :

- `BT_LE_ADV_OPT_USE_IDENTITY` fixe l'usage de l'adresse d'identité, ce qui maintient une MAC constante côté Home Assistant. **Le nom de ce symbole a évolué entre versions de Zephyr** — le confirmer dans `zephyr/include/zephyr/bluetooth/bluetooth.h` de la version de NCS installée.
- La PDU visée est `ADV_NONCONN_IND` : les options connectable et scannable restent en dehors du paramétrage.
- `bt_le_adv_stop()` en fin de train rend le montage économe : l'advertising s'exécute par salves.

### 7.7 Calcul pitch / roll

Avec l'accéléromètre, en statique — l'ouvrant à l'arrêt, moment où l'angle est pertinent :

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

- Moyenner **8 à 16 échantillons** pour ramener la gigue de bruit sous 0,1°.
- Déclencher le calcul après stabilisation : ‖a‖ ≈ 9,81 ± 0,3 m/s² pendant ≥ 200 ms, ce qui garantit que la mesure porte sur la gravité seule.
- Stocker un **offset de calibration par capteur** en RRAM, réglable depuis HA (position fermée = 0°).
- Recaler le **yaw** à zéro à chaque détection d'inactivité.

---

## 8. Intégration Home Assistant

### 8.1 Découverte

1. Intégration **Bluetooth** active, proxies adoptés.
2. À la première trame BTHome valide, le device apparaît en **Découvert** dans *Paramètres → Appareils et services*.
3. Sinon : *Ajouter une intégration → BTHome*.

### 8.2 Entités générées

| Profil | Object ID | Entité HA | Nom par défaut |
|---|---|---|---|
| L (A) / E | `0x0F` | `binary_sensor` | Generic |
| L (A) / E | `0x21` | `binary_sensor` | Motion |
| E | `0x22` | `binary_sensor` | Moving |
| L (A) / E | `0x2B` | `binary_sensor` | Tamper |
| L (A) / E | `0x2C` | `binary_sensor` | Vibration |
| L (A) / E | `0x3A` | `event` | Button |
| L (A) / E | `0x3F` #1 | `sensor` | **Rotation** |
| L (A) / E | `0x3F` #2 | `sensor` | **Rotation 2** |
| L (A) / E | `0x3F` #3 | `sensor` | **Rotation 3** |
| L (B) / E | `0x01` | `sensor` | Battery |
| L (B) / E | `0x02` | `sensor` | Temperature |
| L (B) / E | `0x0C` | `sensor` | Voltage |
| L (B) / E | `0x15` | `binary_sensor` | Battery |
| L (B) / E | `0x16` | `binary_sensor` | Battery charging |
| L (C) / E | `0x51` ×3 | `sensor` | Acceleration, 2, 3 |
| L (C) / E | `0x52` ×3 | `sensor` | Gyroscope, 2, 3 |

Les entités **persistent entre les trames** : Home Assistant fusionne les mises à jour partielles et une entité conserve sa dernière valeur jusqu'à la suivante. Le découpage en 3 trames du profil L est transparent côté interface, et le profil E produit exactement les mêmes entités.

Renommage à la mise en service :

| Entité auto | Renommer en |
|---|---|
| Rotation | Pitch |
| Rotation 2 | Roll |
| Rotation 3 | Yaw |
| Generic | Activité |
| Tamper | Chute / Choc |
| Vibration | Double-tap |
| Temperature | Température interne *(diagnostic)* |

### 8.3 Dériver l'état d'ouverture depuis l'angle

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

Pour 25 capteurs, générer ce bloc par boucle Jinja dans un package versionné (`HA-Santuario/config/packages/ouvrants.yaml`).

### 8.4 Supervision

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

Le heartbeat horaire (§4.3) rend cette supervision exploitable : il distingue un ouvrant immobile depuis plusieurs jours d'un capteur en défaut.

---

## 9. Procédure de mise en œuvre

### Phase 0 — Relevés matériels

1. Relever dans `boards/seeed/xiao_nrf54lm20a/` le **label du nœud IMU**, sa **chaîne `compatible`** et le **GPIO d'INT1** (§7.1).
2. Compiler un `hello_world`, puis un exemple capteur, et confirmer la lecture de l'IMU après application de l'overlay §7.4.

### Phase 1 — Validation unitaire, profil L (1 capteur, 1 proxy)

3. Flasher avec **trame B uniquement**, toutes les **30 s** (cadence accélérée pour la mise au point).
4. Vérifier avec **nRF Connect** : UUID `0xFCD2`, device info `0x44`, adresse **identique après reboot**.
5. Vérifier la découverte du device BTHome dans Home Assistant.
6. Redémarrer le capteur 3 fois : le device reste unique dans l'inventaire.
7. Passer la trame B à 15 min et confirmer que le device reste disponible au-delà de 5 min, ce qui valide le bit trigger-based.

### Phase 2 — Trame A

8. Ajouter la trame A avec pitch/roll/yaw seuls, sur seuil angulaire.
9. Contrôler que `Rotation`, `Rotation 2` et `Rotation 3` correspondent à pitch, roll et yaw, et conservent cette correspondance dans la durée.
10. Ajouter les binaires et l'événement bouton.
11. Vérifier le retour à `0` des binaires après le timeout d'inactivité.
12. Activer le journal `debug` sur `bthome_ble` le temps d'une session et confirmer un décodage sans `Invalid Object ID found in payload` (§2.3a) ni `not sending object ids in numerical order` (§2.3b).

### Phase 3 — Énergie

13. Mesurer la consommation moyenne sur 24 h (Nordic Power Profiler Kit II ou nPM1300 EK).
14. Ajuster les seuils IMU et le taux de rafraîchissement.
15. Comparer au budget §4.4 avant de commander les 22 capteurs restants.

### Phase 4 — Banc Publicité Étendue, en parallèle

16. Réserver 1 XIAO et le proxy **ESP32-U** au banc (§6.1).
17. Dérouler le protocole §6.6, étapes 1 à 7.
18. Statuer sur la généralisation du profil E au vu des taux de réception comparés (étape 5) et du bilan énergétique (étape 7).

### Phase 5 — Déploiement sur 4 niveaux

19. Déployer 1 proxy par niveau (§5.1).
20. Déployer les capteurs par lot de 5, avec un nom `SANT-XX` unique.
21. Vérifier le RSSI de chaque capteur (*Bluetooth → Advertisement Monitor*). Cible : **> −85 dBm** sur au moins un proxy.
22. Calibrer l'offset angulaire de chaque ouvrant en position fermée.
23. Générer les templates §8.3 et les pousser dans `thieryus007-cloud/HA-Santuario`.
24. Étendre la supervision §8.4 aux 25 capteurs.

---

## 10. Diagnostic

| Symptôme | Cause probable | Action |
|---|---|---|
| Device non découvert | Flags AD à ajouter | Insérer `02 01 06` en tête d'advertising |
| Device non découvert, profil L | Advertising étendu actif | `CONFIG_BT_EXT_ADV=n` |
| Device non découvert, profil E | Extended scan à activer côté proxy | §6.5, étages 1 puis 2 |
| Mesures partielles | Object ID inconnu dans la trame | Journal `debug` de `bthome_ble` : `Invalid Object ID found in payload`. Le décodage s'arrête à cet ID. |
| Avertissement d'ordre dans le journal | IDs non croissants | Réordonner selon §2.4 |
| Device dupliqué à chaque reboot | Identité BLE à fixer | `bt_id_create()` + option d'identité, `CONFIG_BT_PRIVACY=n` (§7.2) |
| Device `unavailable` toutes les 5 min | Device info à `0x40` | Bit 2 = 1 sur toutes les trames (§2.2) |
| Un événement sur deux ignoré | Deux jeux de données à < 4 s avec le même packet id | Incrémenter le compteur entre jeux distincts (§2.3c) |
| Motion reste `on` | Trame de retour au repos à émettre | Trame tous binaires à `0` (§2.6) |
| Pitch / Roll / Yaw permutés | Ordre d'insertion variable | Insérer les 3 `0x3F` dans le même ordre à chaque trame |
| Accélération toujours positive | `0x51` porte une magnitude | Utiliser `0x63` pour des composantes signées (§2.5) |
| Gyroscope plafonne à 65,5 | Plage de `0x52` atteinte | Clamper, ou limiter le range gyro à ±125 dps |
| Payload profil E tronqué | Buffer ou `CONFIG_BT_CTLR_ADV_DATA_LEN_MAX` à augmenter | `data[64]` et `255` (§6.4) |
| Événements manqués | Train d'advertising à allonger | 700 ms, 100 ms d'intervalle (§4.2) |
| Événements manqués | `window` du proxy à augmenter | §5.1, en tenant compte du changement de défaut en 2026.8 |
| `Too many BLE events to process` | Proxy au plafond | Ajouter un proxy, ou passer en Ethernet |
| Proxy instable, Wi-Fi qui décroche | Rapport cyclique de scan élevé | Réduire `window`, ou passer en Ethernet |
| IMU non détectée / WHO_AM_I invalide | LDO1 à 1,8 V, ou driver à activer | Overlay §7.4 ; `CONFIG_LSM6DSL` (§7.1) |
| Angles bruités ±1° | Moyennage à mettre en place | 8 à 16 échantillons + attente de stabilité (§7.7) |
| Capteurs émettant simultanément | Jitter à ajouter sur la trame B | ±30 s d'aléa (§4.3) |

---

## 11. Décisions retenues

| Sujet | Décision |
|---|---|
| Transmission | **En clair**, sur les deux profils |
| Profil de production | **L — Legacy 1M**, 31 octets, 3 trames |
| Profil en évaluation | **E — Étendu**, trame unique 74 / 255 octets, banc dédié 1 XIAO + 1 ESP32-U (§6) |
| Protocole | BTHome v2, UUID `0xFCD2` |
| Device info | **`0x44`** sur toutes les trames, tous profils |
| Packet ID | Compteur unique partagé, constant sur un train, incrémenté entre jeux distincts |
| Angles | `0x3F` ×3 (signé, 0.1°) — pitch, roll, yaw dans cet ordre |
| Accélération / gyroscope bruts | Magnitudes via `0x51` / `0x52` ; `0x63` pour une accélération signée |
| Adresse BLE | Statique aléatoire dérivée de `hwinfo_get_device_id()` |
| Driver IMU | `CONFIG_LSM6DSL` (`st,lsm6dsl`), label et INT1 confirmés en phase 0 |
| Régime d'alimentation | System ON + PM Zephyr, réveil sur INT1 |
| Train d'advertising | 100 ms d'intervalle, 700 ms de durée |
| Proxies de production | 4 × ESP32-S3 ESPHome, esp-idf, scan passif ; SLZB série U en alternative (§5.4) |
| Proxy de test | 1 × ESP32-U, extended scan, hors parc de production |
| Température | Grandeurs internes en `diagnostic` ; SHT4x/BME280 en I²C pour une mesure d'ambiance |

---

## 12. Points à confirmer sur banc

| # | Élément | Étape |
|---|---|---|
| 1 | Label du nœud IMU, chaîne `compatible` et GPIO d'INT1 sur `xiao_nrf54lm20a` | Phase 0, préalable à la première compilation |
| 2 | Nom des symboles Zephyr `BT_LE_ADV_OPT_USE_IDENTITY` et `BT_LE_ADV_PARAM_INIT` dans la version de NCS installée | Phase 0 |
| 3 | Pinout `pmic_i2c` de l'overlay §7.4, à recouper avec le schéma Seeed | Phase 0 |
| 4 | Persistance du nom local appris via la trame B ; à défaut, renommage unique dans l'interface HA | Phase 1 |
| 5 | Consommation du LSM6DS3TR-C et du nPM1300 en veille | Phase 3, au PPK II |
| 6 | Référence du board ESP32-U pour la clé `board:` d'ESPHome | Phase 4 |
| 7 | Réception des 74 octets étendus par l'ESP32-U | Phase 4, protocole §6.6 étapes 1–3 |
| 8 | Taux de réception étendu comparé au legacy à distance égale | Phase 4, étapes 4–5 |
| 9 | Chemin de remontée vers HA en profil E : composant externe ESPHome ou passerelle MQTT | Phase 4, étape 6 |
| 10 | Référence exacte des unités SMLIGHT déployées, pour l'option §5.4 | Phase 5 |

La table des Object IDs, les formats, longueurs, facteurs et signes, le comportement du décodage sur les IDs inconnus, l'ordre et la déduplication, l'indicateur sleepy et les budgets d'octets sont établis sur code source ou spécification, et recalculés octet par octet.

---

## Sources

**Code source consulté directement**
- `bthome-ble` 3.9.2 (`parser.py`, `const.py`) — bibliothèque exécutée par l'intégration BTHome de Home Assistant

**Spécifications et documentation officielles**
- BTHome v2 — `https://bthome.io/format/`
- Home Assistant — intégration BTHome, intégration SMLIGHT SLZB
- Seeed Studio Wiki — XIAO nRF54LM20A Sense ; XIAO nRF52840 avec Zephyr (déclaration `st,lsm6dsl`)
- Zephyr Project — boards `seeed/xiao_nrf54lm20a` et `seeed/xiao_ble` ; API `bt_le_ext_adv_*`
- ESPHome — `bluetooth_proxy`, `esp32_ble_tracker`, changelog 2026.8.0
- arduino-esp32 — exemple `BLE5_extended_scan` (ESP32-C3 / S3)
- Bluetooth SIG — « Exploring Bluetooth Core 5.0: What's new in advertising »

**Discussions et suivis de développement**
- esphome/esphome#18047 — configuration BLE de la pile ESP-IDF et périmètre des API utilisées
- esphome/esphome#10626 — Bluetooth Proxy et balises en advertising étendu
- esphome/esphome#17150 — composant `ble_device_base`, couche BLE neutre
- home-assistant/core#78702 / #79669 — `UNAVAILABLE_TRACK_SECONDS` et devices « sleepy »
- SMLIGHT — SLZB en adaptateur Bluetooth distant avec Home Assistant 2026.8
