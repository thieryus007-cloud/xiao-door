
Objectif final de cette phase
Avoir un appareil Matter over Thread sur réseau existant qui :
	•	Apparaît dans Home Assistant comme un Door Lock (ou Contact Sensor)
	•	Remonte les données 6 axes de l’IMU (accélération + gyroscope + orientation)
	•	Remonte les informations concernant la batterie.
	•	Utilise l’accélération pour le wake-up (sortie de sommeil profond)
	•	Utilise le NFC et/ou BLE pour le commissioning (et éventuellement un usage simple)
	•	implementer Aliro Home key si possible


Prérequis
	•	XIAO nRF54LM20A Sense
	•	Antenne NFC soudée (pads N1 / N2)
	•	Antenne 2.4 GHz (recommandée)
	•	nRF Connect SDK (dernière version stable supportant nRF54LM20A)
	•	VS Code + extension nRF Connect
	•	Réseau Matter over Thread déjà fonctionnel et connu de Home Assistant: 3 OTBR deja en place réseau. Santuario.
	•	Home Assistant avec l’intégration Matter active

Étape 0 – Préparation de l’environnement
	1	Installer / mettre à jour nRF Connect SDK (via Toolchain Manager ou west).
	2	Cloner ou mettre à jour le dépôt Seeed pour le board support XIAO nRF54LM20A.
	3	Vérifier que tu peux compiler et flasher un exemple simple (Blinky ou Hello World) sur la carte.

Priorité 1 – Matter Door Lock sur le réseau Thread existant
Objectif : L’appareil rejoint ton réseau Thread/Matter et apparaît dans HA.
	1	Partir de l’échantillon officiel Nordic : samples/matter/lock (Door Lock)
	2	Adapter le board target pour le XIAO nRF54LM20A Sense :
	◦	Créer / modifier les fichiers overlay et conf spécifiques à la carte (voir les overlays déjà fournis par Seeed pour le Matter de base).
	◦	Désactiver ce qui n’est pas nécessaire (PMIC si conflit, flash externe si non utilisé, etc.).
	3	Configurer le device comme Sleepy End Device (SED) pour la faible consommation.
	4	Compiler et flasher.
	5	Commissionner l’appareil sur ton réseau Thread existant (via l’application Matter de HA ou un contrôleur Matter).
	6	Vérifier dans Home Assistant que l’appareil apparaît correctement (état Locked / Unlocked).
Livrable : Appareil Matter Door Lock visible et contrôlable dans HA.

Priorité 2 – IMU 6 axes + Wake-up sur accélération
Objectif : Remonter les données 6 axes vers Home Assistant et utiliser l’accélération pour sortir du sommeil profond.
	1	Intégrer le driver de l’IMU (LSM6DS3TR-C) dans le projet Matter.
	◦	Utiliser les exemples Seeed existants pour la lecture de l’IMU (accéléromètre + gyroscope).
	◦	Activer le mode low-power de l’IMU et l’interruption de wake-up sur mouvement / seuil d’accélération.
	2	Calculer les données utiles :
	◦	Accélération brute (X, Y, Z)
	◦	Gyroscope (X, Y, Z)
	◦	Orientation (pitch / roll, éventuellement yaw si filtrage Madgwick/Mahony)
	3	Mapper les données dans Matter :
	◦	Option simple et recommandée : Utiliser le cluster Contact Sensor (ouvert/fermé) + attributs personnalisés ou un cluster Occupancy / Boolean State pour l’état de mouvement.
	◦	Option plus riche : Créer des attributs personnalisés ou utiliser un cluster de type Measurement pour remonter accélération et orientation.
	◦	Lier l’état du Door Lock à la position de la porte (ex. : porte ouverte = Unlocked + Contact ouverte).
	4	Configurer le wake-up :
	◦	L’IMU réveille le nRF54LM20A sur seuil d’accélération.
	◦	Au réveil → lecture IMU → mise à jour des attributs Matter → retour en sommeil profond (si possible).
	5	Tester :
	◦	Vérifier les valeurs dans les logs.
	◦	Vérifier que HA reçoit bien les mises à jour d’état et les données 6 axes.
	◦	Mesurer la consommation (idéalement avec un Power Profiler).
