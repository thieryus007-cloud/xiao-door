# Suivi des unités

Chaque unité physique XIAO Door possède son propre dossier `unit-XX/` (numéroté à partir de 01), créé au moment où l'unité est déballée/flashée pour la première fois.

Pour créer une nouvelle unité, copier `TEMPLATE.md` vers `unit-XX/unit-XX.md` et remplir les champs.

## Contenu type d'un dossier `unit-XX/`

- `unit-XX.md` — fiche de suivi (voir `TEMPLATE.md`)
- `photos/` — photos de l'unité (soudure antenne NFC, boîtier, installation finale)
- `logs/` — logs de flash / commissioning si utile de les garder

## Registre global

| Unité | Emplacement prévu | Firmware version | Statut | Date commissioning |
|---|---|---|---|---|
| [unit-01](unit-01/unit-01.md) | — (unité de dev/test) | `firmware/apps/lock` (P1+P2) | 🔧 Priorité 1 ✅ commissionnée, Priorité 2 🔴 IMU défectueux (matériel, isolé — voir KNOWN-ISSUES.md) | 2026-08-17 |
| [unit-02](unit-02/unit-02.md) | — (unité de dev/test) | `firmware/apps/lock` (P1+P2) | 🔧 IMU fonctionnel (validé), non commissionnée dans HA | — |
| [unit-03](unit-03/unit-03.md) | — (unité de dev/test) | `firmware/apps/lock` (P1+P2) | 🔧 IMU fonctionnel (validé), non commissionnée dans HA | — |

_(Mettre à jour cette table à chaque nouvelle unité créée)_
