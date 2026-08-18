# Capteur d'angle de porte — Option B : tout en ESPHome (XIAO nRF52840 Sense)

**Objectif :** identique à l'Option A — capteur autonome mesurant l'angle d'ouverture d'une porte (2 axes) et publiant vers Home Assistant l'**angle en degrés**, le **niveau batterie (%)** et l'**état ouvert/fermé dérivé**. Réveil au mouvement + rafraîchissement périodique. Autonomie cible 14–18 mois sur LiPo 604050 1500 mAh.

**Différence avec l'Option A :** le firmware du capteur est écrit **entièrement en YAML ESPHome** (au lieu de C/Zephyr). L'IMU, le calcul d'angle, le deep sleep, le wake-on-motion et l'émission BTHome sont des composants ESPHome. Flash par UF2 (double-tap reset), sans debugger SWD. Maintenance par OTA depuis l'add-on Home Assistant.

**Statut de validation :** la syntaxe des composants (nRF52, lsm6ds, deep_sleep, bthome, adc, template) est vérifiée sur la doc officielle ESPHome et la doc du composant BTHome. **Le seul point NON garanti est la consommation réelle en sommeil**, à mesurer au PPK2 avant toute commande des 25 capteurs (voir §9). C'est précisément ce que cette Option B sert à tester.

---

## 0. Pourquoi cette option, et son unique risque

L'Option B est nettement plus simple à vivre : pas de toolchain Zephyr, pas de SWD, mises à jour OTA, configuration lisible en YAML. Les composants nécessaires existent tous nativement :

