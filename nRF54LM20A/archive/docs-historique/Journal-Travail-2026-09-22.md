# Journal d'exécution — Plan-Reduction-Consommation-2026-09-22.md

Document annexe, séparé du plan (`Plan-Reduction-Consommation-2026-09-22.md`
reste intact, jamais modifié). Trace factuelle et datée de tout ce qui a
été fait en exécutant ce plan, session par session. Mis à jour au fil de
l'exécution.

---

## 2026-09-22 — Session d'exécution (Sonnet 5)

### Phase 0 — Réalignement du source

1. Sauvegarde du travail en cours non commité (plan de résilience
   2026-09-19) dans `archive/xiao_door_sensor-logs-et-backups/reference/
   {main,prj,overlay}_resilience-wip_2026-09-19.*.bak`.
2. Restauration de `main.c`, `prj.conf`, l'overlay depuis le commit
   `c63908d` (`prj.conf`/overlay identiques à HEAD, aucun diff).
3. H_LACTIVE appliqué directement dans `main.c` (`LSM6DSL_CTRL3_C_H_LACTIVE
   = BIT(5)`), en-tête du fichier réécrit (retrait de l'avertissement
   « NE PAS RECOMPILER », explication de la reproductibilité résolue).
4. Rebuild `--pristine` : `zephyr.bin` SHA-256 =
   `0da087ae45d087afdc334828e95a8c44283b1182b1cce5dc1525d7133853f778`,
   **identique** à `golden-image/unit02-verified-2026-09-01-H_LACTIVE.bin`
   — reproductibilité du build confirmée indépendamment.
5. Commit `630b32a` (main.c réaligné).
6. Documentation corrigée (commit `9e38290` puis mises à jour
   ultérieures) : `Configuration...md` §9/§11, `Audit-Consommation-
   2026-09-13.md` §4 et point 5 du §7, `Procedure-Clonage...md` (patch
   binaire marqué obsolète), `Suivi-Nordic-Reproductibilite-Build-2026-
   09-19.md` (statut RÉSOLU), `xiao_nrf54lm20a_project_notes.md` (item 8
   corrigé, item 9 complété).
7. **Flash de référence sur #02** (S/N `9C4A557D`, vérifié en lecture
   seule avant flash) : `verify_image` → **`verified 117396 bytes`**, OK.

### Mesure de référence (Phase 0 étape 7)

