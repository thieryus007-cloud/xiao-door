# XIAO nRF54LM20A Sense → BLE Proxy → Home Assistant via BTHome v2

**Spécification technique de conception et de mise en œuvre**

Transmettre l'ensemble des données capteur de la carte Seeed XIAO nRF54LM20A Sense — hors microphone — vers Home Assistant, en BLE advertising au format BTHome v2, relayé par un Bluetooth Proxy, avec une consommation optimisée pour un fonctionnement sur batterie de plusieurs années.

**Méthode de vérification.** Les valeurs protocolaires de ce document ne sont pas reprises d'une documentation secondaire : la table des Object IDs, les formats, les longueurs, les facteurs et le comportement du parseur ont été extraits du code de **`bthome-ble` version 3.9.2**, la bibliothèque effectivement exécutée par l'intégration BTHome de Home Assistant. Le comportement du Bluetooth Proxy est établi à partir du dépôt ESPHome. Les points qui n'ont pas pu être vérifiés sur source primaire sont signalés explicitement en §12.

---

## 1. Matériel — Seeed XIAO nRF54LM20A Sense

| Élément | Valeur |
|---|---|
| SoC | Nordic **nRF54LM20A**, Cortex-M33 @128 MHz + coprocesseur RISC-V FLPR 128 MHz |
| Mémoire | 512 KB RAM, ~1,5 MB RRAM (2 MB NVM annoncés) |
| Flash externe | PY25Q64HA, 64 Mbit (8 MB), SPI |
| Radio | **Bluetooth LE 6.0** (+ Channel Sounding), Matter, Thread, Zigbee, 2,4 GHz propriétaire, NFC |
| IMU | **ST LSM6DS3TR-C** — accéléromètre + gyroscope 3 axes, fonctions embarquées (wake-up, free-fall, tap/double-tap, activity/inactivity, 6D/4D orientation, tilt, significant motion), capteur de température interne |
| Micro | MSM261DGT006 (PDM) — **hors périmètre** |
| PMIC | Nordic **nPM1300** (charge Li-Po, LDO/buck, mesure VBAT, NTC, ship mode) |
| Consommation | System OFF **~4,76 µA**, Ship Mode **0,33 µA** (mesures Seeed, batterie 3,7 V) |
| Toolchain | **nRF Connect SDK v3.3.0** ou PlatformIO — Zephyr RTOS |

### 1.1 Alimentation de l'IMU — LDO1 du nPM1300

Sur les variantes **Sense**, l'IMU et le microphone PDM sont alimentés par le **LDO1 du nPM1300**, configuré à **1,8 V** dans les définitions de carte Zephyr standard, alors que les deux périphériques exigent **3,3 V**. Sans correction, le driver échoue au `probe` ou remonte des valeurs aberrantes. Voir l'overlay §5.3.

### 1.2 Limites matérielles

**Pas de capteur de température ambiante.** Seules sont disponibles la température du die du SoC (chauffé par le CPU et la radio) et `OUT_TEMP` du LSM6DS3TR-C (résolution 1/256 °C, précision absolue faible). Aucune n'est une mesure d'ambiance exploitable — la publier en `entity_category: diagnostic`, ou ajouter un SHT4x/BME280 en I²C.

**Pas de magnétomètre**, donc **le yaw absolu n'est pas observable.** Pitch et roll sont absolus (vecteur gravité) ; le yaw dérive par intégration gyroscopique. Pour une application d'angle d'ouvrant, orienter le capteur de sorte que l'axe de rotation soit perpendiculaire à la gravité : l'angle devient alors un pitch ou un roll absolu, stable et sans dérive.

---

## 2. Publicité Étendue (BLE 5) — état réel de la chaîne

C'est la question déterminante pour le dimensionnement des trames : si la Publicité Étendue était utilisable de bout en bout, la contrainte des 31 octets disparaîtrait et une trame unique suffirait. Chaque maillon a donc été évalué séparément.

### 2.1 Rappel de ce qu'apporte la Publicité Étendue

| | Legacy (BLE 4.x) | Étendue (BLE 5.0+) |
|---|---|---|
| Charge utile applicative | **31 octets** | **255 octets** par PDU, jusqu'à **1650 octets** avec chaînage |
| Canaux | 3 canaux primaires (37/38/39) | Annonce sur canaux primaires, données sur 37 canaux secondaires |
| PHY | LE 1M | LE 1M, LE 2M, LE Coded (longue portée) |

### 2.2 Matrice de capacité, maillon par maillon

| Maillon | Publicité Étendue | Statut de la vérification |
|---|---|---|
| **Émetteur** — nRF54LM20A + Zephyr / NCS | **Oui** | Le SoC est BLE 6.0. Zephyr expose l'API `bt_le_ext_adv_create()` / `bt_le_ext_adv_start()` sous `CONFIG_BT_EXT_ADV`. Pas de limitation côté capteur. |
| **Protocole BTHome v2** | **Oui** | Aucune limite de longueur dans la spécification ni dans le parseur. |
| **Parseur `bthome-ble` 3.9.2** (Home Assistant) | **Oui** | Vérifié dans le code : la boucle de décodage itère sur `len(payload)` sans borne supérieure ; aucune constante de type longueur maximale n'existe dans `parser.py`. Un payload de plusieurs centaines d'octets serait décodé normalement. |
| **Bluetooth Proxy ESPHome** (ESP32, y compris **S3 / C3 / C6**) | **Non** | Vérifié sur le dépôt ESPHome. La PR #18047 documente un `grep` de l'arbre : aucune API de Publicité Étendue, aucune publicité périodique, aucun appel `esp_ble_gap_*_ext_*` dans `esp32_ble*` ni dans `bluetooth_proxy`. Cette même PR **désactive explicitement** `CONFIG_BT_BLE_50_FEATURES_SUPPORTED` dans la configuration IDF. Le proxy ne fait que du scan legacy. |
| **Firmware BT-proxy SLZB / SLZB-Ultima** | **Non** | Ce sont des builds ESPHome sur ESP32-S3 : même pile, même limitation. |
| **Adaptateur BLE local sur l'hôte HA** | **Indéterminé** | Non applicable dans l'installation actuelle (voir §2.4). Aucun résultat concluant n'a pu être établi ; le seul retour public trouvé (Raspberry Pi 5, radio BLE 5.0, HA OS) rapporte une non-réception des balises étendues. **À traiter comme non acquis tant que ce n'est pas mesuré.** |

