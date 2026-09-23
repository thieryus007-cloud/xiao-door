# Audit de résilience — XIAO nRF54LM20A, 2026-09-19

**Commandé explicitement par l'utilisateur**, préparé par
`archive/docs-historique/Transition-nRF54LM20A-Audit-Resilience.md` (session précédente). Objectif :
résilience du firmware `xiao_door_sensor` (comportement face aux erreurs,
pannes partielles, corruption d'état, blocages silencieux) — **pas** la
consommation, déjà traitée en profondeur ailleurs
(`archive/docs-historique/Audit-Consommation-2026-09-13.md`, `Nordic-Support-Report-XIAO-
nRF54LM20A.md`).

**Méthode suivie** (identique à l'audit consommation du 2026-09-13, sur
demande du document de transition §3) : relecture du code réellement
déployé (`src/main.c`, `prj.conf`, overlay), vérification des affirmations
contre le code source des drivers Zephyr/NCS et le devicetree du SoC réel
pour ce snapshot (`nrf54lm20a_a_b.dtsi`), pas de confiance a priori sur la
documentation existante. **Aucune carte n'a été flashée pour cet audit** —
travail de lecture de code et de vérification croisée uniquement, aucune
action matérielle. `git log` utilisé pour trancher une question ouverte
factuelle (§3 ci-dessous) plutôt que de la laisser en hypothèse.

---

## 1. TROUVAILLE PRINCIPALE — aucune erreur fatale logicielle ne provoque de redémarrage automatique

**Vérifié au niveau le plus fiable possible sans flash : le fichier généré
`build/xiao_door_sensor/zephyr/include/generated/zephyr/autoconf.h`**
(artefact d'un build réel de ce projet, reflète l'expansion Kconfig
complète — contrairement à `zephyr/.config` du même dossier, tronqué à
149 lignes et donc pas fiable comme preuve). `CONFIG_RESET_ON_FATAL_ERROR`
**n'apparaît nulle part** dans ce fichier (un symbole actif y apparaît
toujours comme `#define CONFIG_XXX 1`) — confirmé absent, alors que
`CONFIG_REBOOT` y est bien présent (`#define CONFIG_REBOOT 1`, ligne 107).

**Mécanisme exact, lu dans le code source** :
- Zephyr par défaut (`zephyr/kernel/fatal.c:37-46`) : sur toute erreur fatale
  (exception CPU/HardFault, stack overflow détecté, kernel oops, kernel
  panic — `zephyr/kernel/fatal.c:60-76` énumère les causes), le gestionnaire
  faible `k_sys_fatal_error_handler()` appelle `arch_system_halt()` qui,
  également faible et jamais surchargé pour ARM dans ce SDK (recherche
  `arch_system_halt` dans `zephyr/arch/arm/` : aucun résultat), **boucle
  indéfiniment avec les interruptions coupées** (`zephyr/kernel/fatal.c:29-
  32`, littéralement `for (;;) { /* Spin endlessly */ }`).
- NCS fournit une bibliothèque toute prête pour changer ce comportement
  (`nrf/lib/fatal_error/fatal_error.c`) : son `k_sys_fatal_error_handler()`
  appelle directement `sys_arch_reboot(0)` (reboot immédiat au niveau
  architecture, sans dépendre du sous-système reboot Zephyr) — mais elle
  n'est compilée que si `CONFIG_RESET_ON_FATAL_ERROR=y`
  (`nrf/lib/fatal_error/Kconfig:7-9`, `select REBOOT`).
- Cette option n'est **impliquée par défaut que pour les applications
  d'exemple officielles sous `nrf/samples/`**
  (`nrf/samples/Kconfig:15` : `imply RESET_ON_FATAL_ERROR if !DEBUG && ...`)
  — `xiao_door_sensor` est un projet hors-arbre, sans `Kconfig` propre
  (vérifié : aucun fichier `Kconfig*` à la racine du projet), donc cette
  implication ne s'applique jamais ici. Rien dans `prj.conf` ne l'active
  explicitement.

**Conséquence concrète** : n'importe quelle exception CPU (déréférencement
de pointeur invalide, division par zéro flottante mal gérée, dépassement de
pile) survenant n'importe où dans `main()`, `sample_motion()`, la pile
Bluetooth ou un driver, **fige le SoC indéfiniment, interruptions coupées,
sans aucune tentative de récupération** — jusqu'à un cycle d'alimentation
manuel. C'est très exactement le scénario « blocage silencieux » au cœur
de la commande de cet audit, et il n'est aujourd'hui couvert par **aucun**
mécanisme, ni logiciel ni matériel (voir §2).

