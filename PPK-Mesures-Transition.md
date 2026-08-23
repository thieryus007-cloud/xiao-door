# Transition — mesures de consommation réelle au PPK II (XIAO nRF54LM20A)

**Document de démarrage pour une nouvelle conversation.** Objectif :
mesurer la consommation électrique réelle du firmware `xiao_door_sensor`
(capteur de porte/fenêtre BLE) au **Power Profiler Kit II (PPK II)**
de Nordic, pour remplacer les estimations théoriques par des chiffres
mesurés et trancher la faisabilité d'une batterie 400-600 mAh (objectif
exprimé par l'utilisateur, 3-4+ ans d'autonomie visés).

## Règle de travail impérative — identique à celle du projet

Voir `C:\ncs\CLAUDE.md` (s'applique à tout travail sous `C:\ncs\`) :
**communiquer toutes les informations pertinentes avant d'agir, vérifier
l'état réel avant de supposer qu'un état précédent tient toujours.**
Deux points spécifiques à cette session :

1. **Manipulation matérielle** : avant toute connexion du PPK II à une
   carte, donner le câblage exact, pin par pin, avec sa source (schéma
   officiel Seeed ou documentation Nordic PPK II) — **ne jamais deviner
   un point de mesure de courant**. Le câblage exact pour cette carte
   précise (XIAO nRF54LM20A Sense) n'a **pas encore été recherché/vérifié**
   à la date de rédaction de ce document — c'est la toute première chose
   à faire en début de session, avant de demander à l'utilisateur de
   toucher au matériel.
2. **Ne pas supposer un état non vérifié** : avant de commencer, vérifier
   quelle(s) carte(s) sont physiquement disponibles pour le test (voir
   § « Décisions à prendre en début de session » ci-dessous) plutôt que
   de supposer qu'une carte particulière est déjà connectée ou libre.

## Contexte — ne pas dupliquer, lire en premier

- `C:\ncs\projects\xiao_nrf54lm20a_project_notes.md` — référence complète
  du projet (build/flash/vérification, architecture des trames BTHome,
  historique des bugs). **À lire en premier.**
- `C:\ncs\projects\Recherche-Reveil-Materiel-XIAO.md` — étude de
  faisabilité System OFF + **budget énergétique théorique calculé**
  (§ « Budget énergétique complet », § « Suite ») : c'est ce budget que
  ce document de transition vise à valider ou corriger par la mesure.

## État au moment de la rédaction (2026-08-30)

- **Les 3 unités déployées tournent sur le firmware System OFF**
  (réveil matériel + veille profonde) depuis le 2026-08-30, validées
  fonctionnellement sur matériel réel (HA reçoit correctement toutes
  les trames), mais **jamais mesurées en consommation réelle**.
- Le firmware **précédent** (System ON, sondage logiciel continu toutes
  les 2s, celui qui tournait sur les 3 unités avant le 2026-08-30)
  **n'existe plus nulle part en tant que build actif** — pas de contrôle
  de version sur ce projet. Une copie de son code source a été préservée
  dans `C:\ncs\projects\xiao_door_sensor\reference\main_pre_systemoff_2026-08-30.c`
  (avec un en-tête expliquant son statut et comment la reconstruire en
  projet buildable si une comparaison directe ancien/nouveau firmware
  est souhaitée — **jamais flasher cette version sur une des 3 unités en
  service**, utiliser un exemplaire de test séparé si disponible).

## Chiffres théoriques à valider (repris de `Recherche-Reveil-Materiel-XIAO.md`)

| Scénario (porte au repos, pas de mouvement) | Courant moyen calculé | Statut |
|---|---|---|
| Soft actuel (System ON, ancien firmware) | ~16,6 à 22,6 µA (milieu ~19,6 µA) | Calculé, jamais mesuré |
| Soft optimisé (System OFF, firmware actuel) | ~10,9 à 16,9 µA (milieu ~13,9 µA) | Calculé, jamais mesuré |

Décomposition par poste (voir le document source pour le détail des
calculs et sources) :

| Poste | Soft actuel | Soft optimisé | Statut |
|---|---|---|---|
| IMU LSM6DS3TR-C, low-power @12,5 Hz | 9 µA | 9 µA (inchangé) | VÉRIFIÉ (datasheet) |
| SoC nRF54LM20A, veille | 4,3 µA | 1,0 µA | VÉRIFIÉ (datasheet) |
| PMIC nPM1300, quiescent | ≥0,8 µA (plancher) | ≥0,8 µA (plancher) | VÉRIFIÉ comme plancher, incomplet |
| Boucle logicielle (sondage 2s) | ~2,4 µA | ~0 µA | ESTIMÉ, jamais mesuré |
| Radio BLE (rafales publicitaires) | 0,06-6 µA | 0,06-6 µA | ESTIMÉ, plage large, jamais mesuré |

Les postes marqués ESTIMÉ (boucle logicielle, radio) et le plancher PMIC
incomplet sont les principales sources d'incertitude que la mesure
devrait lever.

## Décisions à prendre en début de session (avec l'utilisateur, pas seul)

1. **Câblage PPK II exact pour cette carte** : rechercher (schéma
   officiel Seeed, déjà référencé dans `xiao_nrf54lm20a_project_notes.md`
   § « Pinout debug », + documentation Nordic PPK II) où couper/piquer
   l'alimentation pour insérer le PPK II en mesure série — carte
   alimentée par batterie LiPo (JST) en usage normal, donc le point de
   mesure est probablement entre la batterie et l'entrée d'alimentation
   de la carte, pas nécessairement le connecteur USB-C (qui sert aussi
   à charger et au pont SAMD11 de flash/debug). **À confirmer avant
   toute manipulation.**
2. **Quelle carte utiliser pour la mesure** : idéalement pas une des 3
   unités actuellement en service (couperait la remontée HA pendant
   toute la durée de la mesure) — demander à l'utilisateur s'il existe
   un exemplaire de test/spare disponible, sinon négocier une
   indisponibilité temporaire d'une des 3 unités déployées.
3. **Comparer ancien vs nouveau firmware ?** Si oui, il faut un second
   dossier de projet buildable à partir de la copie archivée (voir
   ci-dessus) — travail de mise en place à prévoir, pas immédiat.
4. **Durée/scénarios de mesure** : au minimum, courant moyen au repos
   (le chiffre théorique principal ci-dessus) ; idéalement aussi une
   séquence avec mouvement (rafales BLE + réveils GPIO) pour valider le
   poste radio, et si possible un cycle complet incluant une trame
   santé (15 min) pour capturer le réveil GRTC périodique.

## Ce qu'il faudra faire une fois les mesures obtenues

- Comparer aux chiffres théoriques ci-dessus, mettre à jour
  `Recherche-Reveil-Materiel-XIAO.md` (marquer les lignes ESTIMÉ comme
  VÉRIFIÉ ou les corriger) et `xiao_nrf54lm20a_project_notes.md`
  (§ « Implémentation System OFF », § « Prochaines étapes »).
  **Ne pas dupliquer le contenu de ces documents dans cette
  conversation — les mettre à jour directement.**
  - Recalculer l'autonomie réelle sur 1500 mAh (batterie actuelle) et
    sur 400/600 mAh (objectif visé) à partir du courant moyen mesuré.
  - Trancher la faisabilité de la batterie 400-600 mAh sur cette base.
