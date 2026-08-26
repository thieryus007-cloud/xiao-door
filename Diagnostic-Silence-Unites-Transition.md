# Transition — diagnostic du silence des 3 unités déployées (2026-08-24)

**Document de démarrage pour une nouvelle conversation.** Objectif :
diagnostiquer pourquoi les 3 unités XIAO déployées (firmware System OFF,
flashé le 2026-08-30/toutes validées fonctionnelles à cette date) ne
remontent plus aucune information dans Home Assistant depuis le
2026-08-24, malgré un mouvement physique délibéré du capteur.

## Règle de travail — identique au reste du projet

Voir `C:\ncs\CLAUDE.md` : communiquer avant d'agir, vérifier l'état réel
avant de supposer. **Contexte du projet complet** (build/flash/vérifier,
architecture, budget énergétique) : `xiao_nrf54lm20a_project_notes.md`
— à lire en premier, ne pas dupliquer ici.

**Consigne explicite de l'utilisateur (2026-08-24)** : ne pas remettre en
cause Home Assistant comme cause possible — concentrer l'investigation
sur le firmware/le device. Une vérification de connectivité réseau vers
la VM HA (192.168.1.10) et le proxy ESPHome (192.168.1.20) a été
commencée puis explicitement interrompue par l'utilisateur pour cette
raison — ne pas la reprendre sans qu'il le demande.

## Symptôme

- Le 2026-08-24, l'utilisateur a bougé physiquement le XIAO actuellement
  branché en USB sur le PC — **aucune information n'est remontée dans
  HA**.
- L'utilisateur rapporte que **les deux autres unités déployées sont
  "dans le même état"** (non remontées dans HA) — observation de
  l'utilisateur, non vérifiée indépendamment par une capture série sur
  ces deux autres unités (pas physiquement accessibles depuis ce poste
  au moment de la rédaction).
- L'utilisateur qualifie l'unité connectée de "semble figé" — sans
  précision supplémentaire obtenue sur un signe concret (LED, chaleur,
  etc.) au moment de la rédaction de ce document.

## Ce qui est confirmé à ce jour

- **Toutes les unités fonctionnaient correctement au moment du dernier
  test validé** (2026-08-30) : réveil matériel sur mouvement
  (`gpio_wake=1`), trames A/B/C reçues par HA, retour en System OFF —
  voir historique de conversation ou `xiao_nrf54lm20a_project_notes.md`.
- **Unité actuellement branchée identifiée** : unité #1
  (`D2:3A:F7:B1:E8:18`, série pont `C5F0E209`), port **COM3**.
- **Capture série de 90s sur l'unité #1, immédiatement après un
  mouvement délibéré demandé à l'utilisateur** : silence total, 0 ligne
  reçue (ni boot, ni trame). Ambigu par nature — un vrai System OFF sans
  événement produit exactement le même silence qu'un blocage réel ; les
  deux sont indiscernables par la seule écoute passive du port série.
- **Hypothèse vérifiée et écartée** : pas d'incohérence d'unités entre
  `z_nrf_grtc_timer_read()` (lecture du compteur GRTC) et
  `z_nrf_grtc_wakeup_prepare()` (armement du réveil). Vérifié en lisant
  `C:\ncs\v3.4.0\zephyr\drivers\timer\nrf_grtc_timer.c:412-448` (la
  fonction convertit explicitement `wake_time_us` en cycles via
  `sys_clock_hw_cycles_per_sec()`) et la config réelle du build
  (`build/xiao_door_sensor/zephyr/.config` :
  `CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC=1000000`) — 1 tick GRTC = 1 µs
  exactement dans notre configuration, le code de `main.c` qui traite
  les deux valeurs comme des microsecondes est donc correct sur ce point
  précis.
- **Nouveau, 2026-08-24 (après la capture ci-dessus)** : un cycle
  d'alimentation complet de l'unité #1 (câble USB-C débranché puis
  rebranché — pas juste un reset) l'a fait réapparaître dans HA. Les
  deux autres unités restent invisibles à ce stade car leur alimentation
  n'a pas encore été débranchée (seule condition testée jusqu'ici sur
  l'unité #1). Ce test est plus concluant que la piste 1 ci-dessous
  (reset OpenOCD seul, jamais fait) : il écarte un CPU réellement
  planté/bloqué (le firmware boote et fonctionne correctement après un
  vrai POR) et pointe vers un System OFF entré **sans aucune source de
  réveil active** — cohérent avec un échec silencieux de
  `arm_gpio_wake()` et/ou `z_nrf_grtc_wakeup_prepare()` en fin de cycle
  précédent (piste 2, `main.c:997-1016` — gestion d'échec déjà identifiée
  comme `LOG_ERR` seul, sans repli ni retry). Hypothèse renforcée, pas
  encore prouvée par un log réel côté firmware.