**Correctif proposé, coût minimal** : une seule ligne dans `prj.conf` —
```
CONFIG_RESET_ON_FATAL_ERROR=y
```
`CONFIG_REBOOT` est déjà actif (`select REBOOT` de l'option devient donc un
no-op), aucun autre changement nécessaire. Effet attendu : sur toute
exception fatale, redémarrage immédiat au lieu d'un blocage indéfini — la
boucle reprend au prochain cycle, `retained_load()` restaure l'état côté
BTHome/GRTC comme sur n'importe quel reset inattendu (mécanisme déjà en
place, voir `main.c:1075-1097`).

**Attente précise et falsifiable pour la vérification physique
(obligatoire avant confiance, voir §4 du document de transition et §14
ci-dessous)** : après ce changement, un test de fault injection contrôlé
(ex. `k_oops()` ou déréférencement volontaire dans une build de test
séparée, jamais sur #01/#02) doit provoquer un redémarrage visible (trame
BTHome avec `reset_cause` cohérent, `bthome_pid` reparti bas si CRC
invalidée, ou continu si CRC toujours valide) en quelques secondes. Si le
SoC reste figé malgré le changement, l'hypothèse ci-dessus (mécanisme NCS
mal compris) est fausse et à ré-investiguer entièrement — ne pas supposer
que la seule présence de la ligne `prj.conf` suffit sans ce test.

---

## 2. TROUVAILLE PRINCIPALE — watchdog matériel disponible au niveau SoC/board, jamais activé

Confirme et précise la piste déjà repérée dans le document de transition
(§6), avec un chemin d'activation concret vérifié dans ce snapshot NCS
3.4.0 :

- Le SoC nRF54LM20A expose **deux instances matérielles de watchdog**,
  déclarées dans le devicetree du SoC lui-même
  (`zephyr/dts/vendor/nordic/nrf54lm20a_a_b.dtsi:704-828`) : `wdt30`
  (« hardware fixed to Secure », inaccessible depuis l'application non
  sécurisée) et `wdt31` (accessible depuis `cpuapp`), **toutes deux
  `status = "disabled"` par défaut** dans le SoC — un motif standard sur
  cette famille récente (nRF54L*), qui laisse chaque board/application
  choisir explicitement quels périphériques activer.
- Le fichier de board Seeed **expose déjà un alias standard**
  `watchdog0 = &wdt31;`
  (`vendor/platform-seeedboards/.../xiao_nrf54lm20a_nrf54lm20a-
  common.dtsi:106`) — la carte est prête à l'emploi pour un watchdog, mais
  aucun overlay de ce projet (`xiao_door_sensor`, ni aucun des projets de
  test archivés) ne passe `wdt31` à `status = "okay"`.
- Le driver Zephyr correspondant (`CONFIG_WDT_NRFX`,
  `zephyr/drivers/watchdog/Kconfig.nrfx:6-12`) se sélectionne automatiquement
  (`default y depends on DT_HAS_NORDIC_NRF_WDT_ENABLED`) dès qu'un nœud
  `wdt31`/`wdt30` passe à `okay` **et** que `CONFIG_WATCHDOG=y` est
  positionné — confirmé absent des deux (devicetree et `prj.conf`) pour ce
  projet, et absent de `autoconf.h` (voir §1).

**Ce que le watchdog couvrirait, distinct du correctif §1** : un blocage
qui ne déclenche **aucune exception CPU** (donc jamais intercepté par
`RESET_ON_FATAL_ERROR`) — typiquement un deadlock logiciel ou un bus I2C
réellement bloqué électriquement (ex. SDA maintenu bas par l'IMU ou le
PMIC suite à une panne physique). L'IMU est câblé sur `i2c22`, un
périphérique TWIM matériel réel (pas du bit-banging logiciel — vérifié,
`nrf54lm20a_cpuapp_common.dtsi:194-201`), mais rien dans ce projet ne
garantit qu'un blocage électrique du bus (pas juste un NACK, une ligne
réellement figée) soit borné dans le temps par le driver — point non
vérifié plus avant (nécessiterait de lire `nrfx_twim` en détail ou un test
physique), à traiter comme un risque non quantifié plutôt qu'un fait
établi. **Les deux correctifs (§1 et §2) sont complémentaires, pas
redondants** : `RESET_ON_FATAL_ERROR` couvre les crashs détectés par le
CPU, le watchdog couvre les blocages qui ne le sont pas.

