# Projet : Optimisation de la consommation de courant du XIAO nRF54LM20A Sense

Document de suivi vivant — **à mettre à jour à chaque conversation** sur ce
sujet, pour reprendre directement d'ici plutôt que de tout redécouvrir.

## Résumé pour reprendre une nouvelle conversation

**Objectif** : faire descendre le courant moyen au repos sous **6 µA**
(budget calculé, jamais mesuré : ~13,9 µA — voir
`xiao_nrf54lm20a_project_notes.md` § Budget énergétique).

**Où on en est** : le firmware complet (IMU + BLE) mesure ~250-400 µA au
repos — 20 à 60x le budget calculé. Sept causes plausibles testées une par
une, **toutes écartées** (tableau ci-dessous). Le dernier test (carte nue,
IMU et BLE coupés) vient de tomber juste avant la fin de cette conversation
et n'est **pas encore analysé en détail** — c'est le point de départ de la
prochaine session (voir § Dernier résultat, pas encore interprété).

**Prochaine action immédiate** : lire le § « Dernier résultat » ci-dessous,
l'interpréter, décider de la suite (probablement réintroduire IMU seule,
puis BLE seul, pour savoir laquelle des deux réintroduit le plancher élevé
— sauf si le nouveau motif de pics ~1,5 s observé même sans IMU/BLE change
la priorité, voir hypothèse ci-dessous).

## Protocole de travail (fixé le 2026-08-27, valable jusqu'à la fin du projet)

Boucle de mesure/itération, à respecter à chaque cycle sans renégocier :

1. **Claude indique explicitement quand débrancher le XIAO du PC** — une
   fois un changement de firmware buildé et flashé, prêt à être mesuré.
2. **L'utilisateur fait la mesure**, principalement au PPK2 (câblage,
   mode, capture — entièrement pris en charge par l'utilisateur, Claude
   n'y touche plus).
3. **L'utilisateur communique les résultats de mesure** à Claude.
4. **L'utilisateur rebranche le XIAO sur le port USB du PC** pour que
   Claude poursuive les essais/recherche d'optimisation.

Toute action demandée à l'utilisateur doit être formulée clairement (quoi
débrancher/rebrancher) — jamais une consigne vague. Ne pas demander la
suite entre chaque étape tant que le projet n'est pas terminé — narrer ce
qui est fait et enchaîner, sauf pour une action matérielle qui requiert
les mains de l'utilisateur.

## Règles de travail à ne pas oublier

- **Ampere meter uniquement, jamais Source Meter** pour toute mesure de
  courant sur cette carte : risque de destruction documenté (retour
  DevZone, nRF54L15-DK) si la tension source dépasse le point d'injection,
  et artefact de mesure ~7 µA confirmé indépendamment. *(Note 2026-08-27 :
  toutes les mesures de cette session ont en fait été prises en Source
  Meter — la méthode a néanmoins déjà démontré sa capacité à détecter un
  vrai changement de code sur le projet frère, ~100x de réduction. Le
  passage en Ampere meter reste recommandé mais n'est plus considéré comme
  bloquant pour continuer le diagnostic.)*
- **Un seul changement à la fois** entre deux mesures PPK2 (sauf exception
  documentée explicitement, ex. deux erratums indépendants appliqués
  ensemble).
- **Vérifier l'état réel avant d'agir** (`Get-PnpDevice` pour la connexion
  USB) plutôt que redemander à l'utilisateur ce qui a déjà été fait.
- Ne pas réactiver le logging (`CONFIG_LOG`), consigne explicite de
  l'utilisateur — diagnostiquer sans lecture série (SWD memory-read ponctuel
  si nécessaire, avec prudence : voir note de fiabilité plus bas).
- Ne pas reparler du projet nRF52840 sauf si l'utilisateur l'évoque
  lui-même (ce qu'il a fait le 2026-08-27 pour en tirer des leçons
  applicables — voir tableau d'élimination, dernières lignes).

## Lire en premier (ne pas dupliquer)

- `xiao_nrf54lm20a_project_notes.md` — référence complète du projet
  (build/flash/vérification, architecture System OFF, registres IMU,
  trames BTHome, § Budget énergétique).
- `PPK-Mesures-Transition.md` — démarrage de la campagne PPK II (câblage,
  décisions ouvertes, retours communautaires).

## Matériel (état au 2026-08-27)

- XIAO nRF54LM20A Sense **unité #1** (`D2:3A:F7:B1:E8:18`, pont SAMD11
  `C5F0E209`) — port `COM3` quand branchée au PC.
