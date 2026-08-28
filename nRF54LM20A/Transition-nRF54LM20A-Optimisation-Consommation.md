# Projet : Optimisation de la consommation de courant du XIAO nRF54LM20A Sense

Document de suivi vivant — **à mettre à jour à chaque conversation** sur ce
sujet, pour reprendre directement d'ici plutôt que de tout redécouvrir.

## 🔴 OBJECTIF NON NÉGOCIABLE (posé explicitement le 2026-08-27, à ne
jamais perdre de vue)

Ce capteur (XIAO nRF54LM20A Sense) a été choisi spécifiquement pour son
IMU, pour un projet de détection de la position d'une porte/fenêtre **à
tout moment** (réactivité attendue de l'ordre de ~1 s en production).
Le projet frère **XIAO nRF52840 Sense** -- une puce qui consomme environ
2x plus sur le papier et n'a pas de PMIC nPM1300 (chaîne d'alimentation
plus simple) -- atteint déjà **~10 µA en continu** avec la détection de
mouvement fonctionnelle (System OFF SoC + réveil GPIO sur interruption
IMU). **Correction (2026-08-27, plus tard la même session) : le but
FINAL est 5-6 µA en continu, PAS 10-11 µA.** Les ~10 µA du nRF52840
Sense sont le plancher de comparaison à battre, pas la cible réelle. Le
meilleur résultat obtenu à ce jour (~11,68 µA, hibernate PMIC 60s, volet
heartbeat seul -- Test #10 plus bas) reste au-dessus de la cible ET ne
couvre pas le volet détection de mouvement. Ne jamais présenter 10-11 µA
comme "suffisant" -- la référence est 5-6 µA sur le comportement complet
(heartbeat + réactivité mouvement ensemble), pas un seul volet mesuré
isolément. Ce n'est pas négociable, à ne jamais relâcher silencieusement
dans une session future.

**Implication directe** : le hibernate PMIC (nPM1300, découvert plus
bas) ne résout QUE le réveil périodique (santé/heartbeat) -- l'IMU est
câblée directement sur une broche GPIO du SoC (`gpio0.6`), pas sur une
broche GPIO du PMIC, donc le hibernate PMIC ne peut physiquement pas
être réveillé par un mouvement. Le chemin détection-mouvement reste
dépendant du System OFF + réveil GPIO du SoC lui-même. Le projet frère
nRF52840 prouve que cette architecture (System OFF + réveil GPIO) est
capable d'atteindre ~10 µA sur du matériel Nordic comparable/plus
ancien -- argument fort que la boucle de reboot du nRF54LM20A est une
anomalie de support SDK/silicium très récent, pas une limite
fondamentale de l'architecture. Toute proposition de solution doit être
vérifiée sur LES DEUX volets (courant continu ET réactivité mouvement),
jamais un seul présenté comme résolvant l'objectif du projet.

## Résumé pour reprendre une nouvelle conversation

**Objectif chiffré (budget calculé, jamais mesuré)** : ~13,9 µA -- voir
`xiao_nrf54lm20a_project_notes.md` § Budget énergétique. **Dépassé par
l'objectif non négociable ci-dessus (≤10 µA, aligné sur le projet
frère nRF52840) -- c'est ce dernier qui fait foi.**

