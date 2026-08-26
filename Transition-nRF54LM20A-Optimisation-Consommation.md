# Projet : Optimisation de la consommation de courant du XIAO nRF54LM20A Sense

Document de suivi vivant — **à mettre à jour à chaque conversation** sur ce
sujet (ce qui a été fait, testé, implémenté), pour pouvoir reprendre
directement d'ici plutôt que de tout redécouvrir. Modèle repris du document
équivalent du projet nRF52840 : `Transition-nRF52840-Optimisation-Consommation.md`.

## Lire en premier (ne pas dupliquer)

- `xiao_nrf54lm20a_project_notes.md` — référence complète du projet
  (build/flash/vérification, architecture System OFF, registres IMU,
  trames BTHome, § « Budget énergétique (calculé, non mesuré) »).
- `PPK-Mesures-Transition.md` — document de démarrage de la campagne de
  mesure PPK II sur cette carte (câblage à confirmer, décisions ouvertes,
  retours communautaires trouvés en recherche).
- `Transition-nRF52840-Optimisation-Consommation.md` — campagne PPK2 sœur
  sur le XIAO nRF52840, dont certaines leçons se transposent ici (voir
  section suivante).

## Règle de travail impérative — Ampere meter uniquement, jamais Source Meter

Deux raisons indépendantes, désormais confirmées ensemble :

1. **Risque matériel** (`PPK-Mesures-Transition.md`) : un retour DevZone
   Nordic documente une carte nRF54L15-DK détruite en mode Source (le PPK2
   fournissant lui-même une tension dépassant le max admissible du point
   d'injection). Notre carte a sa propre alimentation batterie — mode
   Ampere meter uniquement, sauf besoin explicitement justifié et tension
   vérifiée compatible avec le point d'injection exact.
2. **Artefact de mesure confirmé aujourd'hui (2026-08-26)** sur le projet
   sœur nRF52840 : le mode Source Meter du PPK2 ajoute **~7 µA de courant
   parasite** non représentatif du courant réel (comparaison directe
   Source Meter vs Ampere meter sur la même carte, alimentation externe
   stable — voir `Transition-nRF52840-Optimisation-Consommation.md`
   § « Prochaine étape — isoler le mode de mesure PPK2 » et § « Découverte
   déterminante »). Pour une carte dont le budget calculé tourne autour de
   10-14 µA, une erreur de 7 µA fausserait toute conclusion.

**⚠️ État observé le 2026-08-26** : une capture d'écran de Power Profiler
montre le PPK2 (`EF23F2044470`) actuellement en mode **Source Meter**,
sortie activée à 3500 mV. Ceci contredit la règle ci-dessus — à corriger
(rebrancher en Ampere meter selon le point d'injection qui sera confirmé,
voir `PPK-Mesures-Transition.md` § « Décisions à prendre en début de
session ») avant toute mesure exploitable sur cette carte.

## Leçons transposées du projet nRF52840 (évaluées le 2026-08-26)

**Se transposent telles quelles :**
- Discipline « un seul changement à la fois » entre deux mesures PPK2.
- Documentation immédiate de chaque test (valeurs, conditions, conclusion)
  plutôt qu'a posteriori.
- Méthode de comparaison sur une DK nue pour isoler ce qui est générique
  au SoC/PMIC de ce qui est spécifique au circuit XIAO. **À vérifier** :
  existence d'une DK nRF54LM20A propre — seule une **nRF54L15-DK** est
  référencée à ce jour (`PPK-Mesures-Transition.md` § « Retours
  communautaires »), puce proche mais pas identique.
- Leçon sur l'identification fiable des unités avant/pendant une campagne
  PPK2 (apprise à la dure sur le projet nRF52840 le 2026-08-26 — erreur
  d'attribution de campagne entre unité #1 et #3, corrigée après coup).
  Vérifier systématiquement le numéro de série du pont USB↔SWD (ou
  l'adresse BLE) de la carte réellement en cours de mesure, ne jamais
  supposer à partir de l'ordre de branchement.

**Ne se transposent pas sans revérification :**
- `CONFIG_PM=y` sans effet observé — spécifique aux Kconfig nRF52
  (`CONFIG_PM_DEVICE_SYSTEM_MANAGED`, etc.), pas vérifié sur nRF54L.
- Comparaison régulateur DC-DC vs LDO (`&reg1` sur nRF52840) — architecture
  d'alimentation différente sur cette carte (PMIC nPM1300), pas testée ici.
- Architecture d'alimentation elle-même : le projet nRF52840 tourne encore
  en **System ON** (boucle logicielle, sondage périodique) au moment de sa
  campagne PPK2, alors que `xiao_door_sensor` (nRF54LM20A) est déjà en
  **System OFF hybride** (réveil matériel IMU + GRTC). Les deux choix sont
  volontaires, pour des raisons propres à chaque projet — ne pas chercher à
  aligner l'un sur l'autre sans raison explicite.
- Les pics génériques ~23 ms observés sur nRF52840 (confirmés propres au
  SoC via comparaison DK nue) — jamais recherchés sur nRF54L, à vérifier
  indépendamment si un profil de pics similaire apparaît ici.

## État au 2026-08-26

- **Dépôt Git créé** : `C:\ncs\projects` (racine), remote GitHub
  `https://github.com/thieryus007-cloud/xiao-door` — `xiao_nrf52840_door_sensor/`
  volontairement exclu (déjà versionné séparément dans son propre dépôt).
- **Matériel actuellement branché sur le PC** (vérifié via
  `Get-PnpDevice`) :
  - XIAO nRF54LM20A Sense **unité #1** (`D2:3A:F7:B1:E8:18`, pont SAMD11
    `C5F0E209`) — port `COM3`.
  - PPK2 `EF23F2044470` — port `COM21`.
- **Étape suivante annoncée par l'utilisateur, pas encore faite** : retirer
  le logging du firmware `xiao_door_sensor` avant la campagne de mesure.
  Cohérent avec un piège documenté dans `PPK-Mesures-Transition.md`
  § « Retours communautaires » : un courant anormalement élevé en
  sleep/System OFF vient souvent de l'UART/logging resté actif, pas d'un
  vrai défaut matériel — le firmware actuel ne suspend pas explicitement
  la console avant `sys_poweroff()`. **Détail exact de ce qui sera retiré
  (quels `LOG_*`/`CONFIG_LOG_*`/console UART) pas encore défini — à
  préciser avant modification, conformément à la règle de communication
  du projet.**
- Câblage PPK2 exact pour cette carte (point d'injection) : **toujours pas
  confirmé** (voir `PPK-Mesures-Transition.md` § « Décisions à prendre en
  début de session », point 1).
- Aucune mesure PPK2 réalisée à ce jour sur cette carte.

## À faire ensuite

1. Préciser et retirer le logging concerné dans `xiao_door_sensor` (portée
   exacte à définir avec l'utilisateur).
2. Confirmer le point d'injection PPK2 exact sur cette carte (schéma Seeed)
   avant toute manipulation matérielle.
3. Rebrancher/configurer le PPK2 en **Ampere meter** (jamais Source Meter,
   voir règle ci-dessus).
4. Mesurer, comparer aux chiffres calculés de
   `xiao_nrf54lm20a_project_notes.md` § « Budget énergétique », mettre à
   jour ce document au fil de l'eau (pas de récit à part — l'état courant
   ici, le détail des tests dans `PPK-Mesures-Transition.md` si besoin).
