# Suivi — Reconstruction `west build` non reproductible (nRF54LM20A)

**Statut actuel : 🟢 RÉSOLU (2026-09-22), en interne, sans réponse
d'ingénieur Nordic — voir §8. Ticket à clore, brouillon de clôture prêt
(`Plan-Reduction-Consommation-2026-09-22.md` §11), envoi par
l'utilisateur.**

Document de suivi vivant pour ce point précis. À mettre à jour à chaque
nouvelle réponse de Nordic ou nouvelle piste, jusqu'à résolution. Ne pas
rouvrir l'historique complet de l'investigation (déjà détaillé dans
`Configuration-nRF54LM20A-System-ON-IDLE.md` §11) sauf besoin
spécifique de retrouver le raisonnement.

---

## 1. Résumé du problème (pour reprise rapide sans relire tout l'historique)

- Une image d'or (`golden-image/unit01-verified-2026-08-30.bin`,
  117396 octets) est physiquement vérifiée à **~20-23 µA** au PPK2.
- Reconstruire **exactement le même commit source**, avec **exactement
  la même configuration Kconfig** (vérifié identique, 723/723 lignes),
  sur le **même toolchain** (`dcbdc366a1`, NCS 3.4.0), produit un
  binaire de 117428 octets (32 octets de plus) qui mesure **~30-38 µA**
  sur la **même unité physique**, le même jour, avec le même PPK2.
- Tout ce qui est sous notre contrôle est prouvé identique : source,
  Kconfig, toolchain/SDK/board-files (inchangés depuis avant le
  30 août), déterminisme du build (6 rebuilds identiques), unité #02 et
  méthode de mesure (reflasher l'image d'or redonne ~23 µA de façon
  fiable).
- Localisé par désassemblage + `addr2line` : le compilateur
  (`arm-zephyr-eabi-gcc` 14.3.0, `-Os`) génère un code différent (mais
  logiquement équivalent) pour la même ligne source exacte dans
  `sample_motion()` (calcul de `angle_crossed`, comparaison `abs()`
  sur deux `int16_t`) — code exécuté à chaque cycle de boucle (1×/s, en
  continu).
- Conséquence pratique : **impossible de faire confiance à un rebuild
  depuis les sources tant que ceci n'est pas compris** — tout
  changement de code (même correct et bien intentionné) risque de
  produire un firmware mesurablement dégradé, détectable uniquement
  par une mesure PPK2 physique après coup.

Preuves complètes, méthode d'élimination détaillée, fichiers bruts :
voir `Configuration-nRF54LM20A-System-ON-IDLE.md` §11 et le dossier
`Nordic-Support-Ticket-2026-09-19/` (+ son .zip).

---

## 2. Ce qui est bloqué par ce problème

**Le plan d'amélioration de résilience du firmware
(`Audit-Resilience-2026-09-19.md`) est entièrement suspendu.** Voir son
§13 (mis à jour) pour le détail par item — 6 des 8 correctifs sont déjà
codés et testés (2 déjà innocentés individuellement de l'anomalie de
consommation), prêts à être ré-appliqués tels quels dès que ce point
est résolu. Rien n'a été perdu : le code reste disponible, seule la
décision de le flasher en production est suspendue.

**#02 tourne actuellement l'image d'or vérifiée**
(`unit01-verified-2026-08-30.bin`, ~23 µA) — aucun changement en
attente sur cette unité.

Plus largement : **aucun rebuild depuis les sources ne doit être
considéré fiable pour un déploiement de production** tant que ce point
n'est pas éclairci, quelle que soit la légitimité du changement de code
à l'origine. Le déploiement de nouvelles unités continue de se faire
par clonage de l'image d'or existante (procédure déjà en place, non
affectée par ce problème).

---

## 3. Dossier envoyé à Nordic