- PPK2 `EF23F2044470` — reste branché au PC en permanence (nécessaire pour
  l'appli Power Profiler), ses fils de mesure vont séparément sur les
  pastilles BAT+/BAT- de la carte.
- ⚠️ **Unité #1 fait partie des 3 unités déployées en Home Assistant.**
  Chaque reflash de test l'interrompt (HA) et la fait tourner sur un
  firmware différent des unités #2/#3 (toujours sur l'ancien firmware,
  logging actif, ODR 12,5 Hz — pas encore mises à jour). Point à traiter
  une fois l'optimisation terminée : soit repasser #1 sur le firmware
  final, soit redéployer sur #2/#3 aussi.
- Dépôt Git : `C:\ncs\projects` (racine), remote
  `https://github.com/thieryus007-cloud/xiao-door` — pas encore poussé
  (`git push`) à ce jour.

## Tableau d'élimination — pistes testées pour le plancher élevé

Chaque ligne = un changement isolé testé sur matériel réel avec capture
PPK2 avant/après (méthode « un seul changement à la fois », sauf note
contraire).

| # | Piste | Changement testé | Résultat |
|---|---|---|---|
| 1 | Logging/console UART actif | `CONFIG_LOG=n` | Amélioration partielle mais insuffisante (572 µA → 277-297 µA) — nécessaire mais pas suffisant |
| 2 | ODR accéléromètre 12,5 Hz | Passé à 1,6 Hz | Aucun effet (572 → 532 µA, différence non significative) — **écarté**, revenu à 12,5 Hz |
| 3 | `CONFIG_SERIAL`/`CONFIG_CONSOLE` restés actifs malgré `CONFIG_LOG=n` (bug UARTE connu, DevZone) | Forcés à `n` explicitement | Amélioration (532 → 277 µA) mais toujours 20x le budget — nécessaire mais pas suffisant |
| 4 | Seuil de réveil IMU (`WAKE_UP_THS`) trop sensible au bruit | 0x01 → 0x04 (~31→125 mg) | Aucun effet — **écarté**, revenu à 0x01 |
| 5 | Régulateur interne SoC (`vregmain`) en DCDC, pulse-skip à charge légère | DCDC → LDO | Moyenne inchangée (361-422 µA), juste forme de pics différente — **écarté**, revenu à DCDC. Résultat utile : confirme que le courant est réel, pas un artefact de régulateur |
| 6 | Erratum Nordic [114] GPIO « Wake on pin » (~300 µA documentés) | `nrf_gpio_port_detect_latch_set(NRF_P0, true)` | Aucun effet (277 µA, idem) — **écarté** (mais gardé : correctif officiel sans contrepartie) |
| 7 | Erratum Nordic [37] POWER (System OFF trop tôt après reset) | Écriture registre `0x5005340C=1` | Aucun effet mesurable dans le lot combiné avec #6 — **gardé** (correctif officiel, régression connue sur NCS 3.4.0 signalée par Nordic elle-même) |
| 8 | Durée de coupure d'alimentation trop courte | 2-3 s → ≥30 s | Aucun effet — **écarté** |
| 9 | Mode de mesure PPK2 (Source Meter) | *Hypothèse avancée puis retirée* | L'utilisateur rappelle que la même méthode a détecté un vrai ~100x sur le projet frère — la méthode n'est pas en cause |
| 10 | BLE/contrôleur radio (MPSL) | `bt_enable()` jamais appelé, aucune trame envoyée | **Pire, pas meilleur** (381 µA, gros pics ~1 s) — **BLE exclu comme cause dominante** |
| 11 | IMU **et** BLE tous les deux coupés (carte nue, SoC+GRTC seul) | Les deux désactivés en même temps | Baseline très améliorée (~19 µA sur fenêtre courte) **mais** gros pics périodiques (jusqu'à 17 mA) toutes les ~1,4-1,5 s persistent — voir § suivant, **pas encore pleinement interprété** |

## Dernier résultat obtenu (2026-08-27, fin de session — à interpréter en priorité)

Deux captures PPK2 sur le build « carte nue » (`DIAG_NO_RADIO_TEST=1` +
`DIAG_NO_IMU_TEST=1`, ni IMU ni BLE) :

- Fenêtre courte (588 ms) : moyenne **18,91 µA**, max 27,73 µA — la
  baseline hors pic est enfin dans un ordre de grandeur proche du budget
  calculé SoC seul (~1-2 µA, encore ~10-19x au-dessus mais énorme
  amélioration par rapport aux ~250-400 µA avec IMU+BLE actifs).
- Fenêtre large (3 s) : moyenne **115,78 µA**, max **17,45 mA**, avec
  des pics nets très espacés (~1,4-1,5 s d'écart, visibles 3 fois sur la
  fenêtre) qui tirent la moyenne vers le haut malgré une baseline propre
  entre les pics.

**Ce que ça prouve** : la baseline élevée (~250-400 µA en continu) vue sur
tous les tests précédents était bien majoritairement due à l'IMU et/ou au
BLE (baseline hors-pic quasi normale une fois les deux coupés) — mais un
**phénomène périodique indépendant** (gros pic ~1,4-1,5 s d'écart, jusqu'à
17 mA) persiste même sans IMU ni BLE. Ce phénomène est donc dans le socle
SoC/GRTC/retained_mem/erratums lui-même, pas dans le code applicatif IMU
ou BLE.

**Hypothèse à vérifier en priorité à la prochaine session** : la carte
effectue un cycle complet (réveil → travail → `sys_poweroff()`) beaucoup
plus souvent que prévu (santé 15 min / heartbeat 60 min), au lieu de
rester en vrai System OFF entre les échéances. L'écart ~1,4-1,5 s est
proche du plancher de `z_nrf_grtc_wakeup_prepare()` (1 s, codé en dur dans
`main.c`) — une piste consistant à relever temporairement ce plancher à
une valeur très supérieure (ex. 300 s) avait été envisagée mais **retirée
sur consigne explicite de l'utilisateur avant d'être testée** — à
réévaluer ou remplacer par une autre méthode d'investigation (ex. compter
les cycles via un compteur en RAM retenue lu par SWD avec prudence, voir
note de fiabilité ci-dessous, plutôt que redeviner un correctif).

