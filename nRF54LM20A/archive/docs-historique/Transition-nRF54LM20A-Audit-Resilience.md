# Transition — Audit de résilience du code, XIAO nRF54LM20A (à démarrer dans une nouvelle conversation)

**Ce document prépare le terrain pour un audit de code qui n'a PAS encore
été fait.** Commandé explicitement par l'utilisateur le 2026-09-19, à
réaliser dans une nouvelle conversation (celle qui a produit ce document
a été close intentionnellement après l'avoir rédigé). Objectif de
l'audit : **résilience du firmware** (comportement face aux erreurs,
pannes partielles, corruption d'état, blocages) — **pas** la
consommation, déjà couverte de façon exhaustive ailleurs (voir §3).

---

## 1. État physique exact au moment de la rédaction (2026-09-19)

- **Unité XIAO nRF54LM20A #02** (S/N pont SWD `9C4A557D`) branchée sur un
  port USB du PC.
- Firmware actuellement flashé : **image d'or de production**
  (`xiao_door_sensor/golden-image/unit02-verified-2026-09-01-H_LACTIVE.hex`),
  restaurée et vérifiée cette même session (`verify_image` conforme,
  117396 octets) après une série de tests de diagnostic PMIC (voir §4).
  **Aucun firmware de diagnostic laissé flashé.**
- Unité #01 (S/N `C5F0E209`) : état inchangé depuis le dernier flash
  connu (voir tableau de déploiement, `Configuration-nRF54LM20A-System-ON-
  IDLE.md` §7) — pas touchée cette session.
- **Avant tout flash dans la nouvelle conversation** : appliquer la règle
  absolue du projet (`C:\ncs\CLAUDE.md`) — vérifier le numéro de série SWD
  par une commande `openocd ... -c init -c exit` en lecture seule avant
  chaque flash, comparer explicitement à l'unité ciblée, ne jamais
  supposer qu'une seule carte est branchée.

---

## 2. Ce que "résilience" doit couvrir ici

Le firmware `xiao_door_sensor` (voir `src/main.c`) est déployé sur un parc
réel de **13 unités en service** (#01, #02, unit04-unit13, voir
`Configuration-nRF54LM20A-System-ON-IDLE.md` §7) avec ~7 unités
supplémentaires prévues, intégrées dans Home Assistant, sans supervision
active constante. La question posée : **que se passe-t-il quand quelque
chose se passe mal sur le terrain**, et le firmware actuel est-il capable
de s'en remettre seul, ou de laisser une trace exploitable, plutôt que de
rester silencieusement en panne pendant des semaines avant d'être
remarqué ?

Ce n'est PAS un audit de consommation (§8.5 de `Nordic-Support-Report-
XIAO-nRF54LM20A.md`, `Audit-Consommation-2026-09-13.md`, et cette
conversation même ont déjà traité ce sujet en profondeur — ne pas
rouvrir cet axe sans raison nouvelle).

---

## 3. Fichiers à lire en premier, dans cet ordre

1. `xiao_door_sensor/src/main.c` — code réellement déployé (source de
   vérité, pas la documentation).
2. `xiao_door_sensor/prj.conf` et
   `xiao_door_sensor/boards/xiao_nrf54lm20a_nrf54lm20a_cpuapp.overlay`.
3. `Configuration-nRF54LM20A-System-ON-IDLE.md` — état de référence
   fonctionnel actuel, unique et à jour.
4. `Nordic-Support-Report-XIAO-nRF54LM20A.md` — tout l'historique
   d'isolation consommation (§1-9), y compris la session
   d'aujourd'hui (§8.5).
5. `Audit-Consommation-2026-09-13.md` — précédent audit approfondi de ce
   même firmware, méthode à reproduire pour celui-ci (relecture du code
   réellement déployé, vérification des affirmations contre le code
   source des drivers et les fiches techniques primaires, pas de
   confiance a priori). **§4 de ce document est une contrainte
   structurante pour TOUT audit futur, voir §4 ci-dessous.**
6. `xiao_nrf54lm20a_project_notes.md` — contexte général du projet.

---

## 4. CONTRAINTE CRITIQUE à respecter dès le début de l'audit

**La reconstruction du firmware depuis les sources n'est pas fiable**
(trouvaille de `Audit-Consommation-2026-09-13.md` §4, toujours non
résolue à ce jour) : un `west build` propre sur `main.c` produit un
binaire mesurant ~33 µA au lieu des ~20-22 µA de l'image d'or, avec
**64159 octets de différence sur 117396** — plus de la moitié du binaire
diffère, cause exacte inconnue. L'image actuellement déployée sur
#01/#02 a été obtenue par **patch binaire d'un octet** sur une image
d'or antérieure, pas par compilation propre (voir
`Procédure-Clonage-XIAO-nRF54LM20A.md`).

**Conséquence directe pour l'audit de résilience** : toute correction de
code proposée doit être **physiquement flashée et vérifiée** (PPK2 +
comportement fonctionnel réel) avant d'être considérée fiable — ne
jamais conclure qu'un correctif de code est bon sur la seule base d'une
relecture, tant que cet écart de reconstruction n'est pas expliqué.
Isoler cette régression (diff `.config` complet entre un rebuild propre
et l'image d'or, bissection) reste une tâche ouverte, séparée de l'audit
de résilience proprement dit mais qui le contraint directement.

---

## 5. Piste ouverte trouvée aujourd'hui, à clarifier en premier

**Incohérence entre deux documents/lectures de code, non résolue,
n'affecte a priori pas la consommation (baseline déjà au plancher
datasheet ~4,1 µA confirmé 3× cette session) mais mérite d'être
élucidée pour la résilience/propreté du code :**

- `Audit-Consommation-2026-09-13.md` §6bis affirme que la production
  « le fait déjà depuis toujours (`suspend_external_flash()`, `main.c`) »
  à propos de la mise en veille profonde (DPD) de la flash SPI externe
  `py25q64`.
- Lecture directe du `main.c` actuellement déployé (faite deux fois
  cette session, dont une vérification `grep` dédiée) : **`suspend_
  external_flash()` est définie (ligne 320) mais n'est appelée nulle
  part** — `main()` n'appelle que `configure_spi_pins_for_system_off()`
  (force les broches GPIO brutes, ne passe pas par l'API PM Zephyr du
  driver flash).

**À faire dans l'audit** : déterminer si c'est (a) une imprécision de
l'audit du 2026-09-13 (confusion entre les deux fonctions), (b) un
changement réel de `main.c` entre le 2026-09-13 et aujourd'hui, ou (c)
du code mort à nettoyer (`suspend_external_flash()` jamais appelée) —
sans supposer laquelle avant vérification (`git log`/historique si
suivi, sinon comparaison avec les sauvegardes `archive/`).

---

## 6. Pistes candidates pour l'audit de résilience (non investiguées, à vérifier — pas des conclusions)

Repérées en lisant `main.c` cette session pour un autre objectif
(construction des firmwares de diagnostic §8.5), jamais creusées côté
résilience :

- **Aucun watchdog matériel configuré** (`CONFIG_WATCHDOG`/`CONFIG_WDT`
  absents de `prj.conf`, confirmé par recherche — aucune occurrence dans
  tout `xiao_door_sensor/`). Un blocage logiciel (deadlock I2C, boucle
  infinie) laisserait une unité silencieuse indéfiniment sur le terrain,
  sans réveil automatique.
- **Toutes les branches d'erreur de `sample_motion()`/`main()`
  aboutissent à un simple `printf(...)`** — invisible en production
  puisque `CONFIG_SERIAL=n` en dur (règle absolue du projet). Un échec
  IMU répété/permanent, un échec `bt_le_adv_start()`, une lecture
  batterie en échec : aucun mécanisme de comptage, de reboot de sécurité,
  ni de remontée d'état dégradé vers Home Assistant. Impossible
  aujourd'hui de distinguer à distance « unité éteinte/HS » de « unité
  qui échoue silencieusement en boucle » sans accès SWD physique.
- **`retained_state` (RAM retenue) sans mécanisme de version explicite** :
  CRC valide seulement, pas de champ de version — un futur changement de
  la struct (ajout/réordonnancement de champ) après un reflash pourrait
  interpréter un ancien état retenu comme valide (CRC calculé sur un
  layout différent) sans le détecter.
- **Intégration gyroscopique du yaw jamais recalée** ("aucun recalage
  anti-dérive", commentaire déjà présent dans le code) — dérive non
  bornée sur le très long terme (mois de fonctionnement continu),
  jamais quantifiée.
- **Bouton physique** : bug connu documenté (toujours lu à 0), repris tel
  quel de la production précédente sans être résolu — vérifier si c'est
  toujours hors-périmètre ou si ça mérite d'être réouvert sous l'angle
  résilience (une entrée matérielle qui ne fonctionne jamais est un
  signal qui devrait remonter, pas juste être ignorée).
- **Pas de compteur/heuristique de redémarrages inattendus** au-delà de
  `hwinfo_get_reset_cause()` loggé (invisible, `CONFIG_SERIAL=n`) — un
  cycle de brownout/reset répété ne laisserait aucune trace exploitable
  à distance.

Chacune de ces pistes est à vérifier/creuser dans l'audit, pas à traiter
comme acquise.

---

## 7. Contexte de la conversation qui a produit ce document (pour référence, pas à reprendre)

Cette conversation a, dans l'ordre : vérifié une vidéo YouTube mal
identifiée (lien ne correspondant pas à la description) ; confirmé que
la stratégie LIS2DH12 évoquée ne s'applique pas directement (IMU
différent, goulot dominant différent) ; fermé la piste "sleep mode"
PMIC LOADSW1/LDO1 par lecture du driver (pas de retention mode pour
LDSW) ; exécuté 3 tests d'isolation matériels sur #02 confirmant que
ne jamais couper `imu_vdd` reste ~11-12× pire que la stratégie actuelle
(voir §8.5 de `Nordic-Support-Report-XIAO-nRF54LM20A.md`) ; comparé le
nRF54LM20A (~20-22 µA) au projet frère nRF52840 Sense (~11,35-11,36 µA,
`C:\ncs\projects\nRF52840\xiao_nrf52840_door_sensor\`), expliquant
l'écart par l'absence de PMIC sur le nRF52840 (IMU alimenté par un LDO
simple toujours actif) contre le rail `imu_vdd`/LOADSW1/LDO1 du nPM1300
sur le nRF54LM20A (anomalie ~250-300 µA, toujours en attente de réponse
Nordic). Aucun de ces sujets n'est directement l'objet de l'audit de
résilience — contexte disponible si utile, pas un prérequis de lecture.