### 2.3 Conséquence sur la conception

La chaîne est limitée par son maillon le plus faible. Tant que la collecte passe par des proxies ESPHome, **la Publicité Étendue n'est pas exploitable** et la charge utile reste de **31 octets par advertising**. Cela impose :

- `CONFIG_BT_EXT_ADV=n` sur le capteur ;
- **PHY LE 1M uniquement** — LE 2M et LE Coded ne sont pas scannés ;
- le découpage en plusieurs trames (§3).

Ce n'est pas une limite du nRF54LM20A. C'est une limite du récepteur, et elle est susceptible d'évoluer : la couche BLE d'ESPHome a été refondue en 2026.8 (composant neutre `ble_device_base`), ce qui rend un support futur plausible sans qu'aucune annonce en ce sens n'existe à ce jour.

### 2.4 Cas particulier de cette installation

Home Assistant tourne en **HAOS sur VMware Fusion**, Mac Mini M4, réseau en pont. Une VM ne dispose d'aucun contrôleur Bluetooth tant qu'un dongle USB ne lui est pas explicitement rattaché. **La totalité du trafic BLE transite donc par les proxies réseau** — il n'y a pas d'adaptateur local qui pourrait servir de porte de sortie vers la Publicité Étendue.

### 2.5 Protocole de test, si la question doit être rouverte

Le point est vérifiable en une demi-heure, sans engager le projet :

1. Flasher un XIAO en Publicité Étendue seule (`CONFIG_BT_EXT_ADV=y`, `bt_le_ext_adv_*`, payload BTHome de ~60 octets), PHY primaire 1M.
2. Confirmer l'émission avec **nRF Connect** sur mobile — l'application distingue legacy et étendu.
3. Ouvrir dans Home Assistant : *Paramètres → Appareils et services → Bluetooth → Advertisement Monitor*, et observer si l'adresse remonte.
4. Réémettre le même payload en legacy tronqué à 31 octets pour valider que le capteur est bien à portée du proxy.

Si l'étape 3 échoue alors que l'étape 4 réussit, le verdict de §2.2 est confirmé sur l'installation réelle.

### 2.6 Alternatives, si le besoin de payload dépasse durablement 31 octets

Aucune n'est recommandée en l'état ; elles sont listées pour être écartées en connaissance de cause.

| Piste | Réalité |
|---|---|
| Plusieurs trames legacy | **Retenu.** Sans coût matériel, sans dépendance. Détaillé en §3. |
| Scan response | Inutilisable : elle exige un scan actif et des PDU scannables. Les proxies dédiés capteurs sont en scan passif, et une PDU scannable coûte de l'énergie au capteur. |
| Dongle USB BLE 5 rattaché à la VM HAOS | Techniquement envisageable, mais le comportement de BlueZ sur la Publicité Étendue n'est pas établi (§2.2) et la portée depuis le Mac Mini ne couvrirait pas 4 niveaux. |
| Passer de BTHome à une connexion GATT | Résout la taille, mais détruit le budget énergétique et consomme un slot de connexion sur le proxy (3 par défaut, 5 conseillés au maximum) pour 25 capteurs. |
| Passer les capteurs sur Thread/Matter | Le nRF54LM20A le supporte, et 4 OTBR sont déjà déployés sur le site. C'est la vraie alternative structurelle si le besoin de données dépasse durablement ce que BTHome peut porter — mais c'est un autre projet : consommation, mise en service et modèle de données n'ont rien à voir. |

---

## 3. Format BTHome v2

### 3.1 Structure de la trame BLE

```
[ AD Flags ] [ AD Local Name (optionnel) ] [ AD Service Data 16-bit UUID ]
```

| Élément | Octets | Contenu |
|---|---|---|
| AD Flags | 3 | `02 01 06` — indispensable. BlueZ, utilisé par l'intégration Bluetooth de HA, ne parse pas l'advertising en scan passif sans ce champ. |
| AD Local Name | 2 + N | `LL 09 <ascii>` (complet) ou `LL 08 <ascii>` (abrégé) |
| AD Service Data | 4 + M | `LL 16 D2 FC <device_info> <mesures…>` — UUID `0xFCD2` en little-endian |

**Budget total : 31 octets** (advertising legacy, cf. §2).

### 3.2 Octet Device Info

| Bit | Signification | Valeur retenue |
|---|---|---|
| 0 | Chiffrement | 0 (clair) ou 1 (AES-CCM) |
| 1 | **MAC incluse** dans le payload (6 octets) | 0 — inutile ici |
| 2 | **Trigger-based device** | **1** |
| 3-4 | Réservé | 0 |
| 5-7 | Version BTHome | `010` = v2 |

> Le bit 1 est décrit comme réservé sur le site BTHome, mais il est bien implémenté dans le parseur (`_is_mac_included`) : positionné, il fait interpréter les 6 premiers octets du payload comme une adresse MAC. Il doit donc rester à 0.