- **Plateforme :** DevZone (question posée le 2026-09-19)
- **Contenu du dossier de preuves :**
  `C:\ncs\projects\nRF54LM20A\Nordic-Support-Ticket-2026-09-19\` et son
  archive `.zip` — mail complet, binaires bon/mauvais, comparaison
  Kconfig complète, désassemblage + mapping `addr2line` vers la ligne
  source exacte.
- **Quatre questions posées** (reprises ici pour cocher au fur et à
  mesure) :
  1. Bug connu de non-déterminisme `arm-zephyr-eabi-gcc` 14.3.0
     (sélection d'instructions IT-block/if-conversion) ?
  2. `west build` garantit-il un résultat bit-reproductible pour une
     entrée identique ? Workflow recommandé par Nordic sinon ?
  3. NCS 3.4.1 (sorti le 2026-09-17) change-t-il quelque chose côté
     toolchain/reproductibilité ?
  4. Existe-t-il un moyen supporté de vérifier un binaire avant flash,
     sans remesurer physiquement à chaque fois ?

---

## 4. Réponses reçues

### 2026-09-19 — Assistant IA du site Nordic (pas un ingénieur)

Réponse automatisée (recherche documentaire), reconnaissable à son
formulation systématique "the knowledge sources do not contain...".
**Évaluée en détail** (voir échange du 2026-09-19) :

- ✅ **Exact** : confirme que NCS 3.4.0 utilise Zephyr SDK v1.0.1
  (vérifié indépendamment dans le toolchain local,
  `opt/zephyr-sdk/sdk_version` = `1.0.1`) — détail correct à
  mentionner dans le ticket humain.
- ❌ **Erroné** : suggère `west build -vvv` pour capturer les
  invocations complètes du compilateur. **Testé : ne fonctionne pas**
  — `-v`/`-vvv` est une option de `west` lui-même, pas de la
  sous-commande `build` ; la forme correcte est `west -vvv build ...`
  (option avant la sous-commande).
- ⚠️ Reste vague sur la cause elle-même ("compiler's internal
  optimization state differs across time" — non étayé, ne pas retenir
  comme explication).
- N'apporte aucune réponse nouvelle aux 4 questions posées ; recommande
  d'ouvrir un ticket DevZone humain — déjà fait.

**Aucune réponse d'ingénieur humain à ce jour.**

---

## 5. Actions prévues dès qu'une réponse arrive — attentes précises et falsifiables

À chaque nouvelle réponse Nordic, tester **concrètement** avant de
considérer une piste close ou confirmée — ne pas se contenter d'une
explication verbale.

| Si Nordic répond... | Test à faire | Résultat attendu si l'hypothèse est correcte |
|---|---|---|
| Bug GCC connu avec correctif/version spécifique | Installer la version corrigée, rebuild commit `1994112` (sans changement), comparer au hash de l'image d'or | Binaire identique (même hash SHA-256) OU au minimum mesure PPK2 ~20-22 µA |
| Flag/procédure pour build reproductible | L'appliquer sur un rebuild du commit `1994112`, comparer au hash de l'image d'or | Idem ci-dessus |
| NCS 3.4.1 corrige le problème | Migrer le workspace vers 3.4.1, rebuild `1994112`, comparer | Idem ci-dessus |
| Outil de vérification pré-flash proposé | L'intégrer dans `deploy-scripts/` | Détecte correctement l'écart déjà connu (117396 vs 117428 octets) sur les deux binaires déjà en main, sans nouveau flash |
| Aucune piste, Nordic ferme le ticket sans solution | — | Passer au plan de secours (§6) |

**Dans tous les cas**, valider par mesure PPK2 réelle avant de
considérer quoi que ce soit comme résolu — jamais sur la seule base
d'une explication ou d'un hash qui correspond (voir contrainte de
vérification physique, `Audit-Resilience-2026-09-19.md` §14).

---

## 6. Critère de déblocage explicite du plan de résilience

Le plan (`Audit-Resilience-2026-09-19.md` §13) peut être ré-appliqué
dès que **l'une** des conditions suivantes est remplie et **vérifiée
par PPK2** :

1. Un rebuild du commit `1994112` (sans aucun changement de résilience)
   reproduit un binaire identique (hash SHA-256) à l'image d'or, **ou**
2. Un rebuild du commit `1994112` diffère toujours en binaire mais est
   **mesuré à ~20-22 µA** de façon répétée (fonctionnellement
   équivalent malgré la différence de code — accepterait de facto le
   problème comme cosmétique/inoffensif, sans devoir en comprendre la
   cause exacte).

Si aucune des deux n'est atteignable après réponse de Nordic **et**
investigation complémentaire, décision à reprendre avec l'utilisateur :
accepter un palier ~30-38 µA pour déployer la résilience quand même, ou
continuer à attendre.

**Plan de secours (§6, si Nordic ne peut pas aider)** : envisager une
recompilation complète depuis un environnement isolé/reproductible
(conteneur Docker avec le SDK figé, piste mentionnée par un fil
Nordic DevZone trouvé lors de la recherche initiale) plutôt que de
continuer à chercher la cause exacte dans l'environnement local.

---

## 8. Résolution (2026-09-22)

**La prémisse de départ (§1) était fausse : il n'y a jamais eu de
non-déterminisme du compilateur.** Analyse indépendante
(`Plan-Reduction-Consommation-2026-09-22.md` §1, mise en œuvre et
reproduite le même jour, voir `xiao_door_sensor` commit `630b32a`) :

- Rebuild `--pristine` de `c63908d` + H_LACTIVE = `zephyr.bin` **octet
  pour octet identique** à `golden-image/unit02-verified-2026-09-01-
  H_LACTIVE.bin` (SHA-256 `0da087ae45d087afdc334828e95a8c44283b1182
  b1cce5dc1525d7133853f778`) — critère de déblocage §6.1 rempli.
- Cause réelle des rebuilds « ~30-38 µA » : le commit `ccbe1b5`
  (2026-08-29 18:55, **après** le flash de l'image d'or 18:01) a
  changé `k_msleep(6)` → `k_msleep(40)` dans `sample_motion()` (délai
  de stabilisation accéléromètre), jamais appliqué à l'image d'or
  elle-même. +34 ms de rail `imu_vdd` actif par cycle ≈ +11 µA — modèle
  cohérent avec l'écart mesuré (~22 → ~30-38 µA selon les autres
  changements présents dans chaque rebuild testé).
- La divergence de désassemblage sur `angle_crossed` (§1, §4) était
  réelle mais n'était pas la cause : le commit `1994112` testé
  contenait à la fois le `k_msleep(40)` et une logique de confirmation
  sur 2 cycles absente de l'image d'or — deux sources de divergence
  binaire confondues en une seule, pas un signe de non-déterminisme du
  compilateur sur du code strictement identique. `c63908d` seul
  (§ci-dessus) prouve la reproductibilité byte-exact.
- **Critère de déblocage §6.1 rempli** : le plan de résilience
  (`Audit-Resilience-2026-09-19.md`) peut être ré-appliqué sur la base
  réalignée, avec sa propre mesure PPK2 (sa « régression » du 09-19
  venait de la même base à 40 ms, pas de son propre contenu).
- Réponse Nordic : aucune reçue à ce jour au-delà de l'assistant IA
  (§4). Le ticket peut être clos côté utilisateur sans attendre une
  réponse d'ingénieur — brouillon prêt en §11 du plan de réduction de
  consommation.

**Aucune action supplémentaire requise de Nordic.** Ce document reste
la référence historique de l'investigation ; le plan de réduction de
consommation (`Plan-Reduction-Consommation-2026-09-22.md`) prend le
relais pour la suite du travail sur #02.

---

## 7. Historique des mises à jour de ce document

- **2026-09-22** — Résolu en interne (§8) : cause réelle = délai
  accéléromètre modifié par erreur après le flash de l'image d'or, pas
  un défaut du compilateur. Critère de déblocage §6.1 rempli
  (rebuild `c63908d`+H_LACTIVE byte-exact). Statut passé à 🟢.
- **2026-09-19** — Création. Ticket DevZone posté. Réponse assistant IA
  reçue et évaluée (§4). #02 restaurée sur image d'or. Plan de
  résilience §13 mis à jour avec statut bloqué par item.