- Capture PPK2 `ppk-20260922T192721.csv` (60 s, 100 kHz, tension source
  identique aux ~100 mesures précédentes du projet — confirmé par
  l'utilisateur).
- `tools/ppk_cycle_stats.py` : charge/cycle moyenne **20,97 µC**
  (médiane 20,79), plancher **3,84 µA**, période 1016,7 ms,
  **régime établi 24,47 µA**.
- **Attendu (plan §5.7) : 18,1-18,6 µC/cycle, 21,3-21,9 µA, plancher
  3,4-3,7 µA — mesure hors bornes** (+~12 % sur le régime établi).
- Vérifications faites : tension source identique à la référence
  (confirmé par l'utilisateur) ; firmware réellement flashé confirmé
  identique par SHA-256 + `verify_image`. Cause de l'écart non
  formellement identifiée à ce stade — investigation suspendue sur
  instruction explicite de l'utilisateur (dérouler le plan tel quel),
  reprise possible plus tard si besoin.

### Étape A — Bus I2C PMIC à 400 kHz

1. Overlay modifié (`&pmic_i2c { clock-frequency = <400000>; };`,
   texte identique au plan §6, sans commentaire ajouté).
2. Rebuild `--pristine` : `zephyr.dts` confirme
   `clock-frequency = < 0x61a80 >` sur le nœud `pmic-i2c`, source =
   ligne 41 de l'overlay. SHA-256 `zephyr.bin` =
   `44033dcddaee92b7952a7dbd0d66ef1f40e8c1ae9808f4eecad460f6289bbd51`.
3. Flash sur #02 — bloqué plusieurs fois par le classificateur auto-mode
   de Claude Code (action d'écriture matérielle refusée en mode auto,
   raisons variables selon la tentative : Security Weaken, Blind Apply,
   Irreversible Deletion, Self-Modification, ou raison générique) ;
   débloqué en sortant du mode auto. **Flashé et vérifié** :
   `verified 117396 bytes`, S/N `9C4A557D` confirmé, connexion propre
   (cpu + aux examinés avec succès).
4. **Mesure PPK2** (`ppk-20260922T194348.csv`, 60 s) : charge/cycle
   **15,09 µC** (médiane 15,03), plancher 3,84 µA (inchangé, attendu),
   **régime établi 18,70 µA**. **Gain vs référence : 5,88 µC/cycle**
   (20,97 → 15,09), dans la fourchette de falsification du §6
   (2-6 µC = hypothèse confirmée). Résultat jugé significatif par
   l'utilisateur — étape validée.

### Étape F — LDO1 3,3 → 3,0 V (tentée puis revertée)

1. Overlay modifié (commit `b18eaaa`), flashé et vérifié sur #02.
2. **Mesure PPK2** (`ppk-20260923T065912.csv`, 60 s) : **régression
   sévère** — 0/41 salves de sondage normales détectées, chaque
   événement dure ~509 ms (au lieu de ~13-20 ms), ~140 µC, moyenne
   globale 97 µA. Profil : après ~3 ms d'activité I2C normale, plateau
   plat ~270 µA (rail `imu_vdd` resté allumé, pas de l'activité I2C)
   pendant le reste — signature identique au timeout I2C ~500 ms déjà
   observé à la caractérisation LDO (variante V4). Hypothèse : à 3,0 V,
   une transaction I2C vers l'IMU n'est plus acquittée (pull-ups
   SDA/SCL sur le rail `imu_vdd` lui-même). Risque exact anticipé par
   le plan (« garder seulement si aucune erreur I2C ») — confirmé dès
   la première mesure, pas besoin du test 1h.
3. **Revert** (commit `c477dc4`) : overlay restauré à l'état de l'étape
   D (SHA-256 `zephyr.bin` identique, `b54850ec...`). Reflashé et
   vérifié sur #02.

**État final retenu : A+B+D, régime établi 16,98 µA (−30,6 % vs
référence).** Prochaine étape : §12, validation finale.

### Validation finale (§12) — en cours

- Mesure PPK2 240 s (`ppk-20260923T071227.csv`) : régime établi
  **16,84 µA**, plancher 3,76 µA — cohérent avec l'étape D (16,98 µA)
  sur une fenêtre 4× plus longue, confirme la stabilité. Tests
  fonctionnels HA confirmés par l'utilisateur (« HA fonctionne »).
  Point non tranché : capture < 5 min exigées par le plan, et aucun
  événement identifiable comme une trame santé (BLE + lecture
  batterie/température, transactions PMIC supplémentaires) dans cette
  fenêtre de 240 s — à confirmer avec une capture plus longue (jusqu'à
  15 min, intervalle réel de la trame santé) ou une confirmation
  indépendante (horodatage de mise à jour batterie/température dans
  HA).

### Second test de reproductibilité du build (2026-09-23)

Demandé explicitement par l'utilisateur pour valider que le code
**actuel** (après étapes A, B, D ; C et F revertées) recompile de façon
déterministe — pas seulement l'état de la Phase 0 (déjà prouvé le
2026-09-22, voir plus haut et `Suivi-Nordic-Reproductibilite-Build-
2026-09-19.md` §8).

**Procédure exacte :**
1. Vérification que HEAD (commit `c477dc4`) ne porte aucune
   modification non commitée sur `main.c`/`prj.conf`/l'overlay
   (`git status --short`) — confirmé propre.
2. Rebuild `--pristine` complet depuis les sources (commande §5 de
   `Configuration...md`, PowerShell — pas Bash, voir plus bas).
3. `sha256sum build/xiao_door_sensor/zephyr/zephyr.bin` comparé à la
   valeur déjà enregistrée pour l'image de l'étape D, elle-même
   flashée et validée par deux mesures PPK2 indépendantes (16,98 µA
   puis 16,84 µA).
4. **Résultat : SHA-256 identique**
   (`b54850ec5e29dfdb8530df934acbba85d5374565bcc06567b71778c3c21d78fa`)
   — reproductibilité confirmée sur le code réellement évolué de cette
   session, pas seulement sur la base de départ. Deuxième preuve
   indépendante du même fait (`west build` déterministe sur ce
   toolchain, NCS 3.4.0, `dcbdc366a1`).
5. Ce binaire fraîchement recompilé reflashé sur #02 (S/N `9C4A557D`
   confirmé) : `verified 117128 bytes`, réussi du premier coup.

**Précision sur `k_msleep(6)`/`k_msleep(40)`** : ces deux valeurs,
citées dans l'incident Nordic original (Phase 0, voir §1 du plan), ne
figurent plus nulle part dans le code actuel — l'étape D les a
remplacées par `k_usleep(ACCEL_FIRST_SAMPLE_US=3100)`, une valeur
issue de la caractérisation `xiao_accel_odr_char` (échantillon n°2 à
833 Hz), sans lien avec l'ancien débat 6 ms/40 ms. Le dossier Nordic
reste clos à raison.

### Incident session — échec de détection USB/SWD en début de session

Question posée explicitement par l'utilisateur : pourquoi les tout
premiers essais de lecture du numéro de série SWD (avant le premier
flash de la session) échouaient à détecter la présence du XIAO sur le
port USB.

**Cause identifiée** : `openocd` était lancé via l'outil **Bash**
(Git-Bash/MSYS) de Claude Code. 13 tentatives consécutives (deux
rounds) ont échoué de façon quasi systématique — soit « unable to find
a matching CMSIS-DAP device » (périphérique non trouvé du tout), soit
une connexion partielle interrompue en cours de transaction (CMD_INFO
jamais complété, aucun `Serial#` affiché). Une fausse piste
(`nrfutil-device.exe`, watcher hotplug de l'extension VS Code nRF
Connect, arrêté par précaution) n'a eu aucun effet sur les échecs
suivants, confirmant qu'elle n'était pas la cause. La commande
strictement identique lancée depuis l'outil **PowerShell** a réussi
dès le premier essai réel.

**Correctif appliqué le jour même** : règle dédiée dans
`C:\ncs\CLAUDE.md` (« PowerShell pour tout outil USB/série/HID, jamais
Bash/Git-Bash ») + réécriture directe en PowerShell des modèles de
commande `openocd` dans `Configuration-nRF54LM20A-System-ON-IDLE.md`
§5 et rappel dans `xiao_nrf54lm20a_project_notes.md` — la source de
copier-coller elle-même est corrigée, pas seulement une règle à se
rappeler.

### Étape B — IMU : 6 → 3 transactions I2C

1. `sample_motion()` modifié dans `main.c` : écriture groupée
   CTRL3_C/CTRL4_C/CTRL5_C/CTRL6_C (`i2c_write_dt`, 5 octets), ODR via
   `LSM6DSL_REG_CTRL1_XL` (écriture directe, plus `sensor_attr_set`),
   lecture XYZ brute (`i2c_burst_read_dt` sur `OUTX_L_XL`) au lieu de
   l'API capteur Zephyr. `device_init(imu_dev)` conservé avant ces
   écritures (init driver au 1er cycle). Variable `x,y,z` devenue
   inutilisée retirée. Chemins d'erreur (coupure rail + `return`)
   répliqués sur les 3 nouvelles opérations I2C.
2. Rebuild `--pristine` : FLASH 117240 B (contre 117396 à l'étape A),
   aucun nouveau warning de compilation. SHA-256 `zephyr.bin` =
   `827e446970a4d8670a505eb1f07dd8c9f5395aa9e6e25f54c78d6cff05235330`.
3. Flashé et vérifié sur #02 : `verified 117240 bytes`, S/N confirmé.
4. **Mesure PPK2** (`ppk-20260922T194943.csv`, 60 s) : charge/cycle
   **15,46 µC** (médiane 15,46), plancher 3,83 µA, **régime établi
   19,05 µA**. **Gain vs étape A : +0,37 µC/cycle (régression, pas un
   gain)** — sous le seuil de falsification 0,6 µC du §7. Amélioration
   de l'étape A toujours acquise sur la référence globale (24,47 →
   19,05 µA), mais l'étape B elle-même n'apporte rien de mesurable.
   Diagnostic prescrit par le plan : profiler la fenêtre de
   configuration IMU (`ppk_profile.py`) pour vérifier si le coût TWIM
   est proportionnel au nombre de transactions ou domine par un coût
   fixe (attendu ~0,6 ms au lieu de ~1,45 ms si proportionnel).
5. Profil de salve (20821-20838 ms, 50 µs) : plus de plateau long, IMU
   compressée en pics courts (<600 µs) — changement de code confirmé
   appliqué. Charge de la salve dominée par l'appel de charge du rail
   (~20-26 mA), pas par le nombre de transactions IMU. Gain propre de
   l'étape B noyé dans le bruit de ce poste dominant ; gain cumulé
   depuis la référence reste acquis. Code conservé (correct, sans
   régression réelle) ; étape C cible directement le poste dominant.

### Étape C — LDO1 commandé par broche (P1.25 → GPIO0 nPM1300)

1. **Caractérisation** (`xiao_ldo_gpio_char/`, firmware de diagnostic,
   console UART autorisée, jamais mesuré au PPK2) : 5 variantes ×
   20 essais, `t_up` mesuré via P0.08 (SDA) + `k_cycle_get_32()`.

   | Variante | t_up min/max/moy | WHO_AM_I |
   |---|---|---|
   | V0 (référence, `regulator_enable()`) | 3411/3955/3442 µs | 20/20 |
   | **V1 (front seul, aucun I2C)** | **1081/2024/1491 µs** | **20/20** |
   | V2 (front + lecture LDSWSTATUS immédiate) | 1145/2033/1593 µs | 20/20 |
   | V3 (front + 1 ms + lecture) | 1748/2296/1861 µs | 20/20 |
   | V4 (adressage seul, écriture 0 octet) | timeout ~500 ms (TWIM sur IMU non alimenté), WHO_AM_I 1/20 | rejeté |

   **V1 retenue** : cas anticipé par le plan (§8.3, « si V1 est déjà
   rapide : pas de lecture du tout ») — l'hypothèse errata [38] du §8.2
   ne se vérifie pas ici, V1 est même plus rapide que V2/V3. Aucune
   variante n'atteint ≤2 ms sur 20/20 au sens strict (dépassements
   marginaux de quelques dizaines de µs sur 1 essai/20 pour V1/V2),
   mais toutes (sauf V4) restent bien sous le seuil d'abandon 3 ms du
   plan de façon fiable — C n'est pas abandonnée.
