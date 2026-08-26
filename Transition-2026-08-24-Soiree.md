# Transition — session du 2026-08-24 (fin d'après-midi/soir)

**Document de démarrage pour une nouvelle conversation.** Contient
uniquement des faits vérifiés dans cette session — aucune supposition.
Voir `C:\ncs\CLAUDE.md` (règles de travail, à lire en premier) et
`xiao_nrf54lm20a_project_notes.md` (référence complète du projet).

## Unité #1 (D2:3A:F7:B1:E8:18, sonde debug `C5F0E209`, port COM3)

### État du firmware au moment de la rédaction

Dernier flash effectué : firmware avec les correctifs ci-dessous appliqués.
Câble USB-C débranché/rebranché après ce flash. **Confirmé par
l'utilisateur ensuite : état HA normal après le cycle d'alimentation, puis
un mouvement provoqué délibérément a produit un comportement normal dans
HA.** Pas de capture série indépendante faite en parallèle (rapport direct
de l'utilisateur uniquement) — le port COM3 de l'unité #1 a été vérifié
présent côté Windows au moment de la confirmation.

### Correctifs appliqués et vérifiés aujourd'hui (dans l'ordre)

1. **Historique de diagnostic persistant** (`diag_log_*` dans `main.c`) :
   anneau de 20 entrées en RAM retenue (survit au System OFF, pas à une
   coupure d'alimentation complète), affiché en entier à chaque boot.
2. **Reboot automatique sur échec d'init** (`init_imu()`/`bt_enable()`) :
   remplace un `return 0` silencieux (qui laissait la carte bloquée sans
   source de réveil) par `sys_reboot(SYS_REBOOT_COLD)`. Nécessite
   `CONFIG_REBOOT=y` (ajouté à `prj.conf`). **Déclenché une fois en usage
   réel aujourd'hui** (échec I2C IMU transitoire, `-EIO`) et a fonctionné
   comme prévu (reboot propre, pas de blocage).
3. **`log_panic()` avant poweroff/reboot — ajouté puis retiré**. Ajouté
   pour éviter la perte de logs en mode différé. **A causé un blocage
   reproductible** confirmé par lecture directe des registres via sonde
   SWD : PC bloqué dans `uarte_nrfx_poll_out()`
   (`zephyr/drivers/serial/uart_nrfx_uarte.c:2611`), boucle d'attente
   avec interruptions masquées (`BASEPRI_MAX`), reproductible à
   l'identique après un cycle d'alimentation complet de 30+ secondes.
   **Retiré des 3 sites d'appel** (`main.c`) ; après retrait, le PC lu en
   direct pointait vers `nrfx_grtc_syscounter_get()` (fonction normale de
   lecture du compteur GRTC, pas un point de blocage) — comportement sain
   confirmé.
4. **Dump des octets bruts de la trame A** (`LOG_HEXDUMP_INF`) avant
   l'envoi radio. **Vérifié bit pour bit** à plusieurs reprises contre les
   valeurs affichées dans HA (pitch/roll identiques à l'octet près).
5. **Seuil de réveil relevé puis annulé** : essai `WAKE_UP_THS=0x04`
   (~125 mg) + debounce `WAKE_UP_DUR=0x40` — la carte ne répondait plus à
   un mouvement réel de test. **Reverti aux valeurs d'origine**
   (`WAKE_UP_THS=0x01`, `WAKE_UP_DUR=0x00`), identiques aux unités #2/#3.
6. **`REST_FRAME_DELAY_MS` réduit de 15000 à 10000** (préférence
   utilisateur).
7. **`REST_FRAME_MAX_WAIT_MS=30000` ajouté + trame "repos" exemptée du
   limiteur de débit** (`frame_a_rate_limited()`). Root cause identifiée :
   `rest_since` ne redémarre qu'au dernier `moving=true` réel, et pouvait
   être repoussé indéfiniment par des déclenchements répétés ; de plus,
   `frame_a_rate_limited()` bloquait aussi l'envoi de la trame "repos"
   elle-même si beaucoup de trames étaient déjà parties dans la minute
   précédente — HA restait alors bloqué sur "Détecté" sans limite de
   temps. **Ce correctif vient d'être flashé, pas encore vérifié en
   conditions réelles.**
8. **Bouton (0x3A) + yaw (0x3F #3) — implémentés puis intégralement
   revertis**. La tentative a causé un HardFault confirmé par lecture
   directe des registres (`PC=0xeffffffe`, message OpenOCD "clearing
   lockup after double fault"). Cause exacte non identifiée avant le
   revert. **Sauvegarde du code défaillant** :
   `xiao_door_sensor/src/main_button_yaw_debug_2026-08-24.c.bak`, pour
   reprise ultérieure. État actuel : bouton et yaw non implémentés,
   envoyés à 0 (comme avant toute tentative).
9. **Chute/choc (0x2B) et double-tap (0x2C) : jamais tentés aujourd'hui**,
   reportés délibérément — touchent les mêmes registres IMU
   (`TAP_CFG`/`MD1_CFG`) que le réveil GPIO validé.

### Comportements confirmés par test direct aujourd'hui

- Réveil GRTC périodique (trame santé/heartbeat) : déclenché à l'heure
  prévue, plusieurs fois, sans erreur (`ret_gpio=0 ret_grtc=0`).
- Réveil GPIO sur mouvement : déclenché correctement pour un mouvement
  franc **et** pour deux mouvements volontairement lents/doux (amplitude
  d'angle ~13,9° et ~2,7° respectivement) — les deux ont réveillé la
  carte et remonté dans HA.
- Anomalie mineure non résolue : un second réveil GRTC
  (`reset_cause=0x800`) survient systématiquement ~2s après un réveil
  GRTC normal, sans nouvelle trame ni erreur visible. Cause non
  identifiée.
- **Écart non résolu, non expliqué** : LED de charge du PMIC observée
  **éteinte** en continu alors que la carte tourne sur USB seul, sans
  batterie (confirmé par l'utilisateur) — contredit le comportement
  documenté antérieurement (LED censée être allumée dans cette
  configuration). Puce vérifiée alimentée et fonctionnelle par ailleurs
  (sonde SWD, BLE/IMU/HA tous vérifiés indépendamment le même
  après-midi). Sans impact fonctionnel connu à ce jour.

## Unités #2 et #3

- **N'ont reçu aucun des correctifs du jour** — firmware antérieur à
  cette session, inchangé.
- Unité #3 (`E6:C9:11:CE:6E:C6`, sonde `4587B5C1`) : déplacée après 2h30
  d'inactivité par l'utilisateur, remontée avec succès dans HA (rapporté
  par l'utilisateur, non vérifié indépendamment par capture série).
- Unité #2 (`DE:F6:A3:A9:0F:0F`, sonde `9C4A557D`) : test prévu vers
  17:00 par l'utilisateur — résultat non rapporté dans cette conversation
  au moment de la rédaction.

### Point non résolu : incohérence de numérotation avec un document antérieur

Le fichier `devices/unit-01/RMA-unit-01.md` du dépôt GitHub (rédigé le
17/08/2026, pendant un projet Matter antérieur et distinct — voir
ci-dessous) documente un **IMU défectueux** (n'initialise jamais) sur la
carte de sonde debug `9C4A557D`, et cite `C5F0E209` comme unité de
référence saine. La table "Unités déployées" de ce projet-ci (BLE)
assigne `9C4A557D` à l'unité #2 et `C5F0E209` à l'unité #1 — donc le
défaut IMU documenté concernerait l'unité #2, pas #1. **L'utilisateur a
affirmé explicitement aujourd'hui que "unit-01" (Matter) et "unité #1"
(BLE, `C5F0E209`) sont la même carte physique**, ce qui contredit
directement les numéros de série cités dans le rapport RMA. Ni l'un ni
l'autre n'a été retranché — les deux affirmations existent telles
quelles dans les documents/la conversation, non réconciliées.

## Dépôt GitHub (`https://github.com/thieryus007-cloud/xiao-door`)

- Restructuré aujourd'hui : contenu Matter Door Lock
  (`firmware/apps/lock/`, `docs/01-priorite-1-matter-lock.md`,
  `docs/04-auth-architecture.md`, `docs/XIAO-Door-specs.md`,
  `docs/00-etape-0-environnement.md`) retiré de `main`, préservé
  intégralement sur la branche poussée `archive/matter-lock`.
- `firmware/apps/xiao_door_sensor/` ajouté (source BLE) — **synchronisé
  avec l'état du firmware avant les correctifs #3, #6, #7 ci-dessus**
  (log_panic retiré, délai 10s, filet de sécurité repos) : **pas encore
  re-synchronisé avec le tout dernier code**.
- `xiao_nrf54lm20a_project_notes.md`, `README.md`,
  `devices/unit-0X/unit-0X.md`, `devices/README.md` mis à jour pour
  refléter le projet BLE.
- Docs BLE pré-existants conservés tels quels (non modifiés aujourd'hui) :
  `XIAO-nRF54LM20A-BTHome-HA-v5.md`, `capteur-angle-porte-nRF52840-BTHome.md`,
  `PPK-Mesures-Transition.md`.

## Fichiers locaux modifiés aujourd'hui

- `C:\ncs\projects\xiao_nrf54lm20a_project_notes.md` — tableau des phases
  de transmission, valeurs des tests de sensibilité, plan de préparation
  bouton/yaw/chute-choc/double-tap.
- `C:\ncs\projects\Diagnostic-Silence-Unites-Transition.md` — journal de
  diagnostic de cette session (silence initial, correctifs, suite du
  test).
- `C:\ncs\CLAUDE.md` — deux règles ajoutées à la demande explicite de
  l'utilisateur : (1) ne jamais reposer une question déjà répondue dans
  la conversation ; (2) ne jamais proposer d'explication qui contredit ce
  que l'utilisateur affirme, notamment pour reporter la faute ailleurs
  que sur du code venant d'être modifié.

## À faire, dans l'ordre, en reprenant cette conversation

1. ✅ **Fait** — état HA normal après le cycle d'alimentation post-flash,
   confirmé par l'utilisateur (pas de capture série indépendante, rapport
   direct de l'utilisateur).
2. ✅ **Fait** — mouvement provoqué délibérément par l'utilisateur,
   comportement normal confirmé dans HA (correctif #7 validé en usage
   réel pour ce cas). Non couvert explicitement : le cas rafale (plusieurs
   trames dans la minute précédente) — pas testé à ce stade.
3. Resynchroniser `firmware/apps/xiao_door_sensor/src/main.c` sur GitHub
   (point 2 validé pour le cas simple).
4. Résultat du test de l'unité #2 (prévu 17:00) à obtenir de
   l'utilisateur.
5. Valider pendant la nuit que les trois unités restent réactives (pas de
   blocage), objectif explicite de l'utilisateur pour cette soirée.
6. Reprendre bouton (avec réveil System OFF associé, demandé par
   l'utilisateur — "bouton d'appel"), yaw, chute/choc, double-tap — code
   de départ pour bouton/yaw dans le fichier `.bak` cité en §1.8, cause du
   HardFault à investiguer avant de retenter.