Livrable : Données 6 axes visibles dans Home Assistant + wake-up fonctionnel sur mouvement de la porte.

Priorité 3 – NFC pour le commissioning (et usage simple)
Objectif : Utiliser le NFC pour faciliter le pairing Matter et éventuellement un usage basique.
	1	Activer l’interface NFC (pads N1/N2) dans l’overlay.
	2	Intégrer l’exemple NFC de Seeed / Nordic (tag emulation ou NDEF).
	3	Configurer le NFC pour :
	◦	Commissioning Matter (le plus utile)
	◦	Éventuellement écrire un petit payload NDEF (texte ou URI) pour un usage simple.
	4	Tester le commissioning via NFC avec un téléphone.
Note : On ne cherche pas encore le déblocage sécurisé type Aliro/HomeKey (option 4).
Livrable : Commissioning possible via NFC + tag basique fonctionnel.

Option 4 (reportée)
Déblocage sécurisé à l’approche d’un iPhone connu (Aliro ou HomeKey). À traiter uniquement une fois les priorités 1-2-3 stables et validées.

Ordre de travail recommandé
	1	Priorité 1 complète et validée dans HA
	2	Ajout de l’IMU + wake-up (Priorité 2)
	3	Ajout du NFC (Priorité 3)
	4	Optimisation consommation et robustesse
	5	(Plus tard) Option 4

Points d’attention
	•	Toujours travailler en mode Sleepy End Device.
	•	Gérer correctement les conflits de pins (I2C de l’IMU, NFC, UART debug, etc.).
	•	Tester régulièrement le comportement après un deep sleep + wake-up.
	•	Documenter les overlays et les Kconfig que tu modifies (ils seront utiles pour la suite).

https://wiki.seeedstudio.com/xiao_nrf54lm20a_getting_started/


Je vais d'abord rechercher les spécifications techniques du XIAO nRF54LM20A Sense et les capacités Matter Over Thread pour vous proposer un cahier des charges précis et à jour.
Voici un **cahier des charges complet** pour votre application Home Assistant exploitant le XIAO nRF54LM20A Sense avec Matter Over Thread.

---

# Cahier des Charges — Capteur de Porte Intelligent Matter Over Thread

## 1. Présentation du Projet

### 1.1 Contexte
Développement d'un capteur de porte intelligent alimenté par batterie, utilisant le **XIAO nRF54LM20A Sense** comme nœud Matter Over Thread. Le dispositif combine détection de position 6 axes, accéléromètre, et authentification par proximité (NFC/BLE) pour commander l'ouverture d'une gâche électrique via un relais Shelly.

### 1.2 Architecture Globale

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              HOME ASSISTANT                                  │
│                         (Matter Controller + Automations)                    │
└─────────────────────────────────────────────────────────────────────────────┘
                                      │
                                      ▼ Matter Over Thread
┌─────────────────────────────────────────────────────────────────────────────┐
│                     THREAD BORDER ROUTER                                     │
│              (HomePod mini, Apple TV 4K, Google Nest, etc.)                 │
└─────────────────────────────────────────────────────────────────────────────┘
                                      │
                                      ▼ Thread Mesh
┌─────────────────────────────────────────────────────────────────────────────┐
│                    XIAO nRF54LM20A SENSE (End Device)                        │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────┐ │
│  │ 6-Axis IMU  │  │    NFC      │  │    BLE 6.0  │  │  Matter Over Thread │ │
│  │ LSM6DS3TR-C │  │   (Tag)     │  │  (iPhone)   │  │      Endpoint       │ │
│  └─────────────┘  └─────────────┘  └─────────────┘  └─────────────────────┘ │
└─────────────────────────────────────────────────────────────────────────────┘
                                      │
                                      ▼ Wi-Fi / LAN
┌─────────────────────────────────────────────────────────────────────────────┐
│                         SHELLY RELAY (Wi-Fi)                                 │
│                    Commande gâche électrique 12VAC                           │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 2. Spécifications Matérielles