**Correctif proposé** (trois changements minimaux, à vérifier
physiquement avant confiance) :
1. Overlay : `&wdt31 { status = "okay"; };`
2. `prj.conf` : `CONFIG_WATCHDOG=y`
3. `main.c` : `wdt_setup()` + `wdt_install_timeout()` au démarrage (une
   fois, comme `bt_enable()`), puis un `wdt_feed()` par itération de la
   boucle principale, placé **après** l'envoi effectif des trames et
   `retained_save()` (fin de boucle, juste avant le `k_sleep()` final) —
   pour ne nourrir le chien de garde qu'après un cycle complet et
   réellement fonctionnel, pas juste après le réveil du timer.

**Point d'attention pour le dimensionnement du délai** (à vérifier avant
flash, pas une supposition à valider après coup) : le pire cas légitime de
durée d'une itération n'est pas les ~40-50 ms habituels de
`sample_motion()`, mais la trame de repos répétée
(`send_final_state_frame()`, `main.c:801-810`) : `FINAL_STATE_REPEATS=3` ×
(`ADV_BURST_MS=700` + `FINAL_STATE_REPEAT_GAP_MS=3000`) ≈ 11,1 s. Le délai
du watchdog doit donc être fixé nettement au-dessus de cette valeur (ex.
20-30 s) pour ne jamais déclencher un reset sur un cycle légitime plus
long — un réglage trop serré serait pire que l'absence de watchdog
(redémarrages intempestifs en fonctionnement normal). **Attente précise et
falsifiable** : avec un délai de 20-30 s et un `wdt_feed()` en fin de
boucle, un fonctionnement normal prolongé (heures) ne doit produire aucun
reset inattendu ; un test de blocage volontaire (boucle infinie injectée
dans une build de test séparée) doit produire un reset dans la fenêtre du
délai configuré. Si des resets intempestifs apparaissent en fonctionnement
normal, le délai est mal calibré ou le point de `wdt_feed()` mal placé —
ne pas conclure à un défaut du mécanisme avant d'avoir vérifié ces deux
hypothèses.

---

## 3. Résolution de la question ouverte §5 du document de transition — `suspend_external_flash()` jamais appelée

**Tranché par `git log`, pas par supposition.** La question posée était :
imprécision de l'audit du 2026-09-13, changement réel de `main.c` depuis,
ou code mort ? Vérification faite pour cet audit : lecture du contenu de
`main.c` à chaque commit touchant ce fichier depuis le début de
l'architecture System ON IDLE.

```
46192e7 (29-08, premier commit du pivot) : suspend_external_flash() définie,
         jamais appelée -- main() appelle uniquement
         configure_spi_pins_for_system_off() (deux fois : une fois en
         interne à suspend_external_flash() elle-même, une fois
         directement depuis main()).
c63908d, ccbe1b5, 1994112, b280658 (HEAD actuel) : identique, à chaque commit.
```