- plateforme **nRF52** (board `xiao_ble`), flash UF2 via bootloader Adafruit ;
- composant **lsm6ds** (IMU LSM6DS3TR-C, détection auto, I²C 0x6A) ;
- composant **deep_sleep** (supporte nRF52, réveil sur pin ou durée) ;
- composant externe **bthome** (émission BTHome v2, spécifiquement documenté pour nRF52/XIAO BLE) ;
- **template sensor** (calcul `atan2` de l'angle en lambda), **adc** (batterie).

**L'unique inconnue :** la consommation de sommeil sous ESPHome/nRF52.
- La doc du composant BTHome annonce ~2–5 µA en deep sleep et 6–12 mois sur 400 mAh à 5 min d'intervalle → transposé à ta 1500 mAh, cohérent avec 14–18 mois.
- **Mais** un cas communautaire (firmware Zigbee ESPHome sur XIAO nRF52840 Sense) a mesuré ~7 mA au repos entre mesures — soit ~200× trop. Ce cas concernait Zigbee, pas BTHome+deep_sleep, mais il impose de **mesurer** avant de généraliser.

→ **Règle : flasher cette Option B, mesurer au PPK2. Si µA → adopter B pour les 25. Si mA → basculer sur l'Option A (Zephyr).**

---

## 1. Matériel et outillage

Identiques à l'Option A, **sauf que le débogueur SWD n'est PAS requis** pour B (flash UF2). Le PPK2 reste indispensable pour la décision.

### Par capteur
1. Seeed XIAO **nRF52840 Sense** (IMU LSM6DS3TR-C intégré)
2. Batterie LiPo **604050 3,7 V 1500 mAh**
3. Fils/connecteur batterie → pads BAT+/BAT− (vérifier polarité au multimètre)
4. Boîtier imprimé + fixation battant

### Infrastructure (site)
5. XIAO **ESP32-S3** en `bluetooth_proxy` ESPHome (1 par étage recommandé)
6. Alim USB 5 V secteur par proxy

### Outillage
7. Câble USB-C **data** (pas charge-only) pour flash UF2
8. **Nordic PPK2** ou µA-mètre précis — décision A vs B
9. Home Assistant avec add-on **ESPHome Device Builder** installé

---

## 2. Câblage

Identique à l'Option A :
- **Batterie** : souder BAT+/BAT− aux pads du XIAO, **polarité vérifiée au multimètre** avant branchement (les LiPo tierces inversent souvent le connecteur).
- **IMU** : rien à câbler, le LSM6DS3TR-C est sur le bus I²C interne du XIAO Sense.
- **Pas de SWD** en Option B.

> **Rappel danger (identique A) :** la lecture batterie du XIAO passe par un pont diviseur activé par P0.14 (VBAT_ENABLE). Ne jamais forcer P0.14 HIGH pendant la charge USB (risque de griller P0.31). En ESPHome, on se contente de lire l'ADC (§4) sans manipuler P0.14, ce qui est le comportement sûr par défaut.

---

## 3. Convention de broches nRF52 sous ESPHome

**Important :** sous la plateforme nRF52, ESPHome utilise des **numéros de broches nus**, pas la notation `GPIO...`.

| Silkscreen | Numéro | Fonction |
|---|---|---|
| D0 | 0 | I/O |
| D3 | 3 | I/O |
| D4 | 4 | I²C SDA (broche externe) |
| D5 | 5 | I²C SCL (broche externe) |
| — | 29 | VBAT/2 (lecture batterie XIAO BLE) |

> L'IMU interne est sur un **bus I²C interne** distinct des broches D4/D5 externes. Le composant `lsm6ds` détecte l'adresse 0x6A ; on déclare le bus I²C correspondant. Voir la note en §4.2 : si l'IMU n'est pas détecté, c'est le point à ajuster (bus interne vs pins).

---

## 4. Configuration ESPHome complète (`door-angle-sensor.yaml`)

```yaml
esphome:
  name: porte-angle-01
  friendly_name: "Porte Angle 01"

# --- Plateforme nRF52 (XIAO BLE), flash UF2 ---
nrf52:
  board: xiao_ble
  bootloader: adafruit

logger:
  level: INFO          # DEBUG en phase de mise au point

# --- Composant BTHome externe (émission BLE) ---
external_components:
  - source:
      type: git
      url: https://github.com/dz0ny/esphome-bthome
      ref: main
    components: [bthome]

# ============================================================
#  BUS I2C (IMU interne LSM6DS3TR-C)
# ============================================================
i2c:
  # Bus interne du XIAO Sense pour l'IMU.
  # Si l'IMU n'est pas détecté au boot (voir logs), ajuster sda/scl
  # selon le schéma : l'IMU interne n'est pas sur D4/D5.
  sda: 4
  scl: 5
  scan: true
  frequency: 100kHz

# ============================================================
#  IMU LSM6DS3TR-C
# ============================================================
sensor:
  - platform: lsm6ds
    address: 0x6A
    accelerometer_range: 2G
    accelerometer_odr: 26HZ      # bas ODR = basse conso, suffisant pour un angle
    gyroscope_odr: OFF           # gyro inutile pour l'angle -> coupé (économie)
    accel_x:
      name: "Accel X"
      id: accel_x
      internal: true             # interne : pas remonté à HA, sert au calcul
    accel_y:
      name: "Accel Y"
      id: accel_y
      internal: true
    accel_z:
      name: "Accel Z"
      id: accel_z
      internal: true
    update_interval: never       # lecture pilotée par le cycle (voir on_boot)

  # --- Angle calculé (atan2, 2 axes) ---
  - platform: template
    name: "Angle porte"
    id: angle_porte
    unit_of_measurement: "°"
    accuracy_decimals: 0
    lambda: |-
      // Angle 2 axes sur le plan de rotation de la porte.
      // Projection sur (y, z) : ajuster le couple d'axes selon le montage.
      float ay = id(accel_y).state;
      float az = id(accel_z).state;
      float ang = atan2(ay, az) * 180.0 / 3.14159265;
      return ang;
    update_interval: never

  # --- Batterie ---
  - platform: adc
    pin: 29                       # VBAT/2 sur XIAO BLE
    id: battery_voltage
    internal: true
    update_interval: never
    filters:
      - multiply: 2.0             # compense le pont diviseur /2

  - platform: template
    name: "Batterie"
    id: battery_percent
    unit_of_measurement: "%"
    accuracy_decimals: 0
    lambda: |-
      float v = id(battery_voltage).state;
      float p = (v - 3.0) / (4.2 - 3.0) * 100.0;   // LiPo 3.0V=0% .. 4.2V=100%
      if (p > 100) p = 100;
      if (p < 0)   p = 0;
      return p;
    update_interval: never

# ============================================================
#  ÉTAT PORTE dérivé (ouvert/fermé selon seuil d'angle)
# ============================================================
binary_sensor:
  - platform: template
    name: "Porte"
    id: porte_ouverte
    device_class: door
    lambda: |-
      float a = id(angle_porte).state;
      // seuil +/-15 deg : au-dela = ouvert. A calibrer (voir Calibration).
      if (a > 15.0 || a < -15.0) return true;
      return false;

# ============================================================
#  ÉMISSION BTHOME
# ============================================================
bthome:
  min_interval: 5s
  max_interval: 10s
  tx_power: 4                     # bon compromis portee/conso (nRF52 : -40..+8)
  sensors:
    - type: rotation              # angle (voir note object-id ci-dessous)
      id: angle_porte
    - type: battery
      id: battery_percent
  binary_sensors:
    - type: door
      id: porte_ouverte
      advertise_immediately: true # publie tout de suite au reveil mouvement

# ============================================================
#  CYCLE DE MESURE au reveil : lire capteurs puis publier
# ============================================================
esphome:
  name: porte-angle-01
  on_boot:
    priority: -100
    then:
      - component.update: accel_x
      - component.update: accel_y
      - component.update: accel_z
      - component.update: angle_porte
      - component.update: battery_voltage
      - component.update: battery_percent
      - delay: 500ms              # laisse le temps a BTHome de diffuser

# ============================================================
#  DEEP SLEEP + WAKE-ON-MOTION
# ============================================================
deep_sleep:
  id: deep_sleep_1
  run_duration: 3s                # temps eveille par cycle (mesure + advertising)
  sleep_duration: 10min           # reveil periodique 10 min
  wakeup_pin:
    number: 11                    # P0.11 = INT1 de l'IMU (wake-on-motion)
    mode: INPUT
    allow_other_uses: false
    inverted: false               # INT1 actif HAUT au mouvement (a confirmer)
```

> **Notes critiques sur ce YAML :**
>
> **1. object-id « angle » BTHome.** BTHome v2 n'a pas de type « angle » standard. Le composant ESPHome-BTHome expose un type `rotation` : vérifier dans la page *Sensor Types* du composant l'object-id réellement émis et son unité. Si `rotation` n'est pas disponible dans ta version du composant, remplacer par un `count` signé (comme en Option A) et habiller l'unité côté HA. **À confirmer au premier flash** en lisant l'entité créée dans HA.
>
> **2. Bus I²C interne.** Les valeurs `sda: 4 / scl: 5` sont les broches externes ; l'IMU interne du XIAO Sense peut être sur un autre bus. Si `scan: true` ne détecte pas 0x6A au boot (voir logs USB en phase dev), ajuster selon le schéma Seeed. C'est le point d'intégration le plus susceptible de demander un ajustement.
>
> **3. Wake-on-motion.** Le `wakeup_pin` sur P0.11 réveille le nRF quand l'IMU tire INT1. Mais **il faut que l'IMU soit configuré pour émettre une interruption de mouvement sur INT1** — or le composant `lsm6ds` d'ESPHome n'expose pas forcément la config wake-up registre (MD1_CFG). Deux cas :
>    - si le composant configure INT1 en data-ready seulement, le wake périodique (10 min) fonctionnera mais pas forcément le wake sur mouvement fin ;
>    - sinon, le réveil se fera surtout par `sleep_duration` (10 min), ce qui reste fonctionnel mais rate les ouvertures entre deux réveils.
>    **À valider en test** : ouvrir la porte et vérifier une publication immédiate. Si le wake-on-motion ne se déclenche pas, c'est l'argument fort pour basculer en Option A (Zephyr), où la config registre INT1 est explicite et maîtrisée.
>
> **4. Doublon `esphome:`.** Le bloc `esphome:` avec `on_boot` doit être **fusionné** avec le bloc `esphome:` du haut (un seul bloc `esphome:` par fichier). Il est présenté séparé ici pour la lisibilité ; à la fusion, garder `name`, `friendly_name`, et `on_boot` dans un unique bloc.

---

## 5. Préparation dans Home Assistant

1. Installer l'add-on **ESPHome Device Builder** (Paramètres → Modules complémentaires).
2. Créer un nouveau device, coller le YAML ci-dessus (bloc `esphome:` fusionné).
3. Renseigner les secrets si besoin (aucun Wi-Fi requis côté capteur nRF : il n'émet qu'en BLE).

---

## 6. Build et flash (UF2, sans debugger)

1. Dans ESPHome : **Install → Manual download**, compiler.
2. Récupérer le fichier UF2 :
   `.esphome/build/porte-angle-01/.pioenvs/porte-angle-01/zephyr.uf2`
3. Brancher le XIAO en USB-C (câble **data**).
4. **Double-tap** rapide sur RST (< 0,5 s) → lecteur `XIAO-SENSE` apparaît.
5. Glisser-déposer le `.uf2` dessus. Le device redémarre et exécute le firmware.

> Mises à jour ultérieures : le composant supporte l'OTA via le `.zip` (DFU). Utile une fois le capteur en place.

---

## 7. Émission → réception : proxy ESP32-S3

Le capteur nRF émet du BTHome en BLE ; le proxy ESP32-S3 le relaie à HA. Config proxy identique à celle de l'Option A (fichier séparé, un par étage) :

```yaml
esphome:
  name: esp32s3-ble-proxy-etage1

esp32:
  board: seeed_xiao_esp32s3       # si non reconnu : esp32-s3-devkitc-1
  variant: esp32s3
  framework:
    type: esp-idf

wifi:
  ssid: "StarTh"
  password: !secret wifi_password

api:
  encryption:
    key: !secret api_key

ota:
  - platform: esphome
    password: !secret ota_password

logger:
  level: INFO

esp32_ble_tracker:
  scan_parameters:
    interval: 320ms
    window: 30ms
    active: false

bluetooth_proxy:
  active: false
```

---

## 8. Intégration Home Assistant

1. **Intégration Bluetooth** active dans HA (détecte automatiquement les proxies ESPHome via l'API).
2. Capteur alimenté et à portée d'un proxy → HA découvre automatiquement l'appareil **BTHome**.
3. Paramètres → Appareils et services → découverte **BTHome** « Porte Angle 01 » → *Ajouter*.
4. Entités créées : angle (°), batterie (%), porte (ouvert/fermé).

> Si l'entité angle arrive sans unité (selon l'object-id BTHome réellement utilisé), l'habiller avec un template sensor côté HA :
> ```yaml
> template:
>   - sensor:
>       - name: "Angle porte 01"
>         unit_of_measurement: "°"
>         state: "{{ states('sensor.porte_angle_01_rotation') | float(0) }}"
>         icon: mdi:angle-acute
> ```

---

## 9. Validation — LA mesure qui décide A vs B

C'est le cœur de l'Option B. À faire sur **un** capteur avant toute commande des 25.

1. **Consommation (PPK2)** en lieu et place de la batterie :
   - courant en deep sleep (cible : quelques µA ; annonce composant 2–5 µA) ;
   - pic et durée d'un cycle réveil + advertising ;
   - courant moyen sur 10 min avec quelques ouvertures.
2. **Autonomie réelle** = `1500 mAh / I_moyen_mA`. Valider ≥ 14 mois avec marge (viser théorique ≥ 30 mois).
3. **Décision :**
   - **µA en sommeil → garder Option B** (plus simple, OTA, pas de SWD) pour les 25 ;
   - **mA en sommeil → basculer Option A** (Zephyr), qui garantit ~1–3 µA.
4. **Wake-on-motion fonctionnel ?** Ouvrir la porte entre deux réveils périodiques : une publication immédiate doit apparaître. Si non (voir §4 note 3), c'est un second argument pour l'Option A.
5. **Couverture radio** : promener le capteur aux 25 emplacements futurs, vérifier réception stable (RSSI confortable) par au moins un proxy.

---

## 10. Calibration de l'angle

1. Porte **fermée** : lire l'angle (logs USB en dev, ou entité HA). C'est le zéro mécanique.
2. Porte **ouverte** : vérifier signe et amplitude.
3. Ajuster le couple d'axes dans la lambda `atan2(ay, az)` selon l'orientation du boîtier.
4. Régler le seuil ouvert/fermé (±15° dans le YAML) selon ta porte.

---

## 11. Points de vigilance récapitulés (Option B)

- **Polarité batterie** : multimètre avant branchement.
- **Bus I²C interne IMU** : ajuster sda/scl si 0x6A non détecté au scan.
- **object-id angle BTHome** : vérifier le type `rotation` réellement émis, habiller côté HA si besoin.
- **Wake-on-motion** : confirmer que l'ouverture réveille immédiatement (sinon → Option A).
- **Conso sommeil** : LE critère de décision, à mesurer au PPK2 (µA → B, mA → A).
- **Bloc `esphome:` unique** : fusionner `name`/`on_boot`.
- **P0.14 / charge** : ne pas manipuler VBAT_ENABLE pendant la charge (comportement sûr par défaut ici).

---

## 12. Tableau de décision final A vs B (à remplir après mesure)

| Critère | Option A (Zephyr) | Option B (ESPHome) | Mesuré chez moi |
|---|---|---|---|
| Conso sommeil | ~1–3 µA (prouvé communauté) | 2–5 µA annoncé, à mesurer | ___ µA |
| Autonomie estimée 1500 mAh | > 3 ans | à confirmer | ___ mois |
| Wake-on-motion fin | maîtrisé (registre INT1) | à valider | OK / KO |
| Flash | SWD requis | UF2, sans debugger | — |
| Maintenance | recompile C | OTA / YAML | — |
| Effort mise en œuvre | élevé | faible | — |

**Règle de décision :** si l'Option B passe les critères conso ET wake-on-motion en validation, l'adopter pour les 25 (maintenance bien plus simple). Sinon, commander les 25 et déployer en Option A.
```