## Pistes à investiguer (par ordre de priorité)

1. ~~Test le plus basique~~ — **fait, 2026-08-24, via un test plus concluant
   qu'un reset OpenOCD** : cycle d'alimentation complet (débranchement/
   rebranchement USB-C) de l'unité #1 → la carte boote et redevient visible
   dans HA. Conclusion : le CPU n'est pas planté, le firmware fonctionne
   correctement après un vrai POR. Le System OFF précédent n'avait
   simplement plus aucune source de réveil active — voir piste 2, devenue
   prioritaire.
2. **Gestion d'échec silencieuse dans `main()` — priorité actuelle** : `z_nrf_grtc_wakeup_prepare()`
   et `arm_gpio_wake()` (`xiao_door_sensor/src/main.c`, fin de `main()`)
   se contentent aujourd'hui d'un `LOG_ERR` en cas d'échec, **sans repli
   ni retry** — si l'un des deux échoue en usage réel (ex.
   `-EINVAL`/`-ENOMEM` sur `z_nrf_grtc_wakeup_prepare()` si
   `minimum_latency_us > wake_time_us`, ou tout autre cas non exercé
   pendant les tests courts d'hier), le firmware entre quand même en
   System OFF avec **une seule source de réveil active** (voire aucune)
   — plausible explication d'un silence permanent malgré un mouvement
   réel, si c'est la source GPIO qui a échoué à s'armer. À vérifier :
   ajouter un log temporaire de la valeur exacte de `wake_in_us` calculée
   et des codes de retour des deux fonctions, juste avant
   `sys_poweroff()`, puis reflasher et observer sur plusieurs cycles
   réels (pas juste le premier boot).
3. **Comportement non exercé pendant les tests d'hier** : les tests de
   validation du 2026-08-30 ont surtout enchaîné des réveils GPIO
   rapprochés (secousses manuelles répétées) — le chemin de réveil GRTC
   périodique réel (échéance à 15 min / 60 min avec un vrai temps écoulé
   du monde réel, pas un test compressé) n'a été observé qu'une poignée
   de fois. Un bug spécifique à ce chemin (calcul d'échéance, réarmement
   après un réveil GRTC) pourrait n'apparaître qu'après un fonctionnement
   prolongé en conditions réelles — cohérent avec un problème constaté
   le lendemain (2026-08-24) du déploiement (2026-08-30), après
   plusieurs cycles GRTC réels sur chaque unité.
4. **Vérifier l'état retenu** : si possible sans perturber davantage
   l'unité #1, envisager d'inspecter ce que contient `retained_mem` au
   moment présent (nécessiterait probablement un attach debug avec
   lecture mémoire, à réfléchir avant d'agir — pas fait à ce stade).

## Test en cours (démarré 2026-08-24, après reflash avec historique diag)

- Firmware reflashé sur l'unité #1 avec l'ajout d'un historique de
  diagnostic persistant (anneau de 20 entrées en RAM retenue, survit au
  System OFF mais pas à une coupure d'alimentation complète — voir
  `main.c`, `struct diag_log` / `diag_log_append()` / `diag_log_dump()`,
  ajouté 2026-08-24). Chaque entrée capture `reset_cause`/`cold_boot`/
  `gpio_wake` du boot, et `wake_in_us`/codes retour de `arm_gpio_wake()`
  et `z_nrf_grtc_wakeup_prepare()` armés pour le cycle suivant. Affiché
  en entier (`diag_log_dump()`) à chaque boot, donc à chaque réveil réel
  (System OFF = redémarrage complet du firmware) pendant la période
  d'observation.
- Cycle d'alimentation complet (débranché/rebranché) effectué après le
  flash, comme requis.
- Mouvement délibéré déclenché par l'utilisateur juste après le
  rebranchement, pour vérifier le réveil GPIO sur ce nouveau firmware.
- **Unité #1 reste branchée en continu sur le PC (COM3)**, positionnée
  à un endroit sans aucun mouvement, pour isoler le chemin de réveil
  GRTC périodique (piste 3) sans interférence GPIO — pas de coupure
  d'alimentation prévue pendant cette phase, donc l'historique persistant
  ne devrait subir aucune perte.
- **Attendu si le firmware fonctionne normalement** : trame B (batterie)
  toutes les 15 min ± jitter ±30s, sans autre trame tant qu'aucun
  mouvement ne se produit — à vérifier dans HA après 1-2h.
- ⚠️ **COM3 est réservé à l'unité #1 pour toute la durée de ce test** —
  ne pas le réutiliser ni le cibler par erreur en travaillant sur les
  unités #2/#3 (instruction explicite de l'utilisateur, 2026-08-24).
- Prochaine vérification prévue par l'utilisateur : dans 1-2h, retour
  voir si l'unité a répondu normalement (trames B régulières dans HA,
  et/ou capture série de l'historique diag accumulé).

## Suite du test (2026-08-24, après-midi)

- **Unité #1** : plusieurs cycles complets confirmés en direct (réveil GRTC
  autonome après un vrai sommeil profond, trames motion/repos avec octets
  bruts vérifiés bit pour bit contre HA) — voir correctifs appliqués
  ci-dessous.