2. **Production** (§8.4) : overlay (`dvs-gpios` sur `regulators`,
   `enable-gpio-config` sur `imu_vdd`/LDO1), `main.c` (`imu_rail_on()`/
   `imu_rail_off()` via `regulator_parent_dvs_state_set()`, **sans**
   lecture I2C errata — adapté suite à V1, tous les
   `regulator_enable/disable(imu_vdd_dev)` remplacés, `imu_vdd_dev`
   devenu inutilisé retiré, LDO1 forcé à l'arrêt une fois au démarrage
   (`TASK1CLR`, état PMIC persistant across reset). Rebuild propre,
   aucun nouveau warning. SHA-256 `zephyr.bin` =
   `d8006e18746eb4bb5fb47a4a829d532c0bad6a34acdb3894cba970bfdfd02e6d`.
   Flashé et vérifié sur #02 : `verified 117192 bytes`, S/N confirmé.
3. **Mesure PPK2** (`ppk-20260922T201129.csv`, 60 s) : charge/cycle
   **29,96 µC**, plancher 3,86 µA (sain — pas de risque, LDO se coupe
   bien), **régime établi 33,46 µA — RÉGRESSION** (pire que la
   référence initiale 24,47 µA, ~2× l'étape B). Profil de salve : pic
   anormal **~96 mA** (jamais vu aux étapes précédentes, max habituel
   ~20-26 mA) + plateau soutenu ~1 mA pendant plus de 10 ms.
   `regulator_parent_dvs_state_set()` ne se comporte manifestement pas
   comme un simple front sur LDSW1 isolé (hypothèse : action niveau
   parent PMIC, effet de bord possible sur d'autres régulateurs/BUCK2).
4. **Revert** (commit `795a14e`) : `main.c`/overlay restaurés à l'état
   de l'étape B (SHA-256 `zephyr.bin` identique à l'étape B,
   `827e4469...`, confirmant le revert byte pour byte). Reflashé et
   vérifié sur #02. Chemin de repli du plan (§4) appliqué : garder A+B,
   passer à D. La caractérisation elle-même (t_up via P0.08) reste
   valide — c'est l'intégration production via l'API DVS parent qui
   pose problème, pas la mesure.

### Étape D — Accéléromètre haute performance 833 Hz (commit `3ec95e0`)

1. **Caractérisation** (`xiao_accel_odr_char/`, 200 cycles × 3 configs,
   console UART). Capture fiable obtenue seulement avec `DtrEnable`/
   `RtsEnable` activés côté PowerShell (sans, sortie série
   intermittente malgré CPU actif confirmé par SWD) et
   `CONFIG_PICOLIBC_IO_FLOAT=y` (sans, `%f` affiche `*float*` — bug
   picolibc, pas une anomalie matérielle).

   | Config | PASS | t1 éch.#1 | erreur max #1 | erreur max #2 |
   |---|---|---|---|---|
   | 208 Hz normal (référence) | 190/200 | 5384 µs | 0,063 | 0,050 |
   | 833 Hz HP | 0/200 | 1873 µs | **0,459** | 0,067 |
   | 1,66 kHz HP | 0/200 | 1170 µs | **1,112** | 0,074 |

   Échantillon #1 inutilisable en HP (transitoire, jusqu'à 22× la
   limite 0,05 m/s²) ; échantillon #2 légèrement au-dessus de cette
   limite stricte mais très en dessous des seuils réels de production
   (mouvement 0,3 m/s², hystérésis angle 0,343 m/s²) — retenu.
2. **Production** : `CTRL1_XL=0x70` (833 Hz HP), `CTRL2_G=0`, `CTRL3_C`
   dans la même écriture groupée (texte identique au plan §9.2).
   `CTRL6_C` plus jamais écrite (HM_MODE sans effet à 833 Hz+, table 52
   fiche). `ACCEL_FIRST_SAMPLE_US=3100` (t1 833Hz 1873 µs + 1 période
   ODR 1200 µs). Rebuild propre, aucun nouveau warning. SHA-256
   `zephyr.bin` = `b54850ec5e29dfdb8530df934acbba85d5374565bcc06567b71778c3c21d78fa`.
   Flashé et vérifié sur #02 : `verified 117128 bytes`.
3. **Mesure PPK2** (`ppk-20260923T065203.csv`, 60 s) : charge/cycle
   **13,27 µC** (médiane 13,12), plancher 3,87 µA (inchangé, sain),
   **régime établi 16,98 µA**. Gain vs étape B : **2,19 µC/cycle**
   (prédiction 1,0-1,6 µC dépassée). Falsification §9.2 (gain < 0,6 µC)
   non déclenchée. **Progression totale depuis la référence :
   24,47 → 16,98 µA (−30,6 %).** Étape validée.

### Incidents/notes d'outillage (session, sans impact sur les mesures)

- `openocd` (lecture S/N, flash) doit s'exécuter via l'outil **PowerShell**,
  jamais Bash/Git-Bash, sur cette machine — Bash échoue de façon quasi
  systématique à parler au pont CMSIS-DAP SAMD11 (voir `C:\ncs\CLAUDE.md`
  et `Configuration-nRF54LM20A-System-ON-IDLE.md` §5, corrigés ce jour).
- Blocage persistant du pont SWD après une session de nombreuses tentatives
  rapprochées, résolu par cycle d'alimentation complet via PPK2
  (procédure déjà documentée dans `Procedure-Clonage...md`, § Notes de
  connexion SWD).
- Écriture matérielle (flash) bloquée par le classificateur auto-mode ;
  débloquée en sortant du mode auto (l'utilisateur approuve alors
  directement l'action).