## Note de fiabilité — diagnostic par SWD

Un échantillonnage direct du CPU via `openocd halt` + lecture registre a
été utilisé une fois (2026-08-27) et a montré le CPU dans la boucle idle
du noyau Zephyr 4 fois sur 5 pendant que la carte tournait. **Cette
méthode n'est pas fiable à 100 %** : le simple fait d'attacher un débogueur
SWD peut lui-même maintenir la carte en « Debug Interface mode »
(mécanisme déjà documenté dans `xiao_nrf54lm20a_project_notes.md` § Procédure
— flasher) et fausser ce qui est observé. À utiliser avec cette réserve
explicite, jamais comme preuve définitive seule.

## État du firmware sur la carte à la fin de cette session

`DIAG_NO_RADIO_TEST=1` et `DIAG_NO_IMU_TEST=1` (haut de `main.c`) — **ce
build ne fonctionne pas comme un capteur réel** (IMU et BLE coupés,
aucune trame envoyée). Logging désactivé (définitif), `CONFIG_SERIAL`/
`CONFIG_CONSOLE=n` (définitif), ODR 12,5 Hz, `WAKE_UP_THS=0x01`,
`vregmain=DCDC`, erratums Nordic 114 et 37 appliqués (définitifs, sans
effet négatif connu). **Remettre les deux `DIAG_*` à 0 dès que le
diagnostic carte-nue est exploité** (réintroduire IMU seule, puis BLE
seule, un test à la fois) pour revenir à un firmware fonctionnel.

## Prochaines pistes à tester (par ordre de priorité suggéré)

1. **Interpréter le pic ~1,4-1,5 s carte nue** (§ ci-dessus) — comprendre
   s'il s'agit d'un cycle de reboot répété (RRAM write, erratum register
   write, ou autre) avant de continuer à réintroduire IMU/BLE.
2. **Réintroduire l'IMU seule** (`DIAG_NO_IMU_TEST=0`, BLE restant coupé)
   — mesurer, comparer à la baseline carte-nue pour quantifier la
   contribution réelle de l'IMU.
3. **Réintroduire le BLE seul** (`DIAG_NO_RADIO_TEST=0`, IMU restant
   coupé) — idem pour quantifier la contribution réelle du BLE, sachant
   que le couper seul avait empiré les choses (résultat contre-intuitif
   à comprendre, pas juste à constater).
4. **Compléter le budget PMIC nPM1300** (plancher `IQBAT`=0,8 µA connu
   comme incomplet — ne compte pas la consommation des régulateurs
   LDO1/BUCK sous charge réelle).
5. Une fois la carte nue propre et comprise : repasser en **Ampere
   meter** pour une mesure finale fiable (recommandé mais plus bloquant
   au stade actuel du diagnostic).
6. Envisager de pousser (`git push`) le dépôt `xiao-door` une fois du
   code stable à sauvegarder — pas fait à ce jour, seulement sur demande
   explicite.