### 2.1 Plateforme Principale : XIAO nRF54LM20A Sense

| Caractéristique | Spécification |
|-----------------|---------------|
| **SoC** | Nordic nRF54LM20A (Cortex-M33 @ 128 MHz + RISC-V coprocessor @ 128 MHz) |
| **RAM** | 512 KB |
| **Flash interne** | 2 MB NVM + 8 MB externe |
| **Radio** | BLE 6.0, Thread, Zigbee, Matter, NFC, 2.4 GHz propriétaire |
| **IMU** | LSM6DS3TR-C (accéléromètre 3 axes + gyroscope 3 axes) |
| **Microphone** | PDM (non utilisé dans ce projet) |
| **PMIC** | nPM1300 (gestion batterie LiPo + charge) |
| **GPIO** | 28 broches |
| **Alimentation** | USB-C 5V ou batterie LiPo 3.7V |
| **Consommation veille** | ~4.76 µA (Deep Sleep System OFF) |
| **Consommation active** | ~3.87 mA moyen (TX @ +8 dBm) |
| **Dimensions** | 21 × 17.8 mm |



### 2.2 Capteur 6-Axis (LSM6DS3TR-C)

| Paramètre | Spécification |
|-----------|---------------|
| **Accéléromètre** | ±2/±4/±8/±16 g |
| **Gyroscope** | ±125/±250/±500/±1000/±2000 dps |
| **Interface** | I2C (dédié IMU_SDA/IMU_SCL) |
| **Wake-up** | Pin d'interruption IMU_INT1 connectée au wake-up du SoC |
| **Consommation veille** | 0 (grâce au wake-up par mouvement) |



### 2.3 Module NFC

| Paramètre | Spécification |
|-----------|---------------|
| **Type** | NFC Tag (NFCT Nordic) |
| **Antenne** | Intégrée sur PCB (pads NFC P1.01/P1.02) |
| **Usage** | Authentification par badge/tag NFC |
| **Portée** | ~2-4 cm |

### 2.4 Module BLE 6.0

