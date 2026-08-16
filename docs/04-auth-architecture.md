# Architecture d'authentification NFC / BLE (Priorité 3 + Option 4)

Décisions prises le 16/08/2026, à implémenter en Priorité 3 (NFC) et Option 4 (BLE proximité iPhone), mais conçues maintenant pour que la Priorité 1 (Door Lock Matter de base) ne bloque pas leur ajout ultérieur.

## Principe

Le XIAO maintient en flash (NVS) une liste de **credentials** autorisés à déclencher l'ouverture :

| Champ | Description |
|---|---|
| `type` | `NFC_UID` \| `BLE_ADDR` |
| `id` | UID du tag NFC, ou adresse/identité BLE du téléphone |
| `label` | Nom lisible (ex. "iPhone de Jean", "Badge visiteur") |
| `added_at` | Horodatage d'ajout |

Détection d'un credential connu (tag approché en NFC, ou iPhone détecté à proximité en BLE) → commande d'ouverture envoyée via le cluster Matter Door Lock (`Unlock Door`), remontée à Home Assistant comme tout autre événement de déverrouillage.

## Enrollment (ajout de nouveaux tags/téléphones)

Déclenché **depuis Home Assistant**, pas de bouton physique ni d'app dédiée :

1. Service HA (`button.porte_mode_enrollment` ou équivalent) → commande Matter vers le XIAO
2. Le XIAO entre en fenêtre d'enrollment (ex. 30s), signalée par LED (ex. clignotement bleu)
3. Utilisateur approche le tag NFC, ou le téléphone se connecte en BLE
4. Le credential est stocké en NVS, la fenêtre se referme
5. HA reçoit confirmation (nouvel événement / attribut mis à jour) — libellé du credential renseignable ensuite côté HA (pas de saisie sur l'appareil)

Nécessite un **cluster Matter personnalisé** (vendor-specific) côté firmware, car les clusters standards ne couvrent pas cette gestion de credentials multi-facteurs :

- Commande `EnterEnrollmentMode(duration)`
- Attribut `EnrollmentActive` (bool)
- Commande `RemoveCredential(id)`
- Attribut/événement listant les credentials enregistrés (pour affichage HA)

## Pourquoi pas avant Priorité 3

- Le NFC (lecture tag) et le BLE (scan proximité + Channel Sounding) doivent chacun être validés isolément avant d'être combinés à la logique d'ouverture
- Le cluster Matter personnalisé ajoute de la complexité (définition ZAP) qu'il vaut mieux introduire une fois le Door Lock standard stable
- Voir `XIAO-Door-specs.md` §4.2 pour le détail des deux méthodes d'authentification (NFC tag, BLE Channel Sounding)

## Impact sur la Priorité 1 (maintenant)

Aucun changement de code nécessaire immédiatement. Point d'attention pour la structure du firmware : garder la logique "détection credential → ouverture" découplée du cluster Door Lock standard (fonction commune `door_unlock_request(source)`), pour que NFC et BLE l'appellent tous les deux sans dupliquer la logique Matter.