| Valeur | Sens |
|---|---|
| `0x40` | v2, clair, périodique |
| **`0x44`** | **v2, clair, trigger-based ← retenu pour toutes les trames** |
| `0x41` | v2, chiffré, périodique |
| `0x45` | v2, chiffré, trigger-based |

**Pourquoi `0x44` sur toutes les trames.** Le parseur exécute `self.sleepy_device = bthome_data.is_sleepy_device()` à **chaque** advertising reçu : l'état « sleepy » est donc celui de la **dernière trame reçue**, pas une propriété stable de l'appareil. Alterner `0x40` et `0x44` sur une même adresse MAC fait donc basculer cet indicateur au rythme des trames, et Home Assistant applique alors par intermittence sa fenêtre d'indisponibilité de 5 minutes aux devices non sleepy. Valeur constante `0x44` partout.

### 3.3 Comportement réel du parseur — trois règles à connaître

Ces trois points ont été relevés directement dans `parser.py` de `bthome-ble` 3.9.2.

**a) Object ID inconnu → arrêt du décodage.**
```python
if obj_meas_type not in MEAS_TYPES:
    _LOGGER.debug("Invalid Object ID found in payload: %s", ...)
    break
```
Toutes les mesures situées **après** un ID inconnu sont perdues silencieusement (message en niveau `debug` seulement). C'est le principal piège d'un payload construit à la main.

**b) Object IDs hors ordre croissant → avertissement, mais le décodage continue.**
```python
if prev_obj_meas_type > obj_meas_type:
    _LOGGER.warning("BTHome device is not sending object ids in numerical order ...")
```
L'ordre croissant reste **exigé par la spécification** et doit être respecté — d'autres récepteurs BTHome peuvent, eux, interrompre le décodage — mais côté Home Assistant l'effet immédiat se limite à un avertissement dans le journal.

**c) Déduplication par Packet ID — règle exacte.**