| Paramètre | Spécification |
|-----------|---------------|
| **Protocole** | Bluetooth LE 6.0 |
| **Fonction** | Détection de proximité iPhone (Channel Sounding/RSSI) |
| **Portée** | ~10-15 m (dépend de l'environnement) |
| **Sécurité** | Pairing sécurisé, chiffrement AES |

### 2.5 Actionneur : Relais Shelly

| Paramètre | Spécification |
|-----------|---------------|
| **Modèle recommandé** | Shelly 1 (ou Shelly Plus 1 avec Matter si disponible) |
| **Alimentation** | 110-230VAC ou 12-24VDC selon modèle |
| **Sortie** | Contact sec (relais) |
| **Charge max** | 16A / 250VAC ou 10A / 24VDC |
| **Connectivité** | Wi-Fi 2.4 GHz |
| **Intégration** | Home Assistant via intégration native Shelly |

> **Note importante** : À ce jour (août 2026), Shelly ne supporte pas officiellement Thread. Le relais Shelly sera connecté en Wi-Fi à Home Assistant, qui fera le pont entre le réseau Thread (capteur) et le réseau Wi-Fi (actionneur). Une alternative serait d'utiliser un firmware communautaire Matter Over Thread pour Shelly Gen4, mais cela invalide la garantie. 

### 2.6 Gâche Électrique

| Paramètre | Spécification |
|-----------|---------------|
| **Type** | Électromagnétique ou électromécanique |
| **Tension** | 12 VAC (alternatif) |
| **Consommation** | ~300-500 mA en maintien |
| **Commande** | Via relais Shelly (contact sec) |
| **Alimentation** | Transformateur 220V → 12VAC dédié |

---

## 3. Spécifications Logicielles

### 3.1 Stack Logiciel

| Couche | Technologie |
|--------|-------------|
| **RTOS** | Zephyr RTOS (via nRF Connect SDK) |
| **SDK** | Nordic nRF Connect SDK v2.9+ |
| **Protocole Matter** | Matter 1.3+ (Cluster Door Lock + Binary Sensor) |
| **Transport** | Thread 1.3 (IEEE 802.15.4) |
| **Commissioning** | BLE pour appairage initial (Matter standard) |
| **Langage** | C/C++ |



### 3.2 Clusters Matter Implémentés

Le dispositif s'enregistrera comme un **Matter Sleepy End Device** avec les clusters suivants :

| Cluster | Rôle | Description |
|---------|------|-------------|
| **Door Lock** (0x0101) | Server | État de la porte (verrouillée/déverrouillée) |
| **Boolean State** (0x0045) | Server | État ouvert/fermé (via IMU) |
| **Power Source** (0x002F) | Server | Niveau batterie |
| **Identify** (0x0003) | Server | Identification visuelle (LED RGB) |
| **Basic Information** (0x0028) | Server | Informations fabricant |

---

## 4. Fonctionnalités Détaillées

### 4.1 Détection de Position et d'Accélération (6-Axis IMU)

#### 4.1.1 Algorithmes de Détection

| Fonction | Description | Seuils Configurables |
|----------|-------------|----------------------|
| **État Ouvert/Fermé** | Détection basée sur l'angle de la porte (gyroscope + accéléromètre) | Angle > 15° = Ouvert |
| **Détection de Mouvement** | Wake-up sur vibration/acceleration | Seuil: ±0.5g sur axe Z |
| **Détection de Fermeture** | Impact détecté (pic d'accélération) | Seuil: ±2g pendant < 50ms |
| **Détection d'Arrachement** | Mouvement anormal / tentative d'intrusion | Seuil: ±4g continu > 2s |
| **Inclinomètre** | Position absolue de la porte | ±1° de précision |

#### 4.1.2 Modes de Fonctionnement

```
Mode Veille Profonde (System OFF)
    │
    ├──> Détection mouvement (IMU_INT1 wake-up)
    │       │
    │       └──> Réveil rapide (< 10ms)
    │               │
    │               ├──> Échantillonnage IMU (100 Hz, 1s)
    │               │       │
    │               │       └──> Confirmation ouverture/fermeture
    │               │               │
    │               │               ├──> Mise à jour cluster Matter
    │               │               └──> Retour veille (si stable)
    │               │
    │               └──> Pas de mouvement confirmé
    │                       │
    │                       └──> Retour veille immédiat
    │
    └──> Wake-up périodique (toutes les 5 min)
            │
            └──> Rapport état batterie
                    └──> Retour veille
```

#### 4.1.3 Consommation Énergétique Cible

| Mode | Consommation | Autonomie (batterie 1000mAh) |
|------|-------------|------------------------------|
| Veille profonde | ~5 µA | ~20 ans (théorique) |
| Wake-up IMU | ~10 mA (pic) | — |
| Transmission Thread | ~15 mA (pic TX) | — |
| **Autonomie réelle estimée** | — | **2-3 ans** |

### 4.2 Authentification et Ouverture

#### 4.2.1 Méthode 1 : NFC (Tag/Badge)

| Paramètre | Valeur |
|-----------|--------|
| **Type de tag** | NFC Type 2 (NTAG213/215/216) ou carte MIFARE |
| **Stockage** | UID unique + clé cryptographique |
| **Processus** | 1. Approche tag → lecture UID<br>2. Vérification clé via Matter<br>3. Si valide → commande d'ouverture |
| **Sécurité** | UID + clé AES-128 stockée en flash sécurisée |
| **Temps de réponse** | < 500ms |

#### 4.2.2 Méthode 2 : BLE Proximity (iPhone)

| Paramètre | Valeur |
|-----------|--------|
| **Technologie** | BLE 6.0 Channel Sounding (précision distance) |
| **Détection** | RSSI + Channel Sounding pour estimation distance |
| **Processus** | 1. Scan périodique BLE (toutes les 2s en veille)<br>2. Détection iPhone appairé<br>3. Vérification proximité (< 2m)<br>4. Si authentifié → commande d'ouverture |
| **Sécurité** | Pairing BLE sécurisé + chiffrement Matter |
| **Temps de réponse** | < 2s |

#### 4.2.3 Méthode 3 : Commande Manuelle (Home Assistant)

| Paramètre | Valeur |
|-----------|--------|
| **Interface** | Dashboard Home Assistant / App mobile |
| **Commande** | Bouton virtuel "Déverrouiller" |
| **Sécurité** | Authentification utilisateur HA + 2FA optionnel |
| **Retour** | Confirmation état porte + historique |

### 4.3 Commande de la Gâche Électrique

```
┌─────────────────┐     Matter Over Thread      ┌─────────────────┐
│  XIAO nRF54LM   │ ───────────────────────────> │  Home Assistant │
│  (Authentifié)  │    Cluster Door Lock        │  (Matter Hub)   │
└─────────────────┘                             └─────────────────┘
                                                        │
                                                        │ API/WebSocket
                                                        ▼
                                               ┌─────────────────┐
                                               │  Shelly Relay   │
                                               │  (Wi-Fi)        │
                                               └─────────────────┘
                                                        │
                                                        │ Contact sec
                                                        ▼
                                               ┌─────────────────┐
                                               │  Gâche 12VAC    │
                                               │  (Ouverture)    │
                                               └─────────────────┘
```

| Paramètre | Valeur |
|-----------|--------|
| **Durée d'activation** | 3-5 secondes (configurable) |
| **Feedback** | LED RGB verte = ouverture en cours |
| **Sécurité** | Double validation (authentification + confirmation) |
| **Timeout** | 10s max sans confirmation de porte ouverte |

---

## 5. Intégration Home Assistant

### 5.1 Entités Exposées

| Entité | Type | Description |
|--------|------|-------------|
| `binary_sensor.porte_ouverte` | Binary Sensor | État ouvert/fermé |
| `sensor.porte_angle` | Sensor | Angle d'ouverture (0-180°) |
| `sensor.porte_acceleration` | Sensor | Accélération en g (axe Z) |
| `lock.porte_entree` | Lock | État verrou/déverrou |
| `sensor.porte_batterie` | Sensor | Niveau batterie (%) |
| `sensor.porte_signal` | Sensor | Force signal Thread (dBm) |
| `binary_sensor.porte_intrusion` | Binary Sensor | Détection d'arrachement |
| `button.porte_identify` | Button | Identification (clignotement LED) |

### 5.2 Automations Recommandées

```yaml
# Automation 1 : Ouverture automatique iPhone
alias: "Porte - Ouverture automatique iPhone"
trigger:
  - platform: state
    entity_id: binary_sensor.iphone_proximite
    to: 'on'
condition:
  - condition: state
    entity_id: lock.porte_entree
    state: 'locked'
action:
  - service: lock.unlock
    target:
      entity_id: lock.porte_entree
  - service: notify.mobile_app_iphone
    data:
      message: "Porte déverrouillée automatiquement"

# Automation 2 : Alerte intrusion
alias: "Porte - Alerte intrusion"
trigger:
  - platform: state
    entity_id: binary_sensor.porte_intrusion
    to: 'on'
action:
  - service: notify.mobile_app_iphone
    data:
      message: "🚨 ALERTE : Tentative d'intrusion détectée !"
  - service: camera.record
    target:
      entity_id: camera.entree

# Automation 3 : Notification porte oubliée
alias: "Porte - Notification oubliée ouverte"
trigger:
  - platform: state
    entity_id: binary_sensor.porte_ouverte
    to: 'on'
    for: "00:05:00"
action:
  - service: notify.mobile_app_iphone
    data:
      message: "La porte est restée ouverte depuis 5 minutes"
```

### 5.3 Dashboard Lovelace

```
┌─────────────────────────────────────────┐
│  🚪 Porte d'Entrée                      │
│  ┌─────────┐  ┌─────────┐  ┌─────────┐ │
│  │  🔓     │  │  📊     │  │  🔋     │ │
│  │Déverrou-│  │  45°    │  │  85%    │ │
│  │ iller   │  │ Angle   │  │ Batterie│ │
│  └─────────┘  └─────────┘  └─────────┘ │
│                                         │
│  État: 🟢 Ouverte (depuis 2 min)        │
│  Signal Thread: -65 dBm                 │
│  Dernière ouverture: iPhone (Jean)      │
│                                         │
│  [Historique] [Paramètres]              │
└─────────────────────────────────────────┘
```

---

## 6. Spécifications Électriques et Mécaniques

### 6.1 Alimentation

| Composant | Tension | Consommation |
|-----------|---------|--------------|
| XIAO nRF54LM20A Sense | 3.3V (régulé) | ~5 µA - 15 mA |
| IMU LSM6DS3TR-C | 1.8V (interne) | ~0.5 mA (actif) |
| LED RGB | 3.3V | ~10 mA (max) |
| **Total estimé** | — | **< 20 mA crête** |

### 6.2 Batterie Recommandée

| Paramètre | Valeur |
|-----------|--------|
| **Type** | LiPo 3.7V |
| **Capacité** | 1000-2000 mAh |
| **Connecteur** | JST-PH 2.0mm |
| **Charge** | Via USB-C (nPM1300 intégré) |
| **Autonomie cible** | 2-3 ans |

### 6.3 Boîtier

| Paramètre | Valeur |
|-----------|--------|
| **Matériau** | ABS ou PC |
| **Protection** | IP54 (intérieur / abri) |
| **Fixation** | Adhésif 3M ou vis |
| **Antenne** | IPEX4 pour antenne externe (option) |
| **NFC** | Zone dédiée sur face avant |

---

## 7. Sécurité

### 7.1 Sécurité Matter

| Aspect | Implémentation |
|--------|----------------|
| **Commissioning** | QR Code + code PIN (Matter standard) |
| **Chiffrement** | AES-CCM-128 (couche MAC Thread) |
| **Authentification** | Certificat d'appareil (Device Attestation) |
| **Mise à jour** | OTA (Over-The-Air) via Matter |
| **Réseau** | Isolation VLAN IoT recommandée |

### 7.2 Sécurité Physique

| Menace | Contre-mesure |
|--------|---------------|
| Arrachement du capteur | Détection par IMU + alerte |
| Brouillage radio | Fallback BLE + notification |
| Clonage NFC | Clé cryptographique unique par appareil |
| Replay attack | Timestamp + nonce dans chaque commande |

---

## 8. Plan de Développement

### Phase 1 : Prototype (4 semaines)
- [ ] Configuration nRF Connect SDK + build Zephyr
- [ ] Intégration driver IMU LSM6DS3TR-C
- [ ] Wake-up par mouvement + mesure consommation
- [ ] Commissioning Matter simple (On/Off cluster)

### Phase 2 : Matter Integration (4 semaines)
- [ ] Implémentation cluster Door Lock
- [ ] Implémentation cluster Boolean State (porte)
- [ ] Intégration Thread networking
- [ ] Tests avec Home Assistant

### Phase 3 : Authentification (3 semaines)
- [ ] Lecture NFC + validation
- [ ] Détection BLE iPhone + Channel Sounding
- [ ] Gestion des credentials (stockage sécurisé)

### Phase 4 : Intégration Actionneur (2 semaines)
- [ ] Configuration Shelly + Home Assistant
- [ ] Automations ouverture/fermeture
- [ ] Tests bout-en-bout

### Phase 5 : Optimisation (3 semaines)
- [ ] Optimisation consommation énergétique
- [ ] Calibration algorithmes IMU
- [ ] Tests endurance (autonomie batterie)
- [ ] Documentation + packaging

---

## 9. Livrables

| Livrable | Format |
|----------|--------|
| Code source firmware | GitHub (C/Zephyr) |
| Schéma électrique | KiCad |
| Fichiers PCB | KiCad/Gerber |
| Documentation technique | Markdown/PDF |
| Guide d'installation | Markdown |
| Automations Home Assistant | YAML |

---
