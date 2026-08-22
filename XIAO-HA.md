# Résumé – XIAO nRF54LM20A Sense → Home Assistant via BTHome

## Objectif
Transmettre **toutes** les données des capteurs de la carte **sauf le microphone** vers Home Assistant, en utilisant **BLE + BTHome**, avec une consommation optimisée.

---

## Protocole choisi
- **Bluetooth Low Energy (BLE)**
- Format : **BTHome v2**
- Mode : Advertising (broadcast, sans connexion)
- Intégration Home Assistant : native (intégration BTHome)

---

## Données transmises (sauf microphone)

| Source              | Données                          | Type HA          |
|---------------------|----------------------------------|------------------|
| IMU Accéléromètre   | accel_x, accel_y, accel_z        | sensor (m/s²)   |
| IMU Gyroscope       | gyro_x, gyro_y, gyro_z           | sensor (°/s)    |
| IMU Orientation     | pitch, roll, yaw                 | sensor (°)      |
| IMU Événements      | motion, free_fall, double_tap, activity | binary_sensor |
| Batterie            | battery_level (%), battery_voltage, charging | sensor / binary_sensor |
| Température         | temperature (SoC)                | sensor (°C)     |
| Bouton              | button                           | binary_sensor / event |

---

## Stratégie d’envoi : 2 trames

### Trame 1 – Événementielle (sur mouvement / changement)
Envoyée **uniquement sur événement** (mouvement, free-fall, double-tap, etc.)

**Contenu :**
- Accélération X, Y, Z
- Gyroscope X, Y, Z
- Pitch, Roll, Yaw
- Flags (motion, free_fall, double_tap, activity, button)

### Trame 2 – Périodique (toutes les 15 minutes)
Envoyée **toutes les 15 minutes** (même sans événement)

**Contenu :**
- Battery level (%)
- Battery voltage
- Charging (état de charge)
- Temperature (SoC)

---

## Modèle de trames BTHome v2

### Trame 1 – Événementielle (exemple de packing)

```text
Device Info     : 0x44 (Trigger-based, non chiffré)
Packet ID       : 0x00 + ID (incrémenté à chaque changement)

Object IDs :
0x51  → Acceleration X (sint16, facteur 0.001 m/s²)
0x51  → Acceleration Y
0x51  → Acceleration Z
0x52  → Gyroscope X (sint16, facteur 0.1 °/s)   // ou object ID custom si besoin
0x52  → Gyroscope Y
0x52  → Gyroscope Z
0x3A  → Pitch (sint16, 0.1 °)
0x3A  → Roll
0x3A  → Yaw
0x21  → Motion (binary)
0x22  → Free fall (binary)
0x2C  → Double tap (binary)   // ou event
0x2D  → Activity (binary)
0x3A  → Button event
Note : Les Object IDs exacts pour gyro / orientation peuvent être adaptés (BTHome n’a pas d’IDs natifs pour tout). On peut utiliser des count ou des IDs custom + documentation côté HA.
Trame 2 – Périodique (toutes les 15 min)
Device Info     : 0x40 (Periodic, non chiffré)
Packet ID       : 0x00 + ID

Object IDs :
0x01  → Battery (uint8, %)
0x0C  → Voltage (uint16, 0.001 V)
0x12  → Charging (binary)          // ou power
0x02  → Temperature (sint16, 0.01 °C)

Avantages de cette solution
	•	Très faible consommation (deep sleep + wake-up sur mouvement)
	•	Intégration native et simple dans Home Assistant
	•	Séparation intelligente : données de mouvement uniquement quand nécessaire
	•	Données de batterie/température envoyées régulièrement sans surconsommation
	•	Compatible avec la XIAO nRF54LM20A Sense (nRF54 + LSM6DS3TR-C + nPM1300)

Notes techniques
	•	Taille max advertising classique : 31 octets → d’où la nécessité de 2 trames
	•	Utiliser le mode Trigger-based (0x44) pour la trame événementielle
	•	Utiliser le mode Periodic (0x40) pour la trame batterie/température
	•	Packet ID obligatoire pour éviter les doublons dans Home Assistant
	•	Chiffrement possible (mais réduit encore plus la place disponible)

Statut final retenu : BLE + BTHome + 2 trames (événementielle + périodique 15 min) + toutes les données sauf microphone.