**Réponse : (c) — code mort, jamais un vrai changement.**
`suspend_external_flash()` (qui appellerait `pm_device_action_run(...,
PM_DEVICE_ACTION_SUSPEND)` sur le driver flash **et** le bus SPI — mise en
veille profonde JEDEC via l'API PM Zephyr) n'a **jamais** été invoquée
depuis `main()`, dans aucun commit de cette architecture. Seule
`configure_spi_pins_for_system_off()` (force les broches GPIO brutes,
fonction interne appelée par `suspend_external_flash()` en dernière étape)
est réellement exécutée. L'affirmation de `Audit-Consommation-2026-09-
13.md` §6bis (« la production... le fait déjà depuis toujours ») est une
**confusion entre les deux fonctions** — pas une régression, pas une
observation inexacte sur un état passé différent.

**Conséquence pratique, sans lien avec la consommation** (déjà confirmée
au plancher datasheet indépendamment de ce point) : la puce flash externe
`py25q64` n'est jamais placée dans son propre état bas-niveau « suspendu »
via son pilote (qui écrirait la commande JEDEC Deep Power-Down réelle) —
seules ses broches sont forcées à un niveau électrique déterministe de
l'extérieur. Sans conséquence mesurée à ce jour, mais un vrai nettoyage de
code à faire : soit remplacer l'appel direct à
`configure_spi_pins_for_system_off()` dans `main()` par un appel à
`suspend_external_flash()` (qui l'appelle déjà en interne, en plus des
deux `pm_device_action_run()`), soit supprimer la confusion en clarifiant
que seule l'approche GPIO brute est utilisée intentionnellement. Priorité
basse (cosmétique/clarté, pas un bug fonctionnel), mais à corriger pour
que le code corresponde à ce que la documentation affirme.

---

## 4. Panne fonctionnelle persistante de `sample_motion()` — silence BLE total, y compris la trame santé

Confirme et **aggrave** la piste déjà repérée dans le document de
transition. Lecture exacte de la boucle principale (`main.c:1141-1215`) :

```c
rc = sample_motion(...);
if (rc < 0) {
        printf("Warning: sample_motion failed (%d)\n", rc);
        k_sleep(K_MSEC(MOTION_POLL_INTERVAL_MS));
        continue;                       /* <-- saute TOUT le reste */
}
...
if (health_due) {
        send_frame_b(...);              /* <-- jamais atteint si rc < 0 */
        ...
}
```

Le `continue` saute **inconditionnellement** l'envoi de la trame B
(batterie/santé), pas seulement les trames A/C événementielles comme le
document de transition le formulait. `sample_motion()` peut échouer de
façon persistante sur plusieurs causes réelles et distinctes (toutes déjà
visibles dans le code, `main.c:907-1055`) : `regulator_enable(imu_vdd_dev)`
en échec, écriture I2C `CTRL3_C`/`CTRL6_C` en échec, `device_init(imu_dev)`
en échec, `sensor_attr_set()` (ODR) en échec, ou `sensor_sample_fetch_chan()`
en échec — typiquement un bus I2C bloqué électriquement ou un
régulateur nPM1300 qui ne répond plus.

**Conséquence** : si l'une de ces causes devient permanente sur une unité
déployée, celle-ci **cesse totalement d'émettre en BLE** (plus aucune
trame A, B ou C) tout en continuant à consommer de l'énergie en boucle
(tentative chaque seconde, indéfiniment), **sans aucun mécanisme
d'échappement** — ni compteur d'échecs consécutifs, ni redémarrage de
sécurité après N tentatives, ni tentative de ré-initialisation du bus I2C.
Depuis Home Assistant, cette unité devient indiscernable d'une unité
éteinte ou hors de portée — exactement le symptôme au cœur de la commande
de cet audit (« impossible de distinguer unité HS de unité qui échoue en
boucle sans accès SWD physique »).

**Correctif proposé** : compteur d'échecs consécutifs de `sample_motion()`
(variable locale à `main()`, pas la peine de la mettre en RAM retenue —
elle repart de zéro après tout redémarrage, ce qui est le comportement
voulu) ; au-delà d'un seuil (ex. 30-60 échecs consécutifs, soit 30-60 s à
raison d'un cycle par seconde), déclencher `sys_reboot(SYS_REBOOT_COLD)` —
même logique défensive que celle déjà en place pour l'échec de
`bt_enable()` au démarrage (`main.c:1119-1123`). Un redémarrage
réinitialise l'I2C/le régulateur au passage (ré-exécution complète de
`device_init()` sur les deux drivers), ce qui a une chance réelle de
sortir d'un état bloqué transitoire (ex. bus I2C dans un état incohérent
après un glitch d'alimentation), sans coût mesurable en fonctionnement
normal (le compteur est remis à zéro à chaque succès).

**Attente précise et falsifiable** : ce correctif ne change rien au
comportement en fonctionnement normal (le compteur reste à 0 en pratique)
— une mesure PPK2 après implémentation doit donc rester à ~20-22 µA,
inchangée. Si elle diverge, le correctif a introduit un effet de bord
inattendu (ex. compteur mal placé, provoquant des redémarrages
intempestifs) à investiguer avant d'accepter le changement.

---

## 5. `retained_state` — absence de champ de version explicite

Confirme la piste du document de transition, avec le mécanisme de risque
précisé. Structure actuelle (`main.c:449-460`) :

```c
struct retained_state {
        uint8_t  bthome_pid;
        int16_t  last_sent_pitch_dd;
        int16_t  last_sent_roll_dd;
        int16_t  yaw_dd;
        uint64_t next_health_us;
        uint64_t next_heartbeat_us;
        uint32_t crc;
};
```

Le CRC (`crc32_ieee` sur tous les octets avant le champ `crc` lui-même,
`RETAINED_CRC_OFFSET = offsetof(struct retained_state, crc)`) protège
contre une corruption aléatoire des octets, mais **ne protège pas contre
un changement de disposition des champs qui laisserait `sizeof()` et
l'offset du `crc` inchangés** — par exemple, réordonner
`next_health_us`/`next_heartbeat_us` dans une future version : les octets
bruts en RAM retenue restent identiques après un reflash (la RAM retenue
est de la SRAM ordinaire qui survit à un reflash tant que l'alimentation
n'est pas coupée, déjà établi dans le code et la documentation), le CRC
validerait toujours la même valeur numérique sur les mêmes octets, et le
nouveau firmware réinterpréterait silencieusement les deux échéances
inversées — sans aucune erreur, aucun log, aucun symptôme visible avant
qu'une trame arrive en retard ou en avance de façon inexpliquée. Ce n'est
pas un scénario du quotidien (le déploiement actuel se fait par clonage
d'image complète, qui écrase aussi la RAM retenue au premier boot d'une
unité neuve), mais devient pertinent dès qu'une mise à jour de firmware
sera un jour appliquée par reflash **sur une unité déjà en service**
plutôt que par clonage d'image vierge.

**Correctif proposé** : ajouter un champ `uint16_t layout_version` (ou
similaire) en tout début de structure, incrémenté manuellement à chaque
changement de disposition des champs, vérifié explicitement dans
`retained_load()` en plus du CRC (`crc == retained.crc && retained.layout_
version == RETAINED_LAYOUT_VERSION`). Coût nul en consommation (2 octets,
vérifiée une fois au boot).

---

## 6. `retained_save()` — écriture non vérifiée, fenêtre de « torn write » résiduelle

```c
static void retained_save(void)
{
        retained.crc = crc32_ieee((const uint8_t *)&retained, RETAINED_CRC_OFFSET);
        retained_mem_write(retained_dev, 0, (uint8_t *)&retained, sizeof(retained));
}
```

La valeur de retour de `retained_mem_write()` est ignorée — cohérent avec
le motif général du fichier (voir §8), mais ici plus significatif : si une
coupure d'alimentation survient pendant cette écriture (fenêtre très
brève, une simple copie mémoire vers de la SRAM), la RAM retenue peut se
retrouver avec un mélange d'anciens et de nouveaux octets. Le CRC,
calculé sur le buffer **avant** l'écriture et inclus dans les octets
écrits, ne validera presque certainement pas un tel mélange partiel — donc
`retained_load()` au redémarrage suivant retombera sur le `memset(&retained,
0, ...)` de secours (`main.c:1075-1077`), qui est un état sûr mais qui
**perd tout l'état accumulé** (compteur `bthome_pid`, intégration `yaw_dd`,
échéances GRTC) même si la majorité des octets réellement écrits en RAM
retenue avant la coupure étaient corrects. Risque à faible probabilité
(fenêtre d'écriture très courte, coupure secteur peu fréquente sur une
alimentation USB fixe) mais jamais quantifié ni testé.

**Correctif proposé, optionnel et à faible coût** : relire immédiatement
après écriture (`retained_mem_read()`) et comparer au buffer source avant
de considérer la sauvegarde réussie — ou, plus robuste mais plus complexe,
un schéma à deux emplacements (A/B) avec un indicateur de validité séparé.
Étant donné la faible probabilité et l'absence de symptôme observé à ce
jour sur 13+ unités déployées, ce point est classé priorité basse — à
traiter après les corrections §1/§2/§4 qui couvrent des scénarios plus
probables et plus impactants.

---

## 7. `read_battery()` — échec silencieux, risque de fausse alerte « batterie faible »

```c
static void send_frame_b(int16_t temp_cc)
{
        uint8_t battery_pct = 0;
        uint16_t battery_mv = 0;

        if (read_battery(&battery_pct, &battery_mv) < 0) {
                printf("battery read failed, sending frame B without a fresh value\n");
        }
        ...
        frame_b[B_OFF_BATTERY] = battery_pct;
        ...
        frame_b[B_OFF_BATT_LOW] = battery_pct < 15 ? 1 : 0;
```

Si `read_battery()` échoue (glitch I2C transitoire sur le driver chargeur
nPM1300, ou `device_init()` différé qui échoue ponctuellement), la trame B
est **quand même envoyée**, avec `battery_pct=0`/`battery_mv=0` — ce qui
positionne `OBJ_BATTERY_LOW=1` et affiche 0 % dans Home Assistant, **comme
si la batterie était réellement déchargée**, sur un simple échec de
lecture transitoire. HA ne peut aujourd'hui pas distinguer « batterie
vraiment à 0 % » de « lecture ratée cette fois-ci ». Sévérité modérée :
n'affecte qu'une trame toutes les 15 min (`FRAME_B_INTERVAL_MS`), et une
fausse alerte isolée serait vraisemblablement corrigée à la trame
suivante si l'échec n'est pas persistant — mais reste une fausse
information active envoyée à HA pendant au moins un cycle complet.

**Correctif proposé** : ne pas envoyer la trame B du tout en cas d'échec
de lecture batterie (report à l'échéance suivante, `retained.next_health_us`
inchangée), plutôt que d'envoyer une valeur `0` trompeuse — cohérent avec
le traitement déjà appliqué à `sample_motion()` côté trame A/C (échec →
pas de trame plutôt qu'une trame avec des valeurs fausses). Alternative
plus simple si l'omission complète est jugée trop risquée pour le suivi de
santé général : conserver et renvoyer la dernière valeur batterie connue
(déjà en RAM ordinaire de session, à ajouter) plutôt que 0 en cas d'échec.

---

## 8. Aucune trace persistée des redémarrages inattendus

`hwinfo_get_reset_cause()` est lu et affiché une fois au boot
(`main.c:1064-1070`), mais uniquement via `printf()` — invisible en
production (`CONFIG_SERIAL=n` en dur, règle absolue du projet). Rien ne
persiste cette information au-delà d'un simple affichage perdu. Un cycle
répété de brownout/reset sur une unité déployée ne laisserait donc
**aucune trace exploitable à distance**, contrairement à ce qu'un accès
SWD physique permettrait de voir en lisant `RESETREAS` directement.

**Correctif proposé, à faible coût** : ajouter un compteur de redémarrages
non volontaires (`reset_cause` différent de power-on normal) dans
`retained_state` (incrémenté à chaque boot où `reset_cause` indique un
watchdog/brownout/reset logiciel plutôt qu'une mise sous tension propre),
et l'exposer dans la trame B existante — soit en réutilisant un bit
actuellement toujours à une valeur fixe (aucun candidat évident sans
casser le format BTHome existant, budget de trame déjà serré, voir
`main.c:696-699`), soit en acceptant de dépasser légèrement la trame B
actuelle si un octet est disponible. **Ce point nécessite une décision
produit (quel objet BTHome ajouter, ou accepter de ne garder cette
information que côté SWD/diagnostic ponctuel)** plutôt qu'un choix
technique unilatéral — à trancher avec vous avant implémentation.

---

## 9. Dérive du yaw non recalée — limitation connue, confirmée, non quantifiée

Déjà documenté explicitement dans le code lui-même
(`main.c:453-456`, commentaire "pas de recalage anti-derive") et repris
dans la documentation de référence — pas une découverte de cet audit, mais
confirmé toujours vrai à la lecture du code actuel : `yaw_dd` est
uniquement intégré depuis le gyroscope (`main.c:593-611`), jamais recalé
sur une référence externe. C'est une limitation **fondamentale** de toute
intégration gyroscopique sans magnétomètre (pas un bug corrigible en
l'état) — la dérive est inévitable sur le très long terme, jamais
quantifiée dans ce projet.

**Piste d'amélioration optionnelle, pas une correction de bug** : la
boucle suit déjà un état de repos prolongé (`was_moving`/`rest_since`,
`main.c:1183-1201`) pour la trame de repos — la même détection pourrait
déclencher un recalage du yaw à une valeur de référence (ex. 0) après une
période de repos suffisamment longue (à définir), sur l'hypothèse qu'un
capteur immobile depuis longtemps n'a pas de raison de conserver une
orientation dérivée. Ceci changerait un comportement fonctionnel observable
(le yaw affiché en HA sauterait ponctuellement) — à ne considérer que si
la dérive s'avère réellement gênante en usage réel, pas une priorité de
cet audit.

---

## 10. Bouton physique toujours à 0 — confirmé persister, cause non identifiable par revue de code seule

Bug connu, documenté, repris tel quel de la production précédente
(`main.c:181-188`, `639-656`). Vérification faite pour cet audit :
`init_button()` appelle `gpio_pin_configure_dt(&button, GPIO_INPUT)`, qui
applique bien les indicateurs du devicetree (`GPIO_PULL_UP | GPIO_ACTIVE_LOW`,
`vendor/platform-seeedboards/.../xiao_nrf54lm20a_nrf54lm20a-common.dtsi:47`)
en plus de `GPIO_INPUT` — la configuration logicielle du GPIO est correcte
au niveau du code, aucune anomalie de câblage trouvée dans les fichiers de
carte (broche `gpio0.9` non partagée avec un autre périphérique dans les
fichiers de board lus pour cet audit). **La cause ne peut donc pas être
établie par une revue de code supplémentaire** — elle nécessiterait une
vérification matérielle réelle (multimètre/oscilloscope sur `gpio0.9`
pendant un appui physique, ou comparaison avec une carte de rechange).
Hors périmètre d'un audit de code ; à traiter séparément si le sujet est
rouvert, avec un accès physique à une unité de test (jamais #01/#02 en
production).

---

## 11. Ce qui est déjà solide — à ne pas retoucher

Vérifié pour cet audit, comportement jugé correct et adapté :

- **Répétition de la trame de repos** (`send_final_state_frame()`,
  `FINAL_STATE_REPEATS=3`) — bonne pratique délibérée pour un protocole
  sans accusé de réception (advertising non connecté), déjà motivée dans
  le code, rien à changer.
- **Bornage des échéances GRTC au boot** (`main.c:1088-1097`) — protège
  correctement contre une échéance absolue obsolète chargée d'une session
  précédente après un reflash (RAM retenue = SRAM, GRTC repart de zéro) ;
  logique vérifiée correcte à la lecture.
- **Confirmation sur deux cycles consécutifs** (mouvement et angle,
  `main.c:1003-1030`) — mitigation raisonnable et déjà mesurée contre le
  bruit de capteur transitoire, sans lien direct avec la résilience mais
  ne pose aucun problème de robustesse identifié.
- **Init différée IMU/chargeur** (`zephyr,deferred-init`, overlay) —
  nécessaire et correctement gérée avec les vérifications `device_is_
  ready()`/`device_init()` appropriées dans le code appelant.
- **Reboot sur échec `bt_enable()`** (`main.c:1119-1123`) — seul mécanisme
  d'auto-guérison actif à ce jour dans tout le firmware ; bonne pratique
  déjà en place, à généraliser (voir §4) plutôt qu'à modifier.

---

## 12. Trouvailles mineures, priorité basse

- **`advertise_burst()`** (`main.c:731-745`) : échec de `bt_le_adv_start()`/
  `bt_le_adv_stop()` uniquement affiché (`printf`), jamais compté ni
  retenté. Sévérité faible : protocole broadcaster best-effort déjà
  compensé pour la trame critique (§11), et un échec ponctuel ne bloque
  jamais le cycle suivant (contrairement à la panne `sample_motion()` du
  §4). Pas d'action recommandée dans l'immédiat.
- **Valeurs de retour ignorées de façon généralisée** : `hwinfo_clear_
  reset_cause()` (`main.c:1070`), `retained_mem_write()` (§6),
  `sensor_attr_set()` dans `set_gyro_power()` (`main.c:557`) — cohérent
  avec le motif « erreur = juste un `printf` » déjà signalé au niveau
  architectural (§4), pas des cas individuels à corriger un par un tant
  que le mécanisme d'escalade du §4 n'est pas en place au niveau
  approprié (la boucle principale, pas chaque fonction individuellement).

---

## 13. Plan d'action priorisé

**⚠️ STATUT (2026-09-19, mis à jour) : BLOQUÉ — voir
`archive/docs-historique/Suivi-Nordic-Reproductibilite-Build-2026-09-19.md` pour le suivi
complet.** Les 6 premiers items ont été implémentés et flashés sur #02
le jour même de cet audit ; le rebuild a mesuré ~35-38 µA au lieu des
~20-22 µA attendus. Investigation approfondie : le code de résilience
lui-même est innocenté (un rebuild du commit d'origine SANS aucun de
ces changements reproduit la même anomalie) — la cause est un problème
de reconstruction `west build` non reproductible, indépendant de ce
plan, déjà pressenti au §14 ci-dessous. **#02 a été restaurée sur
l'image d'or vérifiée (~23 µA) ; ce plan reste prêt à ré-appliquer dès
que la reconstruction sera fiable** (ticket ouvert auprès du support
Nordic).

| # | Correctif | Sévérité couverte | Effort | Statut (2026-09-19) |
|---|---|---|---|---|
| 1 | `CONFIG_RESET_ON_FATAL_ERROR=y` (§1) | **Critique** — blocage total indéfini sur toute exception CPU | Une ligne `prj.conf` | Codé et testé, **reverté** — bloqué par le bug de reconstruction, pas par ce correctif |
| 2 | Watchdog matériel `wdt31` (§2) | **Critique** — filet de sécurité pour tout blocage non couvert par #1 (deadlock, bus I2C figé) | Overlay + `prj.conf` + ~10 lignes `main.c` | Codé et testé, **innocenté individuellement** (isolation dédiée, consommation inchangée sans lui) puis reverté avec le reste |
| 3 | Compteur d'échecs `sample_motion()` + reboot de sécurité (§4) | **Élevée** — silence BLE total et permanent sur panne IMU/I2C persistante | ~10 lignes `main.c` | Codé et testé, **reverté** avec le reste |
| 4 | Nettoyage `suspend_external_flash()`/`configure_spi_pins_for_system_off()` (§3) | Basse — clarté du code, pas un bug fonctionnel | Une ligne `main.c` | Codé et testé, **innocenté individuellement** (isolation dédiée) puis reverté avec le reste |
| 5 | Champ de version explicite dans `retained_state` (§5) | Moyenne — risque latent, pas encore matérialisé | ~5 lignes `main.c` | Codé et testé, **reverté** avec le reste |
| 6 | `send_frame_b()` : ne pas publier de fausse valeur batterie sur échec (§7) | Moyenne — fausse alerte possible vers HA | ~5 lignes `main.c` | Codé et testé, **reverté** avec le reste |
| 7 | Compteur de redémarrages inattendus exposé côté BTHome (§8) | Moyenne — décision produit requise avant implémentation | À définir avec vous | Volontairement non implémenté côté radio (persistance interne seule codée avec #5) — décision produit toujours en attente |
| 8 | Vérification après écriture `retained_save()` (§6) | Basse — probabilité très faible | ~5 lignes `main.c` | **Jamais implémenté** — priorité basse, non atteint avant le blocage |

**À la reprise** (dès que le rebuild sera fiable, voir critère de
déblocage précis dans le document de suivi) : les items 1, 3, 5, 6 sont
du code neuf jamais mis en cause fonctionnellement, prêts à ré-appliquer
tels quels. Les items 2 et 4 ont déjà été testés isolément et innocentés
de l'anomalie de consommation — aucune raison de les retester avant de
les remettre en place. L'item 8 reste à écrire. L'item 7 reste bloqué
sur une décision produit, indépendante du bug de reconstruction.

---

## 14. Rappel — contrainte de vérification physique avant toute confiance

Comme pour l'audit consommation (`archive/docs-historique/Audit-Consommation-2026-09-13.md` §4),
**aucune correction de code ci-dessus ne doit être considérée fiable sur
la seule base de cette relecture** : la reconstruction du firmware depuis
les sources reste non fiable (64159/117396 octets de différence sur
rebuild propre, cause toujours inconnue à ce jour). Toute implémentation
de ces correctifs devra donc :
1. Être compilée et **physiquement flashée sur une unité de test**, jamais
   directement sur #01/#02 en production.
2. **Vérifier le numéro de série SWD avant tout flash** (règle absolue du
   projet, `C:\ncs\CLAUDE.md`).
3. Être validée par mesure PPK2 (pour tout changement pouvant affecter la
   consommation, en particulier le watchdog #2) **et** par test fonctionnel
   réel (fault injection contrôlée pour #1/#2/#3 — pas seulement une
   relecture du code déployé).
4. Ne remplacer l'image d'or de référence qu'après cette vérification
   complète, jamais avant.