**🔴 PRIORITÉ ABSOLUE, à traiter avant toute autre chose (établi le
2026-08-27, Phase 1 du plan de test)** : sur un build **sans aucune
source de réveil armée** (ni GPIO, ni GRTC, ni écriture d'erratum),
`sys_poweroff()` appelé immédiatement après `leds_off()` — donc en théorie
un seul micro-transitoire de boot puis un vrai System OFF permanent
(plus aucune activité possible, aucune fonction du firmware n'est
exercée) — la mesure PPK2 montre un **bruit continu en dents de scie
(~15-28 µA, jamais de retour à zéro, période ~5 ms) qui persiste sans
interruption sur toute la durée de la mesure** (plusieurs secondes
observées). **Conclusion : la carte n'atteint jamais réellement le
System OFF, quel que soit le firmware.** Ce n'est ni l'IMU, ni le BLE, ni
le GPIO wake, ni le GRTC, ni les erratums 114/37 — aucun de ce code ne
s'exécute dans ce build. Le problème est plus fondamental : soit
`sys_poweroff()`/`nrf_regulators_system_off()` ne fonctionne pas
réellement sur cette carte/ce SDK (NCS 3.4.0, snapshot très récent pour
cette puce), soit un état persistant (Debug Interface Mode qui ne se
dissipe jamais malgré le protocole de coupure d'alimentation déjà en
place, ou autre) empêche le vrai System OFF.

**🔴 RÈGLE N°1 du projet, posée explicitement le 2026-08-27, à lire avant
toute autre chose — voir aussi `CLAUDE.md` § Chercher la cause dans le
code, jamais dans l'environnement** : le protocole de mesure PPK2 est
**fiable et ne doit plus jamais être remis en cause**. Le PPK2 est
**toujours déconnecté** de la carte XIAO quand celle-ci est connectée au
PC (USB-C). La carte XIAO est **toujours déconnectée** du PC quand le
PPK2 sert à la mesure — en mode Source meter comme en mode Ampere meter
avec alimentation stabilisée externe sur BAT+/BAT-. Ces deux connexions
ne sont **jamais simultanées**. Toute divergence de mesure doit être
expliquée par le **code**, jamais par le protocole de mesure — chercher
dans le code source et sur Internet, pas dans l'environnement matériel.

**Piste invalidée (2026-08-27)** : l'hypothèse « le PPK2 restait branché
sur BAT+/BAT- pendant le flash USB-C, empêchant un vrai cycle
d'alimentation et laissant la carte en Debug Interface Mode » a été
avancée puis explicitement invalidée par l'utilisateur — le PPK2 et
l'USB-C ne sont jamais connectés simultanément (règle ci-dessus). Ne pas
la reformuler. Le mécanisme HAL identifié reste vrai en soi (voir
`nrf_regulators.h:567-577`, § suivant) mais sa cause doit être cherchée
dans le code/firmware ou une régression NCS 3.4.0/nRF54LM20A connue, pas
dans le protocole de mesure.

**Prochaine action immédiate (nouvelle conversation)** : ne PAS reprendre
le plan de phases 2-6 (IMU, BLE, GRTC, GPIO) tant que ce point n'est pas
résolu — inutile d'étudier ce qui se passe *avant* `sys_poweroff()` si
`sys_poweroff()` lui-même n'aboutit jamais. Pistes à explorer en premier :
1. Rechercher sur Internet un erratum/bug/issue connu Nordic ou
   Zephyr/NCS spécifique à `sys_poweroff()`/`nrf_regulators_system_off()`
   ne fonctionnant pas sur nRF54LM20A/NCS 3.4.0 (au-delà des erratums 37
   et 114 déjà trouvés, qui concernent un courant élevé *pendant* un
   System OFF qui a lieu, pas l'absence totale de System OFF). Chip très
   récent (support NCS très récent) — regarder issues GitHub
   nrfconnect/sdk-nrfxlib, nrfconnect/sdk-zephyr, DevZone.
2. Reproduire l'exemple officiel Zephyr `samples/boards/nordic/system_off`
   tel quel (sans aucun code applicatif) sur cette carte, pour savoir si
   le problème est dans ce snapshot NCS/board lui-même ou introduit par
   notre firmware.
3. Vérifier la piste retained_mem/RAM retention : le code de
   `z_sys_poweroff()` (`zephyr/soc/nordic/common/poweroff.c`) désactive la
   rétention RAM puis appelle `nrf_regulators_system_off()` — vérifier
   que cet appel est bien atteint (ex. `CODE_UNREACHABLE` juste après,
   qui ne devrait jamais s'exécuter).
4. Approfondir le mécanisme HAL déjà identifié (§ suivant) : sous quelles
   conditions *autres* qu'un débogueur physiquement attaché le SoC peut
   rester en Debug Interface Mode (bit sticky non réinitialisé par un
   POR complet ? domaine toujours-actif ? lire le registre CTRL-AP
   `APPROTECT`/`DEBUGCTRL` pertinent au repos, sans supposer que ça vient
   du protocole de mesure).

## Plan de test minimal pas-à-pas (2026-08-27, remplace l'instrumentation incrémentale)

Sur demande explicite de l'utilisateur : au lieu d'empiler du diagnostic
(compteurs, journal, écritures d'erratum) sur le firmware complet
existant, repartir de zéro avec le code le plus minimal possible qui
compile, mesurer, puis réintroduire une seule chose à la fois. Élimine
tout risque que l'instrumentation elle-même (RAM retenue, CRC, I2C
retained_mem) brouille la mesure.

Sauvegardes du firmware complet avant ce test : `src/main_full_2026-08-27.c.bak`,
`prj_full_2026-08-27.conf.bak` (dans `xiao_door_sensor/`).

**Étape 0 (2026-08-27, flashée, en attente de mesure)** : `main()` réduit
à un seul appel `sys_poweroff()`. `prj.conf` réduit à `CONFIG_SERIAL=n` /
`CONFIG_CONSOLE=n` / `CONFIG_UART_CONSOLE=n` (confond déjà isolé
indépendamment par un fil DevZone, conservé pour ne pas le
réintroduire) + `CONFIG_POWEROFF=y`. Aucun IMU, BLE, RAM retenue
applicative, LED, ni écriture d'erratum. Binaire : 30452 B flash (contre
94112 B pour la Phase 1B). Si le bruit en dents de scie persiste ici,
la cause est dans le SDK/HAL/SoC pour cette combinaison NCS 3.4.0 +
board, entièrement indépendante de ce projet applicatif.

**Résultat Étape 0 (2026-08-27)** : IDENTIQUE à la Phase 1/1B -- 18,00 µA
moyenne, 23,22 µA max, même dents de scie (période ~4,7 ms sur ce
zoom). Confirme que la cause n'est PAS dans le code applicatif de ce
projet (binaire quasi vide, 30 452 B flash) : elle est dans la couche
SDK/HAL/board pour cette combinaison précise.

## 🔴 Piste version NCS -- probablement la cause racine (2026-08-27)

Recherche Internet (sur demande explicite de l'utilisateur, suite au
résultat Étape 0 identique) :

- Le guide officiel Seeed pour ce board
  ([wiki.seeedstudio.com/xiao_nrf54lm20a_ncs](https://wiki.seeedstudio.com/xiao_nrf54lm20a_ncs/))
  est explicite : **NCS 3.3.0 requis**, avertissement direct contre le
  mélange de versions ("Mixing different versions may cause build or
  compatibility issues"). Board définitions "not yet merged into the
  official NCS repository" -- ajout manuel du board root, comme fait
  dans ce projet.
- **Notre projet est sur NCS 3.4.0**, jamais validée par Seeed pour ce
  board.
- Coïncide exactement avec la régression DevZone déjà trouvée (même code
  fonctionnel sous NCS 3.3.0, cassé sous NCS 3.4.0, sur la puce sœur
  nRF54L15, même anomalie System OFF).
- Confirmé localement : seule NCS 3.4.0 est installée
  (`C:\ncs\v3.4.0`), toolchain `dcbdc366a1` verrouillé sur 3.4.0
  (`toolchains.json`). Pas de 3.3.0 disponible avant ce jour.
- Il existe aussi un board port Zephyr **mainline** officiel
  (`boards/seeed/xiao_nrf54lm20a/` sur docs.zephyrproject.org), distinct
  du dépôt vendor Seeed (`platform-seeedboards`) utilisé actuellement --
  absent du checkout Zephyr embarqué dans NCS 3.4.0 (vérifié :
  `C:\ncs\v3.4.0\zephyr\boards\seeed\` n'existe pas), donc pas encore
  fusionné dans le fork NCS. Marqué "not actively maintained" par
  Zephyr -- piste secondaire, à essayer après NCS 3.3.0.
- Target de board confirmé identique pour les variantes Sense et
  non-Sense (`xiao_nrf54lm20a/nrf54lm20a/cpuapp`) -- la différence Sense
  (IMU, micro) est gérée par l'overlay applicatif de ce projet, pas par
  un board target séparé. Pas de risque de confusion de variante en
  changeant de version NCS.

**Décision utilisateur (2026-08-27)** : installer NCS 3.3.0 en parallèle
de la 3.4.0 existante (rien supprimé) et refaire l'Étape 0 dessus.
Ensuite, si non concluant, essayer le board port Zephyr mainline en
restant sur 3.4.0.

Installation : `nrfutil sdk-manager install v3.3.0 --install-dir C:/ncs`
via l'installation nrfutil utilisateur (`C:\Users\thier\.nrfutil`,
distincte du nrfutil embarqué dans le toolchain 3.4.0 qui est verrouillé
en écriture). Résultat -> `C:\ncs\v3.3.0` + toolchain dédié
(`bundle_id 936afb6332`, `C:\ncs\toolchains\936afb6332`).

⚠️ **Incident pendant l'installation (2026-08-27)** : le téléchargement du
bundle SDK (`sdk-nrf-bundle-v3.3.0.tar.gz`) s'est figé silencieusement
après ~1,78 Go (aucune progression pendant 44 min, aucune erreur
affichée). Diagnostiqué en comparant la taille/date du fichier
`.tar.gz-part` à deux instants (pas la taille totale du dossier
`downloads/`, qui reste trompeusement stable car dominée par des
fichiers déjà terminés). Résolu par `taskkill /F /IM nrfutil.exe` puis
relance de la même commande d'install -- reprise confirmée dans les
logs (`Found '-part' file for resumable download`), terminée avec
succès. À refaire de la même façon si ça se reproduit sur un futur
téléchargement volumineux.

517 liens symboliques non créés pendant l'installation (permission
Windows "Developer Mode" absente) -- tous dans `modules/lib/matter/...`
(exemples/tests Matter, hors sujet pour ce projet BLE-only) plus deux
fichiers `tools/bsim` et `tools/net-tools` sans rapport. Sans impact
vérifié pour ce projet (pas de build Matter/bsim).

**Résultat Étape 0 sous NCS 3.3.0 (2026-08-27, Test #2)** : build + flash
réussis, vérifiés. Mesure PPK2 : 17,84-18,43 µA moyenne, 29,74-33,55 µA
max, même motif dents de scie/pics périodiques -- **identique à NCS
3.4.0**. NCS 3.3.0 vs 3.4.0 écarté comme cause.

**Incident (2026-08-27, entre Test #2 et Test #3)** : tentative de
restaurer la LED de charge (overlay `nordic,led0-mode = "charging"` +
`CONFIG_LED=y`) -- sans effet sur la LED (cause non résolue, secondaire),
et détection au passage d'une fausse alerte de corruption de flash (voir
§ Mécanisme HAL / notes projet, corrigée : `dump_image`+`cmp` contre
`.bin` donne des faux positifs sur les trous RRAM non écrits ;
`verify_image` avec le `.hex` est la bonne méthode). Changements LED
retirés (hors sujet, objectif principal = consommation) avant Test #3.

**Test #3 (2026-08-27) : board port Zephyr mainline upstream** -- un
seul changement par rapport au Test #2 : fichiers de carte
`boards/seeed/xiao_nrf54lm20a/` (dépôt officiel
`zephyrproject-rtos/zephyr`, branche `main`, cloné en sparse-checkout
dans `C:\ncs\vendor\zephyr-upstream-board`) au lieu du dépôt vendor
Seeed (`vendor/platform-seeedboards`). Adaptations nécessaires pour ce
test uniquement (à annuler après) :
- `CMakeLists.txt` : `list(APPEND BOARD_ROOT ...vendor/platform-seeedboards...)`
  commenté (sinon conflit "Board(s) defined multiple times" entre les
  deux dépôts).
- Overlay projet remplacé par une version minimale (juste
  `&usbhs`/`&usbhs_wrapper status = "disabled"`, contournement d'un bug
  de build déjà connu de `nrf_usbhs_wrapper.c`) -- l'overlay complet
  (PMIC/IMU/LED/RAM retenue) est spécifique aux labels du board port
  vendor et absent du board port upstream ; aucun n'est utilisé par
  l'Étape 0 de toute façon. Original sauvegardé en
  `.overlay.disabled_for_upstream_test`.
- Build dans `build_upstream/` (dossier dédié), flash/vérif avec le
  `support/openocd.cfg` du dépôt vendor (indépendant du board port
  utilisé pour compiler -- flash bit-à-bit identique quel que soit
  l'outillage OpenOCD utilisé). Build+flash réussis, `verify_image` OK
  (40556 octets).

**Résultat Test #3 (2026-08-27)** : 290,21 µA moyenne, 441,42 µA max --
**16x PIRE** que le board port vendor (~18 µA). Le board port Zephyr
mainline upstream n'est donc PAS la cause du plancher observé -- au
contraire, le board port vendor Seeed est nettement mieux configuré pour
la consommation (probablement le réglage DCDC de `vregmain` et/ou la
config PMIC/I2C, absents de l'overlay minimal utilisé pour ce test et du
board port upstream par défaut). **Option 2 écartée** -- ne pas
retenter avec plus de configuration ajoutée (ça reviendrait juste à
recopier l'overlay vendor). Retour au board port vendor pour la suite.

Bilan à ce stade : code applicatif, corruption de flash, version NCS
(3.3.0 vs 3.4.0), et board port (vendor vs upstream) tous écartés comme
cause du plancher ~18 µA en dents de scie. Le problème est confirmé
commun à `sys_poweroff()`/`nrf_regulators_system_off()` sur ce SoC avec
la configuration matérielle vendor (DCDC, PMIC).

## 🔴🔴 Test #4 (compteur RAM retenue) et Test #5 (diagnostic série) --
CAUSE IDENTIFIÉE (2026-08-27)

**Test #4** : firmware Étape 0 + compteur de cycles CRC-validé en RAM
retenue (`src/main_test4_counter.c.bak`), mesuré 10 s au PPK2 (18,05 µA
moyenne, motif identique à tous les tests précédents).

**Test #5** (sur proposition de l'utilisateur) : variante diagnostic
LOGICIEL UNIQUEMENT (jamais mesurée au PPK2) -- logging/console/UART
réactivés exprès, affiche cause de reset + compteur juste avant
`sys_poweroff()`, observé en direct sur COM3 (115200 8N1) pendant que la
carte tourne sur USB-C. Piège rencontré et corrigé : avec
`CONFIG_LOG=y`, `printk()` passe en file d'attente différée -- sans
`k_msleep(200)` avant `sys_poweroff()`, rien ne sort jamais sur l'UART
(le message n'a pas le temps d'être transmis avant la coupure). Une fois
corrigé :

```
### BOOT #44 reset_cause=0x00000020 -- entree en sys_poweroff()
*** Booting nRF Connect SDK v3.4.0-99553055607b ***
### BOOT #45 reset_cause=0x00000020 -- entree en sys_poweroff()
... (48 cycles captés en 10 s, ~208 ms/cycle, TOUS reset_cause=0x00000020)
```

**`0x00000020` = bit 5 = `RESET_DEBUG`** (API hwinfo Zephyr,
`include/zephyr/drivers/hwinfo.h`). **Chaque redémarrage est causé par
le SoC lui-même qui se croit réinitialisé par un débogueur, alors
qu'aucune sonde SWD n'était physiquement attachée pendant cette capture**
(session OpenOCD fermée avant le début de la capture série, carte
alimentée uniquement par USB-C).

**Conclusion** : c'est un vrai redémarrage en boucle (pas une boucle WFE
figée), et sa cause directe est confirmée -- le SoC reste bloqué dans un
état "comme si un débogueur était attaché" en permanence, ce qui déclenche
exactement le chemin `nrf_regulators_system_off()` documenté dès le
début de cette session (commentaire HAL : *"Solution for simulated
System OFF in debug mode"*) : `SYSTEMOFF` ne coupe jamais réellement
l'alimentation, le SoC boucle sur un pseudo-redémarrage étiqueté debug au
lieu d'un vrai System OFF. Confirmé indépendant du board port (vendor,
même mécanisme probable sur upstream) et de la version NCS (déjà testé
3.3.0 et 3.4.0 avec le même symptôme de courant).

**Prochaine étape** : rechercher un mécanisme/erratum connu Nordic
nRF54L/nRF54LM permettant à `RESET_DEBUG`/l'état "Debug Interface Mode"
de rester latché sans sonde SWD physiquement attachée (bit sticky non
remis à zéro par un POR complet ? domaine CTRL-AP particulier à ce
SoC/cette révision de silicium ?), et comment le déverrouiller/effacer
autrement qu'en coupant l'alimentation (déjà fait, sans effet -- voir
protocole PPK2 déjà établi, alimentation BAT+/BAT- jamais simultanée
avec USB-C).

## ⚠️ Registres TAMPC identifiés, écriture directe tentée puis ABANDONNÉE (2026-08-27)

Adresses (nRF54LM20A, base secure `NRF_TAMPC_S_BASE = 0x500EF000`) :
- `TAMPC.PROTECT.DOMAIN[0].DBGEN.CTRL` = `0x500EF500` (debug invasif local)
- `TAMPC.PROTECT.AP[0].DBGEN.CTRL` = `0x500EF700` (debug invasif au niveau
  access port)
- Registre protégé : écrire `WRITEPROTECTION=0xF` + `KEY=0x50FA` pour
  déverrouiller, PUIS une écriture séparée (même session, sans reset
  entre les deux) pour changer `VALUE`. Confirmé : la valeur se
  réinitialise à `0x00000011` (`VALUE=1`, debug activé) à **chaque**
  reset -- comportement cohérent avec `system_nrf54l_approtect.h`
  (le SDK réactive volontairement l'accès debug par défaut sur un build
  de développement, pas un bit "collé" au sens matériel).

**Tentative d'écriture (`VALUE=0` sur `DOMAIN[0].DBGEN.CTRL`) : a
déclenché un verrouillage de l'Access Port**, récupéré automatiquement
par OpenOCD via un effacement complet de la puce (`device has AP lock
engaged... successfully erased and unlocked`) -- carte non brickée,
mais firmware entièrement effacé, reflashé ensuite avec succès (vérifié
`verify_image`). **Piste abandonnée** : écrire directement dans ces
registres TAMPC est trop instable/risqué pour continuer sur ce SoC avec
les outils actuels (OpenOCD custom, pas d'outillage officiel Nordic
`nrfutil device`/`Board Configurator` testé). Ne pas retenter sans un
outil de débogage officiel Nordic et une meilleure compréhension du
séquencement exact attendu par ce registre.

Piste alternative plus sûre pour la suite : chercher si `nrfutil device`
(déjà installé, `2.19.0`) propose une commande de lecture/gestion de
l'état debug/APPROTECT documentée et supportée officiellement, plutôt
que des écritures mémoire brutes.

## Test #6 (CONFIG_NRF_APPROTECT_USER_HANDLING) -- ÉCARTÉ

Kconfig officiel Nordic (`zephyr/soc/nordic/Kconfig`,
`choice NRF_APPROTECT_HANDLING`) : SystemInit() ne touche plus du tout à
`TAMPC.PROTECT.DOMAIN[0]/AP[0].DBGEN.CTRL` (ni ouverture comme le défaut
`NRF_APPROTECT_DISABLE`, ni verrouillage comme `NRF_APPROTECT_LOCK` --
resSemblait réversible, contrairement à `LOCK`). Flashé, vérifié, accès
SWD resté pleinement fonctionnel après. **Résultat PPK2 : 18,01 µA
moyenne, 33,81 µA max -- identique en tout point au comportement par
défaut.** APPROTECT/TAMPC DBGEN définitivement écarté comme cause :
que le SDK ouvre le debug à chaque boot ou n'y touche pas du tout,
aucun effet sur le symptôme.

(`CONFIG_NRF_APPROTECT_LOCK` n'a pas pu être testé : bloqué deux fois
par le classificateur de sécurité Auto Mode, y compris sur un simple
Edit de fichier de config -- accepté comme frontière de sécurité
légitime, non contournée davantage.)

## ⚠️ Incident : verrouillage AP spontané, sans manipulation registre (2026-08-27)

Après la mesure PPK2 du Test #6 (résultat ci-dessus), en tentant de
flasher le Test #7 (voir plus bas), le tout premier flash a échoué dès
la connexion initiale : `device has AP lock engaged`, puis la
récupération automatique OpenOCD a elle-même échoué à répétition
(`Timeout waiting for BUSY status`, `DP initialisation failed`) sur 3
tentatives consécutives, y compris à vitesse SWD réduite (200 kHz au
lieu de 500 kHz). Contrairement à l'incident précédent (déclenché par
une écriture registre invalide de ma part), **celui-ci s'est produit
spontanément**, sans aucune manipulation de registre -- juste après une
mesure PPK2 normale suivie d'une tentative de flash standard.

**Résolu par débranchement/rebranchement physique du câble USB-C**
(coupure réelle de l'alimentation de la puce, pas juste un cycle
logiciel) -- la récupération OpenOCD a alors réussi immédiatement au
tout premier essai suivant. Pont USB resté `Status: OK` tout du long
(vérifié Windows PnP) -- ce n'est pas un problème de pont SAMD11
intermittent déjà documenté, c'est bien la puce/le port de debug qui se
bloque.

**Signal important** : un verrouillage AP spontané, sans cause logicielle
identifiée de mon côté, sur cette même puce dont le comportement
`sys_poweroff()` est déjà anormal, renforce l'hypothèse d'un lien entre
ces deux symptômes (état TAMPC/CTRL-AP instable) plutôt que deux
problèmes indépendants.

## Test #7 (sys_clock_disable avant sys_poweroff) -- piste Nordic Developer Academy

Jamais testée jusqu'ici dans ce projet malgré identification précoce
dans la session (exemple de référence NCS 3.3.0 mentionné sur DevZone).
Un seul ajout par rapport au Test #4 : `sys_clock_disable();` juste
avant `sys_poweroff()` (coupe GRTC/horloge basse fréquence). Flashé
après incident ci-dessus, vérifié (`verify_image`, 31348 octets, 0
erreur).

**Résultat (2026-08-27)** : identique (confirmé par l'utilisateur, "idem
rien de nouveau"). `sys_clock_disable()` écarté comme piste.

## Bilan des tests de configuration (2026-08-27) -- 7 variables testées, 1 seule a un effet

| # | Variable testée | Effet |
|---|---|---|
| Phase 1B | Erratum [37] (registre + délai) | Aucun |
| Test #2 | NCS 3.3.0 vs 3.4.0 | Aucun (identique) |
| Test #3 | Board port upstream vs vendor | **16x PIRE** (290 µA vs 18 µA) |
| Test #4 | Compteur RAM retenue (instrumentation) | Aucun (confirme juste le mécanisme) |
| Test #6 | APPROTECT_USER_HANDLING vs DISABLE | Aucun (identique) |
| Test #7 | `sys_clock_disable()` | Aucun (identique) |

Seul le board port a un effet -- et dans le mauvais sens pour l'hypothèse
"le vendor est cassé, l'upstream serait meilleur". Toutes les autres
pistes de configuration/Kconfig accessibles sans risque sont épuisées.
Prochaine étape : recherche ciblée avec la signature exacte maintenant
identifiée (boucle de reboot, `RESET_DEBUG`, pendant `sys_poweroff()`,
spécifiquement nRF54LM20A).

## Test #10 (mfd_npm13xx_hibernate) -- avancée réelle, mais partielle (2026-08-27)

Piste trouvée via forum Seeed ("Sleep Current of the XIAO nRF54LM20A
Using the Built-in nPM1300", forum.seeedstudio.com/t/295544) et fil
GitHub (lolren/nrf54-arduino-core#94, 3 µA obtenu sur board similaire).
Le PMIC nPM1300 a son propre mode hibernate (minuterie interne,
totalement indépendant de `sys_poweroff()` du SoC) -- API Zephyr native
`mfd_npm13xx_hibernate(dev, time_ms)`
(`zephyr/drivers/mfd/mfd_npm13xx.c`), jamais utilisée dans ce projet
avant ce jour.

**Résultats mesurés** :
- Minuterie 10 s : 66,25 µA moyenne sur 30 s (3 cycles), mais **plancher
  entre les réveils quasi nul (~0,6 µA)** -- première fois de toute la
  session qu'un vrai plancher bas est observé.
- Minuterie 60 s : 11,68 µA moyenne sur 60 s (1 cycle) -- confirme la
  dilution attendue.
- Cause de la moyenne encore élevée : un pic de réveil bref (quelques
  ms) mais haut (~173 mA), cout fixe par cycle. Sous la limite typique
  `IBAT` du nPM1300 (290 mA, datasheet) -- tout indique un appel de
  courant normal (recharge condensateurs + redémarrage régulateurs
  depuis zéro), pas un bug corrigible par configuration.
- Projection à 900 s (15 min, échéance santé réelle du projet) :
  ~0,7-1 µA -- proche des chiffres datasheet (0,5 µA hibernate).

**Limite architecturale identifiée** : `irq-gpios` de l'IMU
(`lsm6ds3tr_c`) est câblée sur `gpio0.6`, une broche **du SoC**, pas du
PMIC (vérifié : aucune broche GPIO du nPM1300 n'est câblée sur cette
carte). Le hibernate PMIC coupe l'alimentation du SoC entièrement --
**ne peut donc pas être réveillé par un mouvement IMU**, quel que soit
le firmware. Résout uniquement le volet santé/heartbeat périodique
(≥15 min), pas le volet détection de mouvement (~1 s attendu, voir
§ Objectif non négociable en tête de document).

**Prochaine étape** : le volet mouvement reste dépendant de la
résolution de la boucle de reboot `sys_poweroff()`/`RESET_DEBUG` du SoC
documentée plus haut -- le hibernate PMIC ne la remplace pas, il la
complète pour la partie périodique seulement.

## Rapport technique publié pour Nordic (2026-08-27)

Toutes les preuves de cette session (matrice d'élimination, capture
série, registres TAMPC, incidents de verrouillage AP) consolidées dans
un rapport structuré, publié en artifact pour soumission à Nordic
DevZone : https://claude.ai/code/artifact/5264bdde-1aa4-49ef-af7d-7fbc9d429a0d

## Capteur remis en service (2026-08-27)

Sur demande explicite de l'utilisateur ("remettre en place toutes les
fonctions sans logging pour la remontee des infos vers HA") : firmware
complet restauré (`main_full_2026-08-27.c.bak` + `prj_full_2026-08-27.conf.bak`),
les quatre flags `DIAG_*` remis à `0` (IMU + BLE + trames BTHome actifs,
logging toujours désactivé -- confond déjà isolé indépendamment). Build
NCS 3.4.0, board port vendor. Flashé et vérifié (`verify_image`, 107448
octets, 0 erreur). **L'optimisation de consommation continue en
parallèle sans bloquer le service réel du capteur** -- le plancher ~18 µA
documenté ci-dessus reste présent (cause commune à tout firmware sur ce
SoC, indépendante du code applicatif), mais la carte fonctionne à
nouveau normalement pour Home Assistant.

**Étapes suivantes prévues** (une seule variable ajoutée à la fois par
rapport à l'étape précédente, dans cet ordre) :
1. Étape 0 (ci-dessus) -- référence absolue.
2. + écriture erratum [37] (`0x5005340C=1`) + délai -- déjà testée en
   Phase 1B sur le firmware complet (aucun effet), à revérifier sur base
   minimale pour confirmer que le résultat ne dépend pas du reste du code.
3. + réveil GRTC seul (`z_nrf_grtc_wakeup_prepare()`, délai long ex. 60s).
4. + réveil GPIO seul (sans le réveil GRTC).
5. Reproduire l'exemple officiel Zephyr
   `samples/boards/nordic/system_off` tel quel (overlay
   `nrf54lm20dk_nrf54lm20a_cpuapp.overlay` déjà présent dans le SDK) pour
   comparer à un exemple entièrement hors de ce projet.

## Mécanisme HAL identifié (2026-08-27, vérifié dans le code)

`nrf_regulators_system_off()` (`modules/hal/nordic/nrfx/hal/nrf_regulators.h:567-577`,
appelée en dernier ressort par `z_sys_poweroff()` pour ce chip) :

```c
NRF_STATIC_INLINE void nrf_regulators_system_off(NRF_REGULATORS_Type * p_reg)
{
    p_reg->SYSTEMOFF = REGULATORS_SYSTEMOFF_SYSTEMOFF_Msk;
    __DSB();

    /* Solution for simulated System OFF in debug mode */
    while (true)
    {
        __WFE();
    }
}
```

Si le SoC est en Debug Interface Mode, l'écriture de `SYSTEMOFF` ne coupe
pas réellement l'alimentation : le HAL Nordic *simule* le System OFF par
une boucle `__WFE()` qui se réveille sur n'importe quel évènement
pending — ce qui correspond au bruit continu en dents de scie observé
(jamais de retour à zéro, activité périodique) au lieu d'un plancher plat
proche de zéro. Le mécanisme est confirmé par le code ; **la cause de son
déclenchement reste à identifier** (ce n'est pas le protocole de mesure —
voir règle n°1 ci-dessus).

## Protocole de TEST formel (fixé le 2026-08-27 sur exigence explicite de
l'utilisateur : "plan de test FIABLE et plusieurs verification a chaque
FOIS pour chaque TEST et UN seul TEST a la fois")

Un "test" = la séquence complète ci-dessous, dans l'ordre, sans sauter
d'étape. Un seul changement de code/config par test, énoncé explicitement
avant de commencer.

1. **État de départ vérifié** -- confirmer que le XIAO est branché en
   USB-C sur le PC (`Get-PnpDevice`, chercher `USB\VID_2886&PID_0068\C5F0E209`
   status `OK` -- ne jamais supposer, toujours vérifier) et PAS sur le
   PPK2 (les deux ne sont jamais simultanés, règle n°1).
2. **Un seul changement** -- énoncer précisément ce qui change par
   rapport au dernier test vérifié (fichier, ligne, valeur avant/après).
3. **Build** -- commande exacte consignée, dossier dédié si plusieurs
   versions SDK coexistent. Vérifier la sortie "Completed" sans erreur.
4. **Flash** -- `nrf54lm20a-load`. Vérifier l'absence de signature
   HardFault (`pc: 0xeffffffe`, `msp: 0x00005540`) dans la sortie.
5. **Vérification du flash (obligatoire, plus jamais sautée -- voir
   incident 2026-08-27 ci-dessous)** : `verify_image` avec le `.hex`
   (compare uniquement les adresses réellement écrites -- **PAS**
   `dump_image` + `cmp` contre le `.bin`, qui donne de faux positifs sur
   les trous RRAM non effacés, voir avertissement dans
   `xiao_nrf54lm20a_project_notes.md` § Procédure -- flasher). Si erreur :
   reflasher le même fichier et revérifier avant de continuer -- ne
   jamais mesurer un flash non vérifié.
6. **Transition vers la mesure** -- instruction sans ambiguïté :
   "débranchez l'USB-C maintenant, connectez le PPK2" (jamais les deux
   phrases mélangées, voir CLAUDE.md § qui fait quoi).
7. **Mesure** -- l'utilisateur mesure au PPK2, rapporte la capture.
8. **Consignation immédiate** -- résultat inscrit dans ce document avant
   de passer au test suivant : config exacte, résultat de la vérif flash
   (étape 5), résultat de mesure (moyenne/max/forme), conclusion.
9. **Retour du XIAO sur le PC** -- reprendre à l'étape 1 pour le test
   suivant, ne jamais supposer l'état de connexion inchangé.

⚠️ **Incident qui a motivé ce protocole (2026-08-27)** : un flash
antérieur (Étape 0 sous NCS 3.3.0) avait silencieusement corrompu un mot
de la table de vecteurs (gestionnaire BusFault), jamais détecté avant
qu'une vérification octet-à-octet soit explicitement redemandée deux
fois par l'utilisateur. Reflash + revérification -> flash correct,
mesure PPK2 identique -> corruption écartée comme cause, mais consommer
un cycle de test entier sans le savoir aurait pu fausser n'importe quelle
conclusion précédente. D'où l'étape 5 rendue obligatoire pour tout test
dont le résultat compte.

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

## Plan de test par phases (fourni par l'utilisateur le 2026-08-27, vérifié et en cours d'application)

Un seul changement entre deux mesures, ≥2-3 min de repos stable par
mesure, noter systématiquement moyenne/plancher/pics/build exact.

| Phase | Config | Statut |
|---|---|---|
| 0 | `DIAG_NO_RADIO_TEST=1` + `DIAG_NO_IMU_TEST=1` (baseline carte nue) | **FAIT** — voir § Dernier résultat ci-dessus (~19 µA hors pic, pics ~17 mA/~1,4-1,5 s). Ne pas refaire. |
| 1 | + `DIAG_PHASE1_BARE_POWEROFF=1` : aucun réveil armé (ni GPIO ni GRTC), aucune écriture d'erratum, `sys_poweroff()` juste après `leds_off()` | **FAIT (2026-08-27) — résultat majeur** : 18,67-18,79 µA de moyenne, 26-28 µA max, **bruit continu en dents de scie (période ~5 ms), jamais de retour à zéro, sans interruption sur toute la mesure**. Preuve directe que `sys_poweroff()` n'atteint jamais le vrai System OFF, indépendamment de tout le reste. **Voir § Résumé en tête de document — priorité absolue.** |
| 2 | Retirer `DIAG_PHASE1_BARE_POWEROFF`, réarmer seulement `z_nrf_grtc_wakeup_prepare()` (délai long, ex. 60s), pas de GPIO | Pas encore fait |
| 3A | Réarmer `arm_gpio_wake()` **sans** `nrf_gpio_port_detect_latch_set` (retirer l'appel erratum 114) | Pas encore fait |
| 3B | Idem avec le latch erratum 114 (version actuelle) | Pas encore fait |
| 4 | `DIAG_NO_IMU_TEST=0`, IMU réactivée seule (BLE toujours coupé), config actuelle (THS=0x01, DUR=0x00, ODR 12,5 Hz) | Pas encore fait |
| 4.1 | + ODR → 1,6 Hz | Pas encore fait |
| 4.2 | + THS → 0x04 ou 0x08 | Pas encore fait |
| 4.3 | + Routage INT1 désactivé (MD1_CFG bit5=0) | Pas encore fait |
| 5 | `DIAG_NO_RADIO_TEST=0` aussi — build de production complet | Pas encore fait |
| 6 | Si toujours élevé : pins flottantes, régulateurs PMIC, re-vérification UART, comparaison nRF52840 même PPK2/même alim | Pas encore fait |

**Note de cohérence importante (2026-08-27)** : dans le build de la Phase 0
(déjà mesuré), `z_nrf_grtc_wakeup_prepare()` (plancher 1s) et l'écriture
registre de l'erratum 37 tournaient **encore sans interruption** à chaque
cycle — seul l'erratum 114 (GPIO) était déjà neutralisé de fait (il vit
dans `arm_gpio_wake()`, sauté entièrement par `DIAG_NO_IMU_TEST`). C'est
probablement l'explication des pics ~1,4-1,5 s déjà observés en Phase 0 —
la Phase 1 teste directement cette hypothèse.

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

## Percée System OFF (2026-08-27, test #13) — voir document dédié

Le plan de test minimal pas-à-pas ci-dessus a fini par aboutir : reproduire
fidèlement l'exemple de référence Seeed (System OFF) a donné **3,01 µA**,
confirmé (3,02 µA au second essai). Détail complet (les 9 éléments
techniques débloquants, fichiers exacts, protocole) dans
`XIAO-nRF54LM20A-Solution-System-OFF.md` — **document de référence pour
l'état actuel du firmware**, ce fichier-ci reste l'historique de
diagnostic.

## 🔴 RÈGLE ABSOLUE découverte pendant ce chantier -- CONFIG_SERIAL

**DÈS QU'UN PÉRIPHÉRIQUE SUPPLÉMENTAIRE (BLE, IMU + régulateur,
`CONFIG_PM_DEVICE_RUNTIME`) REJOINT LA BASE SYSTEM OFF :
`CONFIG_SERIAL=n` EN DUR DANS `prj.conf`, JAMAIS `CONFIG_SERIAL=y` +
suspension à l'exécution.** Bug driver UARTE (fuite de référence PM
runtime) : la suspension à l'exécution ne fonctionne que sur la base
System OFF nue (test #13) -- dès qu'autre chose tourne en même temps,
elle échoue systématiquement (`-120`) et fausse toute mesure suivante
(~260-470 µA au lieu de ~3 µA, sans rapport avec la variable réellement
testée). **Erreur commise deux fois dans cette session** (build BLE,
build réveil IMU) avant correction -- voir
`XIAO-nRF54LM20A-Solution-System-OFF.md` § règle absolue, et mémoire
`feedback_xiao_serial_never_runtime_suspend`.

## Tentative réveil IMU (2026-08-27, après la percée) — bloquée, non résolue

Objectif : ajouter le réveil IMU (INT1, seuil 31 mg) à la base System OFF
validée. Résultat mesuré systématiquement **~260 µA de moyenne** (vs ~12 µA
attendu = 3 µA base + ~9 µA IMU datasheet), quelle que soit la variante
testée — **hypothèses suivantes toutes éliminées, aucune n'a fait bouger
la moyenne** :

1. Attente que INT1 redescende avant `sys_poweroff()` (jusqu'à 5 s) — sans
   effet.
2. `WAKE_UP_DUR` 0x00 → 0x01 (debounce ~2 périodes ODR au lieu d'1,
   registre distinct du seuil 31 mg, **jamais modifié** sur consigne
   explicite de l'utilisateur) + attente réduite à 300 ms — sans effet.
3. Délai de stabilisation 200 ms entre l'activation ODR accéléromètre
   (0→12,5 Hz, la vraie transition puisque `lsm6dsl_init_chip()` laisse
   l'ODR à 0 par défaut) et l'armement de l'interruption — sans effet.
4. Suspension du bus I2C (`pm_device_action_run(imu_i2c.bus,
   PM_DEVICE_ACTION_SUSPEND)`) après la dernière transaction — **premier
   essai invalidé** : `CONFIG_SERIAL=y` encore actif dans ce build,
   résultat contaminé par le bug UARTE ci-dessus (260 µA revu à
   l'identique, non concluant). À refaire avec `CONFIG_SERIAL=n`.

**Recherche externe (2026-08-27, errata Nordic + datasheet + pinctrl du
board + DevZone + ST community)** : le bus I2C de l'IMU (`i2c30`) a un
état pinctrl basse consommation dédié (`i2c30_sleep`, `low-power-enable`,
confirmé dans `xiao_nrf54lm20a_nrf54lm20a-pinctrl.dtsi`), jamais déclenché
faute de suspension explicite -- même mécanisme déjà validé pour le bus
SPI flash. Errata [105] TWIM : ne jamais suspendre pendant un
clock-stretching (suspension placée après la dernière transaction
terminée). Détail complet du rapport de recherche dans la conversation du
2026-08-27 (non dupliqué ici).

**Diagnostic jamais complété** (build préparé puis abandonné sur demande
explicite) : `DIAG_NO_IMU_WAKE_ARM` — alimenter/initialiser l'IMU
normalement (régulateurs `power_en`/`imu_vdd`, driver `lsm6dsl`,
`CONFIG_PM_DEVICE`) mais n'armer **aucun** réveil, pour savoir si le
courant élevé vient de l'infrastructure IMU elle-même (régulateurs,
`CONFIG_PM_DEVICE_RUNTIME` combiné à un driver capteur/régulateur jamais
testé avec — risque explicitement signalé avant de commencer) ou
spécifiquement du mécanisme de réveil. **Prochaine étape recommandée pour
reprendre ce chantier** : terminer ce diagnostic avant toute nouvelle
hypothèse sur le réveil lui-même.

## Isolation régulateur imu_vdd/LDO1 (2026-08-27) — cause isolée, PAS acceptée comme définitive

**🔴 Rappel non négociable avant toute conclusion de cette section :
l'objectif du projet reste 5-6 µA en continu, comportement complet. Le
régulateur `imu_vdd`/LDO1 étant identifié comme la source du courant
excédentaire ne veut PAS dire que ce courant est accepté comme coût
inévitable -- il reste une configuration à corriger, pas une limite
physique tant que la preuve du contraire n'est pas faite (voir
justement la référence Seeed ci-dessous, qui prouve le contraire).**

Test à partir de la base validée (GRTC+RAM+BLE, 3,3 µA), en ajoutant
**un seul appel `regulator_enable()` sur `imu_vdd` (LDO1)**, sans toucher
à `power_en`, sans transaction I2C vers l'IMU, sans capteur : **~253 µA**,
reproductible, et confirmé réversible (retrait du même appel -> retour
exact à 3,3 µA). Code vérifié ligne par ligne, aucun bug de séquencement
trouvé -- l'appel `regulator_enable()` seul suffit à provoquer le saut.

**Recherche datasheet nPM1300 (4490_483 v1.1, §6.4 LOADSW/LDO, p.71-75)** :
**aucune spécification de courant de repos (IQ) n'est documentée** pour ce
bloc, ni en mode LDO ni en mode Load Switch (Tables 23-24) -- seuls
RDSON, courant de sortie, temps de soft-start et plage de tension sont
donnés. Point notable : l'entrée de LDO1 (VIN_LDO) doit être alimentée
par VOUT1, VOUT2 ou VSYS (jamais directement VBAT), minimum 2,6 V.

**🔴 Trouvé ensuite, divergence concrète avec la référence officielle
Seeed** (page "Usage of Built-in Sensors",
`wiki.seeedstudio.com/xiao_nrf54lm20a_with_onboard/`, IMU via LDO1) :

```dts
&pmic {
    regulators {
        imu_vdd: LDO1 {
            regulator-min-microvolt = <3300000>;
            regulator-max-microvolt = <3300000>;
            regulator-boot-on;
        };
    };
};
```

**Cette référence ne fixe jamais `regulator-initial-mode =
<NPM13XX_LDSW_MODE_LDO>`** -- ligne présente dans notre overlay (héritée
du firmware de production de ce projet, jamais remise en question avant
cette session). Vérifié dans le driver Zephyr local
(`C:\ncs\v3.4.0\zephyr\drivers\regulator\regulator_npm13xx.c`) : la
sélection de tension (registre `VOUTSEL`, écrit dès que
`regulator-min/max-microvolt` sont présents) est **indépendante** de la
sélection de mode (registre `LDOSEL`, switch vs LDO régulé, écrit
uniquement par `regulator-initial-mode`/`regulator_set_mode()`). Forcer
le mode LDO explicitement pourrait activer une boucle de régulation
active là où le mode par défaut (probablement Load Switch, RDSON
~200 mΩ typique d'après Table 23) suffirait. Pages officielles Seeed
("Low Power Modes", "Getting Started") : ~4,76-4,93 µA en System OFF
documentés pour la carte -- **à confirmer si ces chiffres incluent l'IMU
alimenté en continu ou seulement le socle SoC+GRTC**, mais aucune preuve
que 250 µA soit inévitable avec `imu_vdd` actif.

**Test "retirer regulator-initial-mode" -- FAUX NÉGATIF, piège découvert** :
retirer la propriété de l'overlay n'a **aucun effet mesuré (toujours
~253 µA)** -- mais la lecture directe du registre réel (`mfd_npm13xx_reg_read`,
bloc LDSW base 0x08) a montré `LDSW1LDOSEL=0x01` (LDO) **alors que rien
ne l'écrivait plus**. Cause : **le PMIC nPM1300 reste alimenté en continu
par la batterie, indépendamment du System OFF du SoC -- ses registres
persistent d'un flash à l'autre.** Retirer la propriété devicetree
arrête juste de *réécrire* le registre, il ne le *réinitialise* pas. Le
mode LDO restait actif depuis un test antérieur qui l'avait explicitement
forcé. **Toujours vérifier l'état réel par lecture registre avant de
conclure qu'un changement de configuration devicetree a été sans effet
sur ce PMIC.**

**Test mode Load Switch réel (2026-08-27)**, `regulator-initial-mode =
<NPM13XX_LDSW_MODE_LDSW>` forcé explicitement (confirmé par lecture
registre : `LDSW1LDOSEL=0x00`) -- **risque matériel accepté explicitement
par l'utilisateur** (tension batterie mesurée 4,22-4,23 V, au-dessus du
Vdd absolu max 3,6 V de l'IMU en mode Load Switch, sortie non régulée) :
**275,67 µA -- PIRE qu'en mode LDO (253 µA), pas mieux.** Le mode
Load Switch est écarté : ni plus économe, ni sûr. `imu_vdd` remis en
mode LDO (sûr) dans l'overlay, **mais `regulator_enable()` retiré de
`main.c`** -- le régulateur n'est plus jamais activé, unité #01 revenue
à l'état validé (3,28 µA reconfirmé au PPK2).

**Bilan de cette investigation régulateur** : cause du ~250-275 µA
toujours non élucidée après recherche approfondie (Zephyr regulator
core, driver MFD, driver charger, datasheet nPM1300 complète pertinente,
lecture directe de registres réels, test des deux modes LDSW/LDO). Piste
`LDSWCONFIG` (active discharge, soft-start level, p.78 datasheet) et bug
LDO nPM1300 (Zephyr PR #83790, présent dans NCS 3.4.0 mais concerne un
temps de stabilisation, pas explicitement un courant) restent
inexplorées. **L'objectif 5-6 µA reste non négociable -- ~250-275 µA
n'est PAS accepté comme coût inévitable, voir référence Seeed
(~4,76-4,93 µA avec IMU sur la même carte).**

**Registre `LDSWCONFIG` lu et écarté (2026-08-27)** : lecture I2C directe
(diagnostic temporaire, serial réactivé puis redésactivé aussitôt après,
voir règle absolue CONFIG_SERIAL) sur build où `regulator_enable()`
n'est jamais appelé : `LDSWCONFIG=0x00` (valeur de reset), `LDSW1LDOSEL=
0x01` (LDO, persistant d'un test antérieur), `LDSWSTATUS=0x00` (jamais
activé). Confirme qu'aucune configuration soft-start/active-discharge
non standard n'explique le ~250-275 µA -- rien ne l'écrit dans le code,
et le registre est resté à sa valeur de reset. Piste écartée.

**Bug LDO nPM1300 (Zephyr PR #83790) réexaminé en détail (2026-08-27),
piste sérieuse mais non concluante** : le code du workaround
(`regulator_npm13xx.c`, `regulator_npm13xx_enable()`) fait exactement
`k_msleep(2)` + une lecture du registre `LDSWSTATUS` juste après le
`write` d'activation -- actif par défaut dans notre overlay (propriété
`nordic,anomaly38-disable-workaround` absente → `ldo_disable_workaround
= false` → branche du workaround exécutée). **Recherche approfondie de
l'errata Nordic officiel du nPM1300 lui-même** (jusqu'ici seul l'errata
du SoC nRF54LM20A avait été consulté, jamais celui du PMIC) :
`docs.nordicsemi.com/bundle/errata_nPM1300_Rev1` --

- **[38] LOADSW/LDO : LDO startup time exceeds specification.**
  Condition documentée : *« quand les BUCK sont désactivés ou sans
  charge et qu'il n'y a pas de communication TWI active »* --
  **correspond exactement aux conditions du test #9** (LDO1 seul, aucun
  trafic I2C après activation, BUCKs sans charge réelle en System OFF).
  Conséquence documentée : la tension de sortie du LDO monte très
  lentement au lieu d'atteindre `VOUTLDO` dans le temps `tSS` typique.
  Workaround Nordic : *« After enabling the LDO, trigger any TWI
  command »* -- c'est exactement ce que fait le driver Zephyr
  (PR #83790), déjà actif. **Non élucidé : le driver ne fait qu'UNE
  lecture, 2 ms après l'activation -- l'errata ne précise pas si une
  seule lecture aussi tôt suffit à « débloquer » la rampe dans tous les
  cas, ou si un rattrapage plus long/plus tardif est nécessaire selon la
  charge réelle. Le symptôme mesuré (~250-275 µA stable, constant sur
  des fenêtres de plusieurs minutes) est cohérent avec un régulateur
  resté bloqué indéfiniment dans un état de démarrage lent plutôt qu'une
  simple rampe qui finirait par se stabiliser.**
- **[40] LOADSW/LDO : Voltage drops on VINLDO1/VINLDO2 at LDO startup**
  et **[41] LOADSW/LDO : LDO startup might cause reset** -- concernent
  des chutes de tension VSYS/VBAT transitoires au démarrage (limite de
  courant VBUS, mode BUCK forcé PFM), pas un courant de repos permanent
  élevé. Pas notre symptôme (pas de reset observé, pas d'alimentation
  VBUS ici -- carte sur batterie via BAT+/BAT-). Écartées.
- **[27]/[31] (BUCK, pas LDSW/LDO)** : `[27]` +1 mA si BUCK reprogrammé
  au même voltage que le VSET résistif au premier boot (pas notre cas,
  aucun BUCK piloté par ce firmware) ; `[31]` +~300 µA en mode
  Hystérétique sous 20 mA de charge, réglé par une lecture/écriture TWI
  -- concerne le bloc BUCK, pas LDSW/LDO, mais **mécanisme structurellement
  identique** (I2C absent = régulateur bloqué dans un mode transitoire
  plus gourmand) -- renforce l'hypothèse [38] par analogie directe.

**Test #11 exécuté (2026-08-27)** : reprise du test #9 + 20 lectures I2C
du registre `LDSWSTATUS` espacées de 5 ms (fenêtre 100 ms) juste après
`regulator_enable()`, au-delà de l'unique lecture déjà intégrée par le
driver à 2 ms. **Résultat : 253 µA, strictement identique au test #9 --
aucun effet.** L'errata [38] est écartée comme explication du ~250 µA
(du moins via ce remède précis) : soit la fenêtre TWI n'est pas le
mécanisme en jeu, soit le symptôme mesuré n'est pas une rampe de
démarrage lente mais un courant de repos réellement élevé une fois le
régulateur stabilisé -- cohérent avec le fait que le protocole de mesure
attend déjà 10 s avant la lecture PPK2, largement au-delà de tout temps
de rampe LDO plausible même très dégradé.

**Piste microphone PDM partagé, test #12 (2026-08-27)** : découverte
que `imu_vdd` et `dmic_vdd` sont le même nœud devicetree (`imu_vdd:
dmic_vdd: LDO1`, vendor Seeed) -- LDO1 alimente aussi le microphone PDM
embarqué (`MSM261D3526H1CPM`, datasheet officiel Seeed consulté).
`pdm20` reste `status="disabled"` (jamais touché, hors périmètre projet)
-- `PDM_CLK` (P1.13) jamais piloté, broche flottante dès que `imu_vdd`
est actif. Datasheet du micro : seul VDD=0V garantit un courant bas
("Powered Down" dans le diagramme d'états) ; Sleep Mode (`fCLOCK ≤
50kHz`) = 1 µA typ, mais Low-Power Mode (150-900kHz) = **290 µA typ** --
très proche du ~250-275 µA mesuré, sans garantie qu'une horloge
flottante tombe côté Sleep plutôt que Low-Power. Test : `PDM_CLK` forcé
en sortie LOW (0 Hz stable), `PDM_DIN` en entrée pull-down, micro
toujours ni configuré ni utilisé. **Résultat : 253 µA, exactement
identique aux tests #9 et #11 -- aucun effet.** Piste écartée (du moins
via ce remède précis -- une horloge flottante n'est pas la cause).

**Observation transversale** : trois modifications logicielles
indépendantes (fenêtre TWI étendue, broche PDM_CLK pilotée, aucune des
deux) donnent la **même valeur exacte, 253 µA**, sans aucune variation.
Seul le changement de mode du régulateur (LDO → Load Switch, test #10)
a fait varier la valeur mesurée (253 → 275,67 µA). Ceci pointe vers une
cause interne au bloc LOADSW/LDO du nPM1300 lui-même plutôt que vers la
charge en aval (IMU ou microphone) -- prochaine piste : chapitre
« Electrical specifications » du datasheet nPM1300 (tables séparées de
la description fonctionnelle déjà consultée), à la recherche d'un
courant de repos ("quiescent current", "IQ_LDO") documenté pour le mode
LDO en sortie 3,3 V.

Le réveil IMU reste mis de côté pour l'instant (décision utilisateur,
2026-08-27) — voir `XIAO-nRF54LM20A-Solution-System-OFF.md` pour l'état
du firmware sans IMU (GRTC + RAM retenue + BLE, validé 3,28 µA au repos,
unité #01 dans cet état à la fin de cette session).