| Condition | Résultat |
|---|---|
| Premier paquet reçu (pas d'historique) | Accepté |
| Plus de **4 s** depuis le dernier advertising | **Accepté**, même packet id identique |
| Écart croissant < 64 (rollover 255→0 géré) | Accepté |
| Packet id identique, ou plus ancien, à moins de 4 s | **Rejeté** |

Le commentaire du code précise l'hypothèse : le capteur n'émet pas plus de 16 jeux de données par seconde. Deux conséquences directes pour la conception :

- **Le train de répétitions à packet id constant est correct et voulu** : les copies sont rejetées, c'est exactement leur rôle (redondance de réception).
- **Deux jeux de données distincts émis à moins de 4 s d'intervalle doivent avoir des packet id différents**, sinon le second est perdu.

### 3.4 Autres règles du format

1. **Little-endian** pour toutes les valeurs multi-octets.
2. **Mesures multiples du même type** : suffixées côté HA (`rotation`, `rotation_2`, `rotation_3`) **dans l'ordre de la trame**. Cet ordre doit être identique à chaque advertising.
3. **Binary sensors** : 1 octet, `0x00` / `0x01`.

### 3.5 Object IDs — table extraite de `bthome-ble` 3.9.2

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
| `0x51` | acceleration | **non signé** | 2 | 0.001 | m/s² |
| `0x52` | gyroscope | **non signé** | 2 | 0.001 | °/s |
| `0x57` | temperature | signé | 1 | 1 | °C |
| `0x5A` | count | signé | 2 | 1 | — |
| `0x5B` | count | signé | 4 | 1 | — |
| `0x62` | speed | signé | 4 | 0.000001 | m/s |
| `0x63` | **acceleration (signée)** | signé | 4 | 0.000001 | m/s² |
| `0x64` | light level | non signé | 1 | 1 | — |
| `0x65` | settings revision | non signé | 1 | 1 | — |
| `0xF2` | firmware version | 3 octets | 3 | — | x.y.z |

> **Il n'existe aucun gyroscope signé dans BTHome v2.** Vérifié par énumération complète de `MEAS_TYPES` : `0x52` est le seul objet gyroscope, non signé, plage 0 → 65,535 °/s. `0x63` fournit une accélération signée, mais aucun objet symétrique n'existe pour la vitesse angulaire.

**Capteurs binaires** — 1 octet chacun

| ID | Grandeur | 0 / 1 |
|---|---|---|
| `0x0F` | generic | Off / On |
| `0x11` | opening | Closed / Open |
| `0x15` | battery (low) | Normal / Low |
| `0x16` | **battery charging** | Non / En charge |
| `0x1A` | door | Closed / Open |
| `0x21` | motion | Clear / Detected |
| `0x22` | moving | Non / Oui |
| `0x2B` | tamper | Off / On |
| `0x2C` | vibration | Clear / Detected |
| `0x2D` | window | Closed / Open |

**Événements**

| ID | Type | Valeurs |
|---|---|---|
| `0x3A` | button | `0x00` none · `0x01` press · `0x02` double_press · `0x03` triple_press · `0x04` long_press · `0x05` long_double_press · `0x06` long_triple_press · `0x80` hold_press |
| `0x3C` | dimmer | 2 octets |

### 3.6 Correspondances retenues pour les événements IMU

BTHome ne définit pas d'objet natif pour « free fall », « double tap » ou « activity ». Mapping retenu :

| Événement LSM6DS3TR-C | Object ID | Entité HA | Renommage |
|---|---|---|---|
| Wake-up / motion | `0x21` motion | binary_sensor | *Mouvement* |
| Activity / Inactivity | `0x0F` generic | binary_sensor | *Activité* |
| Free-fall / choc | `0x2B` tamper | binary_sensor | *Chute / Choc* |
| Double-tap | `0x2C` vibration | binary_sensor | *Double-tap* |
| Bouton | `0x3A` button | event | *Bouton* |
| Pitch / Roll / Yaw | `0x3F` ×3 | sensor ×3 | *Pitch* / *Roll* / *Yaw* |

**Remise à zéro des binaires.** BTHome est sans état : un `motion = 1` envoyé une fois reste à `on` indéfiniment. Il faut émettre une trame de retour au repos, tous les binaires à `0`, après le timeout d'événement (5 à 30 s).

---

## 4. Architecture de trames

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

### 4.1 TRAME A — Événement de mouvement + orientation

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

### 4.2 TRAME B — Périodique (batterie / santé), 15 min

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

Nom limité à **7 caractères** dans cette configuration. Pour un nom plus long, supprimer `0x15` (dérivable côté HA d'un seuil sur `0x01`) : 2 caractères libérés par objet binaire retiré.

### 4.3 TRAME C — IMU brut (optionnelle)

Transmettre des échantillons `accel`/`gyro` instantanés dans un beacon apporte peu : ce sont des valeurs ponctuelles, non corrélées temporellement, sans horodatage. L'information utile est déjà calculée à bord et portée par la trame A.

Si la trame est conservée, deux contraintes de format s'imposent :

- **`0x51` et `0x52` sont non signés** : seules des **magnitudes** peuvent y être portées. Pour des composantes signées, `0x63` existe pour l'accélération (signé, 4 octets) ; **il n'y a pas d'équivalent pour le gyroscope** (§3.5).
- **`0x52` sature à 65,535 °/s** alors que le LSM6DS3TR-C monte à ±2000 °/s. Clamper, ou configurer le gyroscope en ±125 dps.

| Ordre | Object ID | Donnée | Octets |
|---|---|---|---|
| 1 | `0x00` | Packet ID | 2 |
| 2–4 | `0x51` ×3 | \|a_x\|, \|a_y\|, \|a_z\| | 9 |
| 5–7 | `0x52` ×3 | \|ω_x\|, \|ω_y\|, \|ω_z\| | 9 |
| | | **Total mesures** | **20** |

**Budget : 3 + 25 = 28 / 31 octets.**

**Variante recommandée** : n'envoyer que **‖a‖** et **‖ω‖** (`0x51` ×1, `0x52` ×1), soit 6 octets — 14 octets libres pour le nom, information directement interprétable.

### 4.4 Variante chiffrée (AES-CCM)

Le chiffrement ajoute **8 octets** (compteur 4 + MIC 4). Le compteur remplace fonctionnellement le Packet ID, qui est supprimé.

| | Clair | Chiffré (`0x45`) |
|---|---|---|
| Mesures max, sans nom | 23 o. | **15 o.** |
| Mesures max, nom 7 caractères | 14 o. | 6 o. |

**Trame A chiffrée réalisable** (15 octets) : `0x21` (2) + `0x2B` (2) + `0x2C` (2) + `0x3F` ×3 (9). Il faut renoncer à `0x0F` et `0x3A`.

Clé = **16 octets / 32 caractères hexadécimaux** (*bindkey* demandé par Home Assistant).

Sur un réseau domestique maîtrisé, le chiffrement protège surtout contre l'**usurpation** (rejeu, injection de faux angles). Compromis proposé : clair pour la mise au point, chiffré en production sur les ouvrants donnant sur l'extérieur.

---

## 5. Stratégie d'émission et politique de réveil

### 5.1 Événements déclencheurs (trame A)

| Déclencheur | Source | Mise en œuvre |
|---|---|---|
| Réveil sur mouvement | LSM6DS3TR-C `WAKE_UP` → INT1 | Seuil `WK_THS`, durée `WAKE_DUR` |
| Franchissement angulaire | Calcul embarqué | Hystérésis ±2° |
| Free-fall / choc | LSM6DS3TR-C `FREE_FALL` → INT1 | — |
| Double-tap | LSM6DS3TR-C `TAP_SRC` → INT1 | `TAP_CFG` / `INT_DUR2` |
| Inactivity | LSM6DS3TR-C `SLEEP_CHANGE` | Remet tous les binaires à 0 |
| Bouton | GPIO | Anti-rebond ≥ 30 ms |

### 5.2 Train d'advertising

Un capteur qui n'émet qu'un seul advertising event a une probabilité de réception faible : le proxy n'écoute qu'un canal primaire à la fois.

| Paramètre | Valeur | Justification |
|---|---|---|
| Intervalle d'advertising | **100 ms** | Chaque event balaie les 3 canaux primaires |
| Durée du train | **700 ms** | ≈ 7 events, packet id **constant** sur le train |
| Type de PDU | `ADV_NONCONN_IND` | Ni scan request, ni connexion |
| PHY | **LE 1M** | LE 2M / Coded non scannés (§2) |
| Advertising | **Legacy** | §2 |

Coût radio : ~7 events × 3 canaux × ~1 ms ≈ **21 ms de TX**, de l'ordre de **0,1 mAs** par événement à 0 dBm.

### 5.3 Anti-rebond et limitation de débit

| Règle | Valeur |
|---|---|
| Intervalle minimum entre deux trames A | **4 s** |
| Trames A max par minute | **10** (fenêtre glissante) |
| Envoi angulaire si Δ > | **2,0 °** sur pitch ou roll |
| Trame « repos » après inactivité | **15 s** |
| Trame B | **15 min** ± 30 s de jitter aléatoire |
| Heartbeat (trame A état repos) | **60 min** sans événement |

> L'intervalle minimum est porté à **4 s** — et non 2 s — pour s'aligner sur la fenêtre de déduplication du parseur (§3.3c). En deçà de 4 s, deux jeux de données distincts ne sont acceptés que si leurs packet id diffèrent ; avec un compteur correctement incrémenté c'est le cas, mais 4 s supprime toute dépendance à cette subtilité et réduit la charge radio.

Le **jitter** sur la trame B est important avec 25 capteurs : sans lui, des nœuds redémarrés simultanément après une coupure se synchronisent et émettent tous dans la même seconde toutes les 15 min.

### 5.4 Bilan énergétique prévisionnel

| Poste | Courant moyen estimé |
|---|---|
| nRF54LM20A, System ON idle (GRTC actif) | 5 – 10 µA |
| LSM6DS3TR-C, accéléromètre seul low-power @26 Hz | 10 – 50 µA *(à mesurer)* |
| nPM1300, courant de repos | 5 – 15 µA *(à mesurer)* |
| Radio (trames A+B moyennées) | < 5 µA |
| **Total estimé** | **~25 – 80 µA** |

Sur une LiPo 1000 mAh : **1,5 à 4 ans** théoriques, à réduire d'environ 30 % (autodécharge, froid). Sur un ouvrant très sollicité, viser **1 an**.

**Régime retenu : System ON + Power Management Zephyr, pas System OFF.** Sur nRF54, la sortie de System OFF redémarre depuis `main()` sans état retenu : compteur Packet ID, calibration d'offset et contexte BLE seraient perdus à chaque réveil, pour un gain d'environ 5 µA marginal face à l'IMU qui doit rester alimentée. System OFF et Ship Mode restent réservés au stockage et au transport.

---

## 6. Firmware — nRF Connect SDK / Zephyr

### 6.1 Driver de l'IMU

Zephyr **ne fournit pas de driver nommé `lsm6ds3tr_c`.** Le LSM6DS3TR-C est piloté par le driver **LSM6DSL**, dont il est un équivalent registre pour les fonctions utilisées ici — c'est ce que fait la propre documentation Zephyr de Seeed pour le XIAO nRF52840 Sense, avec un nœud `lsm6ds3tr_c:` déclaré `compatible = "st,lsm6dsl"`.

```conf
CONFIG_LSM6DSL=y
CONFIG_LSM6DSL_TRIGGER_OWN_THREAD=y
```

Le **nom du nœud et la chaîne `compatible` réellement utilisés pour le XIAO nRF54LM20A** doivent être relevés dans le DTS de carte livré par Seeed / Zephyr (`boards/seeed/xiao_nrf54lm20a/`) : ils conditionnent le label à référencer dans l'overlay et le symbole Kconfig à activer. C'est le premier point à vérifier avant la première compilation.

### 6.2 Adresse BLE

Home Assistant identifie un device BTHome **par son adresse MAC**. Une adresse qui change au redémarrage crée un nouveau device à chaque reboot ; sur 25 capteurs, l'inventaire devient ingérable.

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
       le plus significatif doivent valoir 0b11. */
    addr.a.val[5] |= 0xC0;

    return bt_id_create(&addr, NULL);
}
```

Déterministe, reproductible, sans dépendance au stockage de configuration ni usure de la RRAM. Alternative : `CONFIG_BT_SETTINGS=y` avec un backend NVS/ZMS — fonctionnel, mais dépendant de l'intégrité du stockage et plus contraignant pour un reflashage de masse.

### 6.3 `prj.conf`

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
CONFIG_BT_EXT_ADV=n            # advertising legacy imposé par la chaîne proxy (§2)

# --- Identité ---
CONFIG_HWINFO=y

# --- Capteurs ---
CONFIG_SENSOR=y
CONFIG_I2C=y
CONFIG_LSM6DSL=y               # driver du LSM6DS3TR-C (§6.1)
CONFIG_LSM6DSL_TRIGGER_OWN_THREAD=y

# --- PMIC ---
CONFIG_REGULATOR=y
CONFIG_MFD=y
CONFIG_ADC=y

# --- Power management ---
CONFIG_PM=y
CONFIG_PM_DEVICE=y

# --- Crypto (uniquement si chiffrement BTHome) ---
# CONFIG_PSA_WANT_ALG_CCM=y
# CONFIG_PSA_WANT_KEY_TYPE_AES=y

# --- Divers ---
CONFIG_MAIN_STACK_SIZE=2048
CONFIG_LOG=y
CONFIG_LOG_DEFAULT_LEVEL=2
```

### 6.4 `app.overlay` — LDO1 à 3,3 V

Structure reprise d'un projet publié fonctionnant sur cette carte exacte (XIAO nRF54LM20A Sense sous Zephyr) :

```dts
/* Bus I2C du PMIC */
&pmic_i2c {
    sda-gpios = <&gpio1 18 GPIO_ACTIVE_HIGH>;
    scl-gpios = <&gpio1 17 GPIO_ACTIVE_HIGH>;
    status = "okay";
};

/* LDO1 alimente l'IMU et le micro PDM : 3,3 V requis (défaut 1,8 V) */
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

Il faut ensuite garantir que le rail est établi **avant** que le driver du capteur ne sonde le bus. Deux moyens, au choix selon ce que permet le DTS de carte :

- priorité d'initialisation du régulateur supérieure à celle du capteur (`CONFIG_SENSOR_INIT_PRIORITY`), ou
- initialisation différée du nœud capteur (`zephyr,deferred-init`) et appel explicite de `device_init()` après `regulator_enable()`.

```c
const struct device *imu = DEVICE_DT_GET(DT_NODELABEL(lsm6ds3tr_c));  /* label à confirmer, §6.1 */

/* variante init différée */
k_msleep(20);          /* montée du rail + boot du LSM6DS3TR-C */
device_init(imu);
```

### 6.5 Construction du payload BTHome

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

/* --- TRAME A : événement + orientation --- */
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

### 6.6 Émission du train d'advertising

```c
#include <zephyr/bluetooth/bluetooth.h>

/* 100 ms en unités de 0,625 ms */
#define ADV_INT      0x00A0
#define TRAIN_MS     700

/* Non connectable, non scannable, adresse d'identité (pas de RPA). */
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

/* Incrémenté uniquement entre deux jeux de données distincts. */
static void bthome_next_packet(void) { bthome_pid++; }
```

Points de vigilance :

- `BT_LE_ADV_OPT_USE_IDENTITY` est indispensable : sans lui, Zephyr peut employer une adresse privée résolvable tournante, ce qui créerait un nouveau device HA à chaque rotation. **Le nom exact de ce symbole a évolué entre versions de Zephyr** — le confirmer dans `zephyr/include/zephyr/bluetooth/bluetooth.h` de la version de NCS installée.
- Ne pas ajouter d'option connectable ni scannable : la PDU visée est `ADV_NONCONN_IND`.
- Ne pas activer `BT_LE_ADV_OPT_EXT_ADV` (§2).
- `bt_le_adv_stop()` en fin de train est ce qui rend le montage économe : l'advertising ne tourne pas en continu.

### 6.7 Calcul pitch / roll

Avec accéléromètre seul, en statique — l'ouvrant à l'arrêt, moment où l'angle est pertinent :

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

- Moyenner **8 à 16 échantillons** ; sinon le bruit se traduit par ±1° de gigue.
- Ne calculer qu'après stabilisation (‖a‖ ≈ 9,81 ± 0,3 m/s² pendant ≥ 200 ms) : l'accélération dynamique fausse l'estimation.
- Prévoir un **offset de calibration par capteur** en RRAM, réglable depuis HA (position fermée = 0°).
- Le **yaw** dérive : le remettre à zéro à chaque inactivité, ou ne pas l'exposer.

---

## 7. Chaîne de transmission — le BLE Proxy

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

### 7.1 Configuration ESPHome — proxy dédié aux capteurs

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
  baud_rate: 0

api:
  encryption:
    key: !secret api_key

ota:
  - platform: esphome
    password: !secret ota_password

wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password
  power_save_mode: none

esp32_ble_tracker:
  scan_parameters:
    active: false        # scan passif : pas de SCAN_REQ.
                         # Inutile ici (PDU non scannables) et réduit
                         # le bruit RF pour les autres capteurs du site.
    interval: 1000ms
    window: 900ms

bluetooth_proxy:
  active: false          # proxy dédié aux advertisements.
                         # Réserver active: true à 1 ou 2 proxies pour
                         # les connexions GATT (serrures, SwitchBot, etc.)
```

**Notes de version — ESPHome 2026.8.0 :**

- La couche d'advertisements BLE a été extraite dans un composant neutre `ble_device_base` ; `bluetooth_proxy` n'est plus réservé à l'ESP32.
- La clé `esp32_ble_id` est renommée **`ble_hub_id`** (alias déprécié jusqu'à 2027.2.0).
- **Le paramètre `window` par défaut a changé** : avec la coexistence Wi-Fi sur ESP-IDF 5.5.5 ou plus récent, `window` prend désormais par défaut la valeur de `interval`, conformément à la recommandation d'Espressif. Un `window` explicite n'est jamais modifié. La valeur `900ms` ci-dessus est donc un choix délibéré (90 % de rapport cyclique) ; laisser le champ vide donne 100 %.

### 7.2 Dimensionnement du parc

| Contrainte | Valeur | Remarque |
|---|---|---|
| Capteurs passifs par proxy | 20 – 40 | Au-delà : `Too many BLE events to process` |
| Connexions GATT par proxy | 3 par défaut, 9 max | Ne pas dépasser **5** (stabilité mémoire) |
| Portée pratique intérieure | 8 – 15 m / 1 dalle | Béton armé : 1 proxy par niveau minimum |

Pour **25 capteurs sur 4 niveaux**, **4 proxies (1 par étage)** sont cohérents. La redondance entre étages est un atout : Home Assistant retient la meilleure source par RSSI et déduplique via le Packet ID.

Pour les proxies critiques, privilégier une liaison **Ethernet**. BLE et Wi-Fi partagent la radio 2,4 GHz de l'ESP32 ; libérer la partie Wi-Fi augmente le taux de capture et supprime le compromis sur `window`.

### 7.3 Alternative matérielle au proxy ESPHome

Depuis **Home Assistant 2026.8**, les SMLIGHT **SLZB série U** sous SLZB-OS peuvent servir d'adaptateur Bluetooth distant **nativement**, tout en restant coordinateur Zigbee ou Thread Border Router, sur Ethernet, sans reflashage en ESPHome. Le mode de scan se choisit dans les options de l'intégration SMLIGHT.

Deux réserves à lever avant d'y compter :

1. Le site utilise des **SMLIGHT Nano HUB** comme OTBR, pas des SLZB série U. La fonction est liée à un modèle précis — vérifier la référence exacte de chaque unité déployée.
2. Ces appareils sont eux aussi bâtis autour d'un ESP32-S3 ; **rien n'indique qu'ils lèvent la limitation Publicité Étendue de §2**. À considérer comme un moyen de mutualiser le matériel et de passer les proxies sur Ethernet, pas comme un moyen d'augmenter la taille de charge utile.

Les SMLIGHT ne relaient pas les connexions GATT actives : ce sont des proxies d'advertisements uniquement.

### 7.4 Limites de la chaîne proxy

| Attente | Réalité |
|---|---|
| Recevoir l'advertising étendu | Non (§2) |
| Recevoir la scan response en scan passif | Non — aucun SCAN_REQ n'est émis. Toute donnée placée en scan response est perdue ; le nom est donc porté par l'advertising de la trame B. |
| Filtrer les advertisements avant transmission | Non : tout est relayé, le tri se fait dans HA |
| Décoder BTHome | Non : décodage par `bthome-ble` dans HA |

---

## 8. Intégration Home Assistant

### 8.1 Découverte

1. Intégration **Bluetooth** active, proxies adoptés.
2. À la première trame BTHome valide, le device apparaît en **Découvert** dans *Paramètres → Appareils et services*.
3. À défaut : *Ajouter une intégration → BTHome*.
4. Si le chiffrement est activé, HA demande le **bindkey** (32 caractères hexadécimaux).

### 8.2 Entités générées

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

Les entités **persistent entre les trames** : HA fusionne les mises à jour partielles, une entité absente d'une trame conserve sa dernière valeur. Le découpage en 3 trames est transparent côté interface.

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

Le heartbeat horaire (§5.3) rend cette supervision exploitable : sans lui, « ouvrant immobile depuis 3 jours » et « capteur en panne » sont indiscernables.

---

## 9. Procédure de mise en œuvre

### Phase 0 — Levée des inconnues matérielles

1. Relever dans `boards/seeed/xiao_nrf54lm20a/` le **label du nœud IMU**, sa **chaîne `compatible`** et le **GPIO d'INT1** (§6.1).
2. Compiler un `hello_world` puis un exemple capteur, et confirmer la lecture de l'IMU après application de l'overlay §6.4.

### Phase 1 — Validation unitaire (1 capteur, 1 proxy)

3. Flasher avec **trame B uniquement**, en clair, toutes les **30 s** (cadence accélérée pour la mise au point).
4. Vérifier avec **nRF Connect** : UUID `0xFCD2`, device info `0x44`, adresse **stable après reboot**.
5. Vérifier la découverte du device BTHome dans HA.
6. Redémarrer le capteur 3 fois : aucun nouveau device ne doit apparaître. Sinon, l'identité BLE n'est pas fixée (§6.2).
7. Passer la trame B à 15 min et confirmer l'absence de passage en `unavailable` au bout de 5 min. Sinon, le bit trigger-based n'est pas positionné.

### Phase 2 — Trame A

8. Ajouter la trame A avec pitch/roll/yaw seuls, sur seuil angulaire.
9. Contrôler que `Rotation`, `Rotation 2` et `Rotation 3` correspondent à pitch, roll, yaw et ne permutent jamais.
10. Ajouter les binaires et l'événement bouton.
11. Vérifier le retour à `0` des binaires après le timeout d'inactivité.
12. Activer le journal `debug` sur `bthome_ble` le temps d'une session et confirmer l'absence de message `Invalid Object ID found in payload` (§3.3a) et de `not sending object ids in numerical order` (§3.3b).

### Phase 3 — Énergie

13. Mesurer la consommation moyenne sur 24 h (Nordic Power Profiler Kit II ou nPM1300 EK).
14. Ajuster les seuils IMU et le taux de rafraîchissement.
15. Valider l'écart au budget §5.4 avant de commander les 22 capteurs restants.

### Phase 4 — Déploiement sur 4 niveaux

16. Déployer 1 proxy par niveau (§7.1).
17. Déployer les capteurs par lot de 5, nom `SANT-XX` unique.
18. Vérifier le RSSI de chaque capteur (*Bluetooth → Advertisement Monitor*). Cible : **> −85 dBm** sur au moins un proxy.
19. Calibrer l'offset angulaire de chaque ouvrant en position fermée.
20. Générer les templates §8.3 et les pousser dans `thieryus007-cloud/HA-Santuario`.

### Phase 5 — Durcissement (optionnel)

21. Activer le chiffrement AES-CCM sur les ouvrants extérieurs, payload réduit (§4.4).
22. Mettre en place la supervision §8.4 sur les 25 capteurs.

---

## 10. Diagnostic

| Symptôme | Cause probable | Action |
|---|---|---|
| Device jamais découvert | Flags AD absents | Ajouter `02 01 06` |
| Device jamais découvert | Advertising étendu utilisé | `CONFIG_BT_EXT_ADV=n` (§2) |
| Découvert, mais mesures partielles | Object ID inconnu dans la trame | Journal `debug` de `bthome_ble` : `Invalid Object ID found in payload`. Tout ce qui suit cet ID est perdu. |
| Avertissement d'ordre dans le journal | IDs non croissants | Réordonner. Le décodage continue côté HA, mais la spec l'exige et d'autres récepteurs peuvent s'arrêter. |
| Nouveau device HA à chaque reboot | Adresse BLE non fixée | `bt_id_create()` + option d'identité, `CONFIG_BT_PRIVACY=n` (§6.2) |
| Device `unavailable` toutes les 5 min | Device info `0x40` au lieu de `0x44` | Bit 2 = 1 sur toutes les trames |
| État `unavailable` intermittent | Mélange `0x40` / `0x44` sur la même MAC | Uniformiser sur `0x44` (§3.2) |
| Un événement sur deux ignoré | Deux jeux de données à < 4 s avec le même packet id | Incrémenter le compteur entre jeux distincts (§3.3c) |
| Motion reste `on` indéfiniment | Pas de trame de retour au repos | Émettre une trame A tous binaires à `0` |
| Pitch / Roll / Yaw permutés | Ordre d'insertion variable | Insérer toujours les 3 `0x3F` dans le même ordre |
| Accélération toujours positive | `0x51` est non signé | Envoyer une magnitude, ou utiliser `0x63` |
| Gyroscope plafonne à 65,5 | Saturation `0x52` | Clamper, ou limiter le range gyro à ±125 dps |
| Événements manqués | Un seul adv event émis | Train de 700 ms (§5.2) |
| Événements manqués | `window` trop petit côté proxy | Voir §7.1, en tenant compte du changement de défaut en 2026.8 |
| `Too many BLE events to process` | Proxy saturé | Ajouter un proxy, ou passer en Ethernet |
| Proxy instable, Wi-Fi qui décroche | Rapport cyclique de scan trop élevé | Réduire `window`, ou passer en Ethernet |
| IMU non détectée / WHO_AM_I invalide | LDO1 à 1,8 V, ou mauvais driver | Overlay §6.4 ; driver `CONFIG_LSM6DSL` (§6.1) |
| Angles bruités ±1° | Pas de moyennage | 8 à 16 échantillons + attente de stabilité (§6.7) |
| Tous les capteurs émettent en même temps | Pas de jitter sur la trame B | ±30 s d'aléa (§5.3) |

---

## 11. Décisions retenues

| Sujet | Décision |
|---|---|
| Publicité Étendue | **Écartée** — non supportée par la chaîne de réception (§2). Réévaluable par le test §2.5. |
| Protocole | BLE advertising legacy 1M, BTHome v2, UUID `0xFCD2`, 31 octets |
| Device info | **`0x44`** sur toutes les trames |
| Nombre de trames | **3** — A (événement + angles), B (batterie/santé + nom, 15 min), C (IMU brut, optionnelle) |
| Packet ID | Compteur unique partagé, constant sur un train, incrémenté entre jeux distincts |
| Angles | `0x3F` ×3 (signé, 0.1°) — pitch, roll, yaw dans cet ordre |
| Accéléro / gyro bruts | Optionnels, magnitudes uniquement ; aucun gyroscope signé n'existe |
| Adresse BLE | Statique aléatoire dérivée de `hwinfo_get_device_id()` |
| Driver IMU | `CONFIG_LSM6DSL` (`st,lsm6dsl`), label et INT1 à confirmer sur le DTS de carte |
| Régime d'alimentation | System ON + PM Zephyr, réveil sur INT1 |
| Train d'advertising | 100 ms d'intervalle, 700 ms de durée |
| Chiffrement | Optionnel — 8 octets, payload réduit à 15 o. sans nom |
| Proxies | 4 × ESP32-S3 ESPHome, esp-idf, scan passif ; SLZB série U en alternative (§7.3) |
| Microphone PDM | Exclu du périmètre |

---

## 12. Éléments non vérifiés sur source primaire

Ces points reposent sur une source secondaire ou nécessitent une mesure. Ils sont à traiter comme des hypothèses de travail, non comme des acquis.

| # | Élément | Nature de l'incertitude |
|---|---|---|
| 1 | Label du nœud IMU, chaîne `compatible` et GPIO d'INT1 sur `xiao_nrf54lm20a` | Le DTS de carte n'a pas été consulté. Bloquant pour la première compilation — traité en phase 0. |
| 2 | Nom exact des symboles Zephyr `BT_LE_ADV_OPT_USE_IDENTITY` et `BT_LE_ADV_PARAM_INIT` | Ces symboles ont évolué entre versions de Zephyr. À confirmer dans le SDK installé. |
| 3 | Consommation réelle du LSM6DS3TR-C et du nPM1300 en veille | Fourchette estimée (§5.4), non mesurée. |
| 4 | Persistance du nom local appris via la trame B | Comportement du cache de noms de HA non vérifié dans le code. Si le nom ne s'applique pas, renommer une fois dans l'interface — impact cosmétique. |
| 5 | Comportement de BlueZ sur la Publicité Étendue avec un dongle BLE 5 | Non testé. Sans objet dans l'installation actuelle (§2.4). |
| 6 | Capacité BLE proxy des SMLIGHT Nano HUB du site | Fonction documentée pour la série SLZB U ; référence exacte des unités déployées à vérifier (§7.3). |
| 7 | Pinout `pmic_i2c` de l'overlay §6.4 | Repris d'un projet publié sur cette carte, non recoupé avec le schéma Seeed. |

Le reste — table des Object IDs, formats, longueurs, facteurs, signes, comportement du parseur sur les IDs inconnus, l'ordre et la déduplication, indicateur sleepy, absence de limite de longueur de payload, absence de support Publicité Étendue dans ESPHome, budgets d'octets — est établi sur code source ou spécification, et recalculé octet par octet.

---

## Sources

**Code source consulté directement**
- `bthome-ble` 3.9.2 (`parser.py`, `const.py`) — bibliothèque exécutée par l'intégration BTHome de Home Assistant

**Spécifications et documentation officielles**
- BTHome v2 — `https://bthome.io/format/`
- Home Assistant — intégration BTHome, intégration SMLIGHT SLZB
- Seeed Studio Wiki — XIAO nRF54LM20A Sense ; XIAO nRF52840 avec Zephyr (déclaration `st,lsm6dsl`)
- Zephyr Project — boards `seeed/xiao_nrf54lm20a` et `seeed/xiao_ble`
- ESPHome — `bluetooth_proxy`, `esp32_ble_tracker`, changelog 2026.8.0
- Bluetooth SIG — « Exploring Bluetooth Core 5.0: What's new in advertising »

**Discussions et suivis de développement**
- esphome/esphome#18047 — désactivation explicite de BLE 5.0, absence d'API de Publicité Étendue dans l'arbre
- esphome/esphome#10626 — Bluetooth Proxy et balises en advertising étendu
- home-assistant/core#78702 / #79669 — `UNAVAILABLE_TRACK_SECONDS` et devices « sleepy »
- SMLIGHT — SLZB en adaptateur Bluetooth distant avec Home Assistant 2026.8
