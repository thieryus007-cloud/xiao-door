# Suivi des unités

Chaque unité physique XIAO Door possède son propre dossier `unit-XX/` (numéroté à partir de 01), créé au moment où l'unité est déballée/flashée pour la première fois.

Pour créer une nouvelle unité, copier `TEMPLATE.md` vers `unit-XX/unit-XX.md` et remplir les champs.

## Contenu type d'un dossier `unit-XX/`

- `unit-XX.md` — fiche de suivi (voir `TEMPLATE.md`)
- `photos/` — photos de l'unité (soudure antenne NFC, boîtier, installation finale)
- `logs/` — logs de flash / commissioning si utile de les garder

## Registre global

| Unité | Adresse BLE | Firmware | Statut HA |
|---|---|---|---|
| [unit-01](unit-01/unit-01.md) | `D2:3A:F7:B1:E8:18` | `firmware/apps/xiao_door_sensor` | ✅ Intégré |
| [unit-02](unit-02/unit-02.md) | `DE:F6:A3:A9:0F:0F` | `firmware/apps/xiao_door_sensor` | ✅ Intégré |
| [unit-03](unit-03/unit-03.md) | `E6:C9:11:CE:6E:C6` | `firmware/apps/xiao_door_sensor` | ✅ Intégré |

_(Mettre à jour cette table à chaque nouvelle unité créée)_