- **Unité #3** : déplacée après 2h30 d'inactivité, remontée avec succès
  dans HA.
- **Unité #2** : test prévu vers 17:00 cet après-midi (déplacement après
  une période d'inactivité, même protocole).

### Correctifs appliqués sur l'unité #1 pendant ce test

- Historique de diagnostic persistant (anneau de 20 entrées en RAM
  retenue) + affichage complet à chaque boot (`diag_log_*`, `main.c`).
- `log_panic()` avant `sys_poweroff()` : les logs de fin de cycle étaient
  perdus (mode différé, tampon jamais vidé avant le reset matériel) —
  corrigé, permet maintenant de voir la trame "repos" et l'entrée en
  System OFF de façon fiable.
- `log_panic()` + `sys_reboot()` au lieu de `return 0` silencieux sur
  échec de `init_imu()`/`bt_enable()` — un échec sur ce chemin laissait la
  carte bloquée indéfiniment (pas un vrai System OFF, aucune source de
  réveil armée, log perdu) ; reboot automatique à la place.
- Dump des octets bruts de la trame A (`LOG_HEXDUMP_INF`) avant l'envoi
  radio — vérifié bit pour bit contre les valeurs affichées dans HA,
  concordance exacte confirmée.
- Essai d'un seuil de réveil relevé (`WAKE_UP_THS`/`WAKE_UP_DUR`) abandonné
  et **revenu aux valeurs d'origine** (identiques aux unités #2/#3) après
  que la carte ne répondait plus à un mouvement réel avec le seuil relevé.
- Anomalie mineure toujours présente, non bloquante : un second réveil
  GRTC parasite ~2s après chaque réveil GRTC normal, sans conséquence
  observée (pas de nouvelle trame, pas d'erreur).
- Point non résolu : une trame B (santé) est restée non reflétée dans HA
  pendant ~27 minutes à un moment du test — pas encore instrumentée avec
  un dump d'octets bruts comme la trame A, donc pas encore élucidé.

## Écart non expliqué, non bloquant (2026-08-24, fin d'après-midi)

LED de charge PMIC de l'unité #1 observée **éteinte** en continu alors
qu'elle est alimentée en USB seul, **sans batterie connectée** (confirmé
par l'utilisateur) — contredit le comportement documenté plus haut
(§ « Firmware déployé », LED censée être allumée en continu dans cette
configuration, éteinte seulement une fois une vraie batterie LiPo
branchée). Puce vérifiée alimentée et fonctionnelle par ailleurs (sonde
SWD non destructive : `Examination succeed` à deux reprises ; BLE/IMU/HA
tous vérifiés indépendamment le même après-midi, y compris octets bruts
bit à bit). Cause non identifiée — LED pilotée uniquement par le PMIC en
hardware, aucun code de ce firmware ne la contrôle (`leds_off()` ne touche
que les LED GPIO). Sans impact connu sur le fonctionnement du capteur à
ce jour ; à investiguer si le temps le permet, pas prioritaire.

## Ce qu'il ne faut pas refaire sans notification claire

- Ne pas re-flasher une unité sans en informer l'utilisateur au
  préalable (impact : coupe l'unité de HA pendant le test, comme à
  chaque flash).
- Débrancher/rebrancher l'USB-C reste nécessaire après toute session
  OpenOCD (mode debug, voir `xiao_nrf54lm20a_project_notes.md`).
- Ne pas conclure à un problème HA/réseau sans que l'utilisateur le
  redemande explicitement (consigne donnée le 2026-08-24, voir plus haut).
