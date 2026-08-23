# Recherche — réveil matériel IMU + System OFF (XIAO nRF54LM20A Sense)

**Document de démarrage pour une nouvelle conversation.** Objectif :
étudier et vérifier la faisabilité d'une optimisation batterie avancée sur
le firmware `xiao_door_sensor` (capteur de porte/fenêtre), sans l'implémenter
tant que la recherche n'a pas confirmé chaque point. Le firmware actuel
fonctionne déjà bien (~2-7 ans théoriques sur LiPo 1500 mAh, voir contexte
ci-dessous) — cette recherche est une **optimisation, pas une urgence**.

## Règle de travail impérative pour cette recherche

**Étudier, vérifier, ne pas inventer.** Chaque affirmation technique de la
recherche à produire doit être traçable à une source primaire :
- Le code réellement présent dans `C:\ncs\v3.4.0` (SDK installé), à lire
  directement — ne jamais supposer le comportement d'un driver ou d'une API
  sans avoir lu son code.
- La datasheet ST du LSM6DS3TR-C et ses notes d'application officielles
  (recherche internet requise — ne pas deviner des adresses de registres ou
  des valeurs de bits de mémoire).
- La documentation officielle Nordic (DevZone, docs Nordic Semiconductor,
  dépôts GitHub `nrfx`/`hal_nordic`) pour tout ce qui concerne le
  comportement du System OFF sur nRF54L — cette puce est très récente
  (sortie 2025-2026), la documentation peut être incomplète ou dispersée ;
  le signaler explicitement plutôt que de combler les trous par supposition.
- Si une information ne peut pas être vérifiée (doc absente, comportement
  non documenté), le dire clairement plutôt que d'extrapoler depuis une
  puce Nordic différente (nRF52/53/54H) en supposant que ça se comporte
  pareil.

## Contexte — pourquoi cette recherche

Projet : XIAO nRF54LM20A Sense en capteur de porte/fenêtre BLE (BTHome v2)
remontant vers Home Assistant. 3 unités déployées et fonctionnelles à ce
jour, 10 de plus attendues fin septembre 2026. Firmware source :
`C:\ncs\projects\xiao_door_sensor\src\main.c`. Documentation complète du
projet (procédures de build/flash/vérification, architecture des trames
BTHome, historique des bugs trouvés et corrigés) :
`C:\ncs\projects\xiao_nrf54lm20a_project_notes.md` — **à lire en premier**
pour le contexte complet, ce document-ci ne le duplique pas.

Le firmware actuel sonde l'accéléromètre toutes les 2s en réveillant le CPU
à chaque fois (System ON + power management Zephyr standard), avec le
gyroscope explicitement coupé hors lecture (correctif appliqué le
2026-08-23, gain d'un facteur ~700x sur l'autonomie). Budget énergétique
estimé (théorique, **non mesuré**) : ~25-80 µA en moyenne, soit ~2 à 7 ans
sur une LiPo 1500 mAh.

**L'optimisation étudiée ici** : remplacer le sondage logiciel périodique
par un réveil purement matériel — le SoC reste en **System OFF** (le mode
le plus bas de consommation) et ne se réveille que lorsque l'IMU détecte
elle-même un mouvement via son interruption matérielle (INT1), plutôt que
de réveiller le CPU toutes les 2s pour lui demander "as-tu bougé ?". Gain
espéré : modeste par rapport au correctif gyroscope (quelques µA sur un
budget déjà de ~25-80 µA), donc à ne poursuivre que si le reste s'avère
simple/fiable — voir « Décision à prendre » en fin de document.

## Ce qui est déjà confirmé (vérifié dans le SDK le 2026-08-23, à revérifier si besoin)

1. **Pin d'interruption INT1 de l'IMU câblé et connu** :
   ```
   C:\ncs\vendor\platform-seeedboards\zephyr\boards\arm\xiao_nrf54lm20a\nrf54lm20a_cpuapp_common.dtsi:204
   irq-gpios = <&gpio0 6 GPIO_ACTIVE_HIGH>;
   ```
   sur le nœud `lsm6ds3tr_c` (bus `i2c30`, adresse `0x6a`).

2. **Le driver Zephyr `lsm6dsl` ne supporte qu'un seul type de trigger** :
   ```
   C:\ncs\v3.4.0\zephyr\drivers\sensor\st\lsm6dsl\lsm6dsl_trigger.c:51
   __ASSERT_NO_MSG(trig->type == SENSOR_TRIG_DATA_READY);
   ```
   Pas de `SENSOR_TRIG_MOTION` ou équivalent — un réveil sur seuil de
   mouvement matériel nécessite de sortir de l'API `sensor_trigger`
   standard et d'écrire les registres du LSM6DS3TR-C directement en I2C
   (bus `i2c30`, device déjà initialisé par le driver — voir comment
   obtenir un accès I2C bas niveau sans conflit avec le driver existant :
   **point à étudier**).

3. **System OFF supporté sur la famille nRF54L dans ce SDK**, avec un
   mécanisme de rétention RAM sélective :
   ```
   C:\ncs\v3.4.0\zephyr\soc\nordic\common\poweroff.c
   ```
   Voir en particulier les branches `#if defined(CONFIG_SOC_SERIES_NRF54L)`
   (désactivation de la rétention RAM par défaut, sauf zones définies via
   `CONFIG_RETAINED_MEM_NRF_RAM_CTRL` + devicetree `retained_mem`).

4. **Chiffre de consommation System OFF mesuré par Seeed** (pas nous) pour
   cette carte précise : ~4,76 µA (source : caractéristiques publiées par
   Seeed pour la XIAO nRF54LM20A Sense — **à retrouver et citer la source
   exacte**, ce chiffre vient d'une réponse antérieure dans la conversation
   d'origine, pas d'une vérification directe de ce document-ci).

## Questions ouvertes à rechercher

1. **Registres du LSM6DS3TR-C pour le réveil sur mouvement** :
   adresses exactes, format et valeurs de `WAKE_UP_THS`, `WAKE_UP_DUR`,
   `MD1_CFG` (routage de l'interruption vers INT1), et toute dépendance
   (ex. filtre configuré au préalable, mode de fonctionnement de
   l'accéléromètre requis). Source : datasheet ST officielle du
   LSM6DS3TR-C (rechercher le PDF officiel ST, pas un résumé tiers) +
   notes d'application ST sur le "wake-up interrupt" si elles existent.

2. **Interaction avec le driver Zephyr existant** : peut-on écrire ces
   registres directement via le bus I2C (`i2c30`) sans perturber le
   fonctionnement du driver `lsm6dsl` pour les lectures normales
   d'accélération/gyroscope/température ? Faut-il un device I2C séparé ou
   réutiliser le handle existant ? Étudier `lsm6dsl.c`/`lsm6dsl.h` dans
   `C:\ncs\v3.4.0\zephyr\drivers\sensor\st\lsm6dsl\` pour voir si le driver
   expose un moyen propre de faire des accès registre bruts, ou s'il faut
   un device I2C Zephyr indépendant sur la même adresse.

3. **Réveil du nRF54L sur base de temps (RTC) pendant le System OFF** :
   la trame santé (batterie/température, toutes les 15 min) a besoin d'un
   réveil périodique même sans mouvement. Sur les anciens nRF52, le System
   OFF classique ne se réveille QUE sur GPIO/reset/NFC, pas sur RTC. Est-ce
   toujours le cas sur nRF54L ? Y a-t-il un domaine "always-on" (GRTC ou
   équivalent) qui survit au System OFF sur cette puce ? Chercher la
   documentation Nordic officielle (infocenter/DevZone) sur le System OFF
   du nRF54L spécifiquement — **ne pas supposer que ça se comporte comme
   un nRF52**.

4. **Comportement réel au réveil** : le System OFF sur nRF SoCs redémarre
   l'exécution depuis `main()` (pas une reprise de contexte comme un sleep
   classique) — confirmer que c'est bien le cas sur nRF54L, et étudier
   concrètement comment utiliser `CONFIG_RETAINED_MEM_NRF_RAM_CTRL` pour
   conserver le compteur `packet_id` BTHome et l'historique anti-rafale
   (`frame_a_send_times[]` dans `main.c`) à travers ces redémarrages —
   chercher un exemple Zephyr fonctionnel (`samples/boards/nordic/`
   dans le SDK, ou exemples Nordic officiels) plutôt que d'improviser la
   configuration devicetree du `retained_mem`.

5. **Chiffrer un budget énergétique réaliste** pour la solution complète
   (System OFF + réveil GPIO/IMU + réveil périodique pour la trame santé),
   à partir de sources vérifiables (datasheet nRF54LM20A pour la
   consommation System OFF officielle Nordic — peut différer du chiffre
   Seeed cité en `4.` ci-dessus, à comparer) et non d'une extrapolation.

## Résultats de la recherche (2026-08-23)

Sources primaires téléchargées localement dans `C:\ncs\projects\docs\`
(voir `docs/README.md` pour l'index, les URLs d'origine et les pages
exactes) — chaque affirmation ci-dessous a été vérifiée par lecture
directe de ces PDF (`pdftotext`), pas seulement rapportée par les
agents de recherche qui les ont initialement localisés.

### Q1 — Registres LSM6DS3TR-C pour le réveil sur mouvement

**Confirmé, avec un point à corriger dans le code existant (voir
« Découverte annexe » plus bas).** Datasheet ST DocID030071 Rev 3
(la datasheet **du -C**, pas une extrapolation depuis le LSM6DS3 de
base — le titre et toutes les sections le confirment).

| Registre | Adresse | Bits pertinents | Détail |
|---|---|---|---|
| `WAKE_UP_THS` | `0x5B` | bit7 `SINGLE_DOUBLE_TAP`, bits5:0 `WK_THS[5:0]` | Seuil, 1 LSb = FS_XL/2⁶ (ex. ±2 g → ~31,25 mg/LSb) |
| `WAKE_UP_DUR` | `0x5C` | bit7 `FF_DUR5`, bits6:5 `WAKE_DUR[1:0]`, bit4 `TIMER_HR`, bits3:0 `SLEEP_DUR[3:0]` | `WAKE_DUR` : 1 LSb = 1×ODR_time |
| `MD1_CFG` | `0x5E` | bit5 `INT1_WU` | Routage de l'event wake-up vers INT1 |
| `TAP_CFG` | `0x58` | bit7 `INTERRUPTS_ENABLE`, bits6:5 `INACT_EN[1:0]`, bit4 `SLOPE_FDS`, bits3:1 `TAP_X/Y/Z_EN`, bit0 `LIR` | `INTERRUPTS_ENABLE=1` est **obligatoire** (porte globale) pour que le wake-up fonctionne, quel que soit `MD1_CFG` |
| `CTRL6_C` | `0x15` | bit4 `XL_HM_MODE` | `1` = mode low-power accéléromètre (ODR ≤ 52 Hz) |

Ces adresses/bits ont été vérifiées **deux fois** : une fois via
lecture directe de la datasheet ST (`docs/LSM6DS3TR-C_datasheet_DocID030071_Rev3.pdf`,
pages 88/90/92/66), et une fois par comparaison avec les définitions
déjà présentes (mais non utilisées) dans le driver Zephyr
`lsm6dsl.h` (`C:\ncs\v3.4.0\zephyr\drivers\sensor\st\lsm6dsl\lsm6dsl.h:444-524`)
— **les deux concordent exactement, bit pour bit**.

Séquence de configuration minimale déduite de la datasheet : ODR
accéléromètre non nul (`CTRL1_XL`) → `XL_HM_MODE=1` si low-power
souhaité → `WAKE_UP_THS` (seuil) → `WAKE_UP_DUR` (durée) → `TAP_CFG`
avec `INTERRUPTS_ENABLE=1` → `MD1_CFG` avec `INT1_WU=1`.

**Non déterminé** : la consommation typique en mode « low-power avec
wake-up activé » n'est **pas documentée séparément** par ST — la seule
valeur disponible est `LA_IddLM` = accéléromètre seul en low-power à
12,5 Hz, sans préciser si la logique d'interruption wake-up est
incluse dans cette mesure ou non (voir « Découverte annexe » — cette
valeur a un impact direct et déjà mesurable sur le firmware actuel).
La note d'application ST AN5130, qui aurait pu détailler ce point,
n'a pas pu être récupérée (blocage anti-bot st.com, aucun miroir
trouvé) — gap explicitement non comblé, pas d'extrapolation.

### Q2 — Interaction avec le driver Zephyr existant

**Confirmé : accès direct possible sans conflit, sans device
supplémentaire.** Le driver `lsm6dsl_i2c.c`
(`C:\ncs\v3.4.0\zephyr\drivers\sensor\st\lsm6dsl\lsm6dsl_i2c.c:21-51`)
n'utilise que l'API I2C standard Zephyr (`i2c_burst_read_dt`,
`i2c_reg_update_byte_dt`) sur un `struct i2c_dt_spec` interne au
driver (non exposé publiquement). Une application peut construire son
propre `i2c_dt_spec` pointant vers le même bus (`i2c30`) et la même
adresse (`0x6a`) sans toucher au devicetree existant ni créer de
second nœud — l'API `i2c_transfer` de Zephyr sérialise déjà les
transactions au niveau du bus, donc pas de risque de collision avec
les lectures normales du driver tant que les accès restent des
transactions I2C complètes et non entrelacées manuellement.

### Q3 — Réveil du nRF54L sur base de temps (RTC) pendant le System OFF

**Confirmé sans ambiguïté — bien meilleur que sur nRF52.** Datasheet
Nordic 4539_001 v1.0, §5.2 « System OFF mode », p. 70 (citation
vérifiée mot pour mot par lecture directe du PDF) :

> "The following wakeup sources will initiate a wakeup from System
> OFF: The DETECT signal generated by the GPIO peripheral; The
> ANADETECT signal generated by the LPCOMP peripheral; The SENSE
> signal generated by the NFCT peripheral to wake-on-field; **The
> SYSCOUNTER compare event generated by the GRTC peripheral**; A
> valid USB voltage on the VBUS pin...; A debug session is started; A
> pin reset."

Le GRTC (Global RTC) est dans un **domaine « always-on »** qui bascule
automatiquement sur le quartz basse fréquence (LFXO, 32,768 kHz) en
System OFF (§8.11, p. 312-313) — c'est le mécanisme qui manquait sur
nRF52. API Zephyr : `z_nrf_grtc_wakeup_prepare(wake_time_us)`
(`C:\ncs\v3.4.0\zephyr\include\zephyr\drivers\timer\nrf_grtc_timer.h:239`),
temps relatif en microsecondes — pour la trame santé toutes les 15
min, passer `15 * 60 * 1000000`.

**Le GRTC est déjà activé sur la XIAO elle-même**, pas seulement sur
une DK de référence :
```
C:\ncs\vendor\platform-seeedboards\zephyr\boards\arm\xiao_nrf54lm20a\nrf54lm20a_cpuapp_common.dtsi:57-62
&grtc {
    owned-channels = <0 1 2 3 4 5 6 7 8 9 10 11>;
    child-owned-channels = <3 4 7 8 9 10 11>;
    status = "okay";
};
```
Temps de réveil documenté : `tOFF2ON` = 37 µs typ (déclenchement →
première instruction CPU).

### Q4 — Comportement réel au réveil + rétention RAM

**Confirmé explicitement pour le nRF54L (pas par analogie nRF52).**
Datasheet Nordic §5.2, p. 70 : *"When the device wakes up from System
OFF, a system reset is performed."* — reset complet, comme un POR, pas
de reprise de contexte. Précondition documentée avant l'entrée en
System OFF : *"The register RESET.RESETREAS must be cleared. Failure
to do so can make the system immediately wake up from System OFF
mode."* — **déjà géré correctement par le SDK** :
`C:\ncs\v3.4.0\zephyr\soc\nordic\common\poweroff.c:77`
(`nrfx_reset_reason_clear(UINT32_MAX)` dans la branche
`CONFIG_SOC_SERIES_NRF54L`), donc rien à ajouter de ce côté.

**Exemple Zephyr officiel trouvé, avec overlay dédié à notre SoC
exact** (pas juste « un nRF54L quelconque ») :
`C:\ncs\v3.4.0\zephyr\samples\boards\nordic\system_off\`, testé en CI
sur `nrf54lm20dk/nrf54lm20a/cpuapp` (`sample.yaml`). Contient :
- `src/main.c` : détection de la cause de reset via
  `hwinfo_get_reset_cause()` (`RESET_CLOCK` = réveil GRTC,
  `RESET_LOW_POWER_WAKE` = réveil GPIO), appel de
  `z_nrf_grtc_wakeup_prepare()` avant `sys_poweroff()`.
- `src/retained.c` : structure retenue avec CRC32 de validation
  (`retained_validate()`/`retained_update()`) — directement adaptable
  pour `packet_id` et `frame_a_send_times[]`.
- `boards/nrf54lm20dk_nrf54lm20_a_b_cpuapp.dtsi` : nœud devicetree
  `compatible = "zephyr,retained-ram"` réservant 4 KB en haut de la
  SRAM (512 → 507 KB utilisables + 4 KB retenus). `CONFIG_RETAINED_MEM_NRF_RAM_CTRL`
  se sélectionne automatiquement dès que ce nœud + `CONFIG_POWEROFF`
  sont présents — rien à activer manuellement en Kconfig au-delà du
  devicetree.

Le SRAM de base de la XIAO (`&cpuapp_sram { status = "okay"; }`,
hérité de `nordic/nrf54lm20a_cpuapp.dtsi`, même puce que la DK) permet
de transposer ce pattern directement, sous réserve de vérifier la
taille SRAM exacte de la nRF54LM20A avant de choisir l'adresse de la
région retenue.

### Q5 — Budget énergétique réaliste

**Chiffres officiels Nordic** (datasheet §11.2.1.1, p. 1253, puce nue
sans PMIC/carte, 0 KB RAM retenue) :

| Scénario | Typ. |
|---|---|
| System OFF, réveil GPIO seul | 0,7 µA |
| System OFF, réveil GPIO + GRTC/LFXO | 1,0 µA |
| System ON idle, réveil GPIO, 512 KB RAM retenue | 4,0 µA |
| System ON idle, réveil GPIO + GRTC, 512 KB RAM retenue | 4,3 µA |

**Chiffre Seeed 4,76 µA — vérifié comme réellement publié par Seeed**
(`wiki.seeedstudio.com/xiao_nrf54lm20a_getting_started/`), mais c'est
une **mesure carte complète** (PMIC nPM1300, régulateurs, IMU inclus),
donc structurellement plus élevée que les 0,7-1,0 µA puce nue Nordic —
cohérent une fois qu'on tient compte du niveau de mesure, mais aucun
document ne détaille la décomposition exacte de cet écart (~4 µA
attribuable au PMIC/à la carte, non confirmé chiffre par chiffre).

**Non documenté** : la consommation System OFF avec RAM retenue
(seul le cas 0 KB est chiffré pour System OFF dans le datasheet) — à
mesurer réellement (PPK II) plutôt qu'à extrapoler.

**Recalcul du gain réel de l'optimisation étudiée** : l'IMU en
low-power (9 µA, voir découverte ci-dessous) doit rester actif en
continu dans les deux architectures (c'est lui qui génère
l'interruption de réveil) — son courant ne varie donc pas entre
« System ON + sondage logiciel » et « System OFF + réveil matériel ».
Le gain réel de cette optimisation se limite donc au delta côté SoC
seul : ~3 à 3,3 µA (`IONIDLE1/2` System ON avec RAM retenue, 4,0-4,3 µA,
vs `IOFF1` System OFF + GRTC, 1,0 µA — en supposant une RAM retenue
comparable, non confirmée pour le cas System OFF). C'est cohérent avec
ce que le document de démarrage anticipait déjà (« gain espéré :
modeste... quelques µA ») — la recherche confirme l'ordre de grandeur
plutôt que de le corriger.

### Découverte annexe (hors des 5 questions posées, mais directement
liée au budget énergétique — Q5) : la valeur « 1,25 µA » citée dans le
code actuel ne correspond pas à la datasheet

Le commentaire dans `xiao_door_sensor/src/main.c:220-221` affirme :
*« Accéléromètre seul : ~1,25 µA en low-power @12,5 Hz (datasheet
LSM6DS3TR-C) »* — et le firmware configure bien l'ODR à 12,5 Hz
(`main.c:231`, `odr_attr = { .val1 = 12, .val2 = 500000 }`), donc la
condition de mesure correspond exactement à la ligne du tableau
vérifiée directement dans la datasheet ST (Table 4, p. 24,
`pdftotext -raw`) :

```
LA_IddLM  Accelerometer current consumption in low-power mode  ODR = 12.5 Hz  9 µA
```

**La datasheet donne 9 µA typ., pas 1,25 µA** — un facteur ~7,2×. Ce
n'est pas une question d'interprétation : la ligne du tableau
correspond exactement à la config firmware (accéléromètre seul,
low-power, 12,5 Hz). Origine probable de l'erreur (à titre indicatif,
non vérifiable a posteriori) : 0,9 mA (gyro actif en continu,
`IddHP`) / 1,25 µA ≈ 720 — proche du facteur « ~700x » cité dans le
même commentaire et dans `xiao_nrf54lm20a_project_notes.md`, ce qui
suggère que 1,25 µA a été déduit à l'envers pour faire coller ce
facteur plutôt que lu dans la datasheet.

**Conséquence** : la décision architecturale de garder le gyroscope
coupé hors lecture reste entièrement justifiée — même avec 9 µA au
lieu de 1,25 µA pour l'accéléromètre, le facteur gyro-actif-en-continu
vs gyro-coupé reste **~100×** (0,9 mA / 9 µA), pas 700× mais toujours
massivement dominant. Le budget global du firmware actuel (annoncé
~25-80 µA en moyenne) doit cependant être revu à la hausse d'au moins
+7,75 µA sur son terme accéléromètre — **à corriger dans
`xiao_nrf54lm20a_project_notes.md` et dans le commentaire du code**
(changement de commentaire uniquement, pas de changement de
comportement — proposé séparément, pas fait ici sans confirmation).

## Décision à prendre à l'issue de la recherche

Ne pas commencer l'implémentation avant d'avoir, pour chaque question
ci-dessus, soit une réponse vérifiée et sourcée, soit un constat explicite
que l'information n'est pas disponible et qu'il faudra la déterminer par
l'expérimentation directe sur le matériel. Une fois la recherche terminée,
présenter un bilan clair : faisabilité confirmée ou non, complexité réelle
estimée, et gain énergétique attendu recalculé à partir de chiffres
vérifiés (pas des estimations de la conversation d'origine) — pour
trancher si ça vaut l'effort face à la limite de vieillissement calendaire
de la batterie LiPo elle-même (déjà notée dans
`xiao_nrf54lm20a_project_notes.md` comme probable facteur limitant réel,
au-delà d'un certain seuil de consommation déjà atteint).

## Bilan (2026-08-23)

**Faisabilité : confirmée pour les trois briques techniques testées.**
Aucun point bloquant trouvé :
- Les registres IMU nécessaires (adresses, bits) sont vérifiés dans la
  datasheet ST officielle et concordent avec les définitions déjà
  présentes dans le driver Zephyr `lsm6dsl.h` — aucune API cachée ou
  comportement non documenté à ce niveau.
- L'accès I2C bas niveau ne nécessite pas de contourner le driver
  existant de façon risquée — juste un second `i2c_dt_spec` sur la
  même adresse.
- Le System OFF du nRF54L dispose bien d'un réveil RTC (GRTC,
  domaine always-on) — le point d'incertitude le plus sérieux du
  document de départ (« est-ce comme le nRF52 ? ») est levé : **non**,
  le nRF54L fait mieux. Le GRTC est déjà actif sur le devicetree de la
  XIAO.
- Le redémarrage complet depuis `main()` au réveil et le mécanisme de
  rétention RAM sont confirmés par un exemple Zephyr officiel testé en
  CI sur `nrf54lm20dk/nrf54lm20a/cpuapp` — le même SoC que la XIAO.

**Complexité réelle estimée : modérée, pas triviale.** Le travail
resterait non négligeable : écriture registre brute IMU (hors API
`sensor_trigger` standard, donc code à maintenir soi-même sans le
filet du driver), restructuration de `main.c` autour d'un
redémarrage complet à chaque cycle plutôt qu'une boucle continue (le
firmware actuel suppose un état vivant entre les mesures — anti-rafale,
compteurs — qui devrait être explicitement sauvé/restauré via
`retained_mem` à chaque réveil), et calibrage empirique du seuil/de la
durée de wake-up (aucune valeur toute prête, à régler sur le matériel
réel). Pas un simple ajout de Kconfig.

**Gain énergétique recalculé : plus modeste que ce que les chiffres
carte (Seeed, ~4,76 µA) pouvaient laisser espérer.** L'IMU en
low-power (9 µA vérifié, pas 1,25 µA comme actuellement documenté —
voir « Découverte annexe » ci-dessus) doit tourner en continu dans les
deux architectures pour générer l'interruption de réveil — son
courant ne change pas entre les deux approches. Le delta réel se
limite au SoC seul : de l'ordre de **3 à 3,3 µA** (comparaison
`IONIDLE` System-ON-idle-RAM-retenue vs `IOFF1` System-OFF+GRTC —
chiffre à confirmer, le cas System OFF avec RAM retenue n'est pas
chiffré séparément par Nordic). Sur un budget déjà descendu à
25-80 µA (à corriger vers le haut de quelques µA suite à la
découverte du §Q5), quelques µA de moins représentent un gain
proportionnellement faible.

**Recommandation** : la recherche confirme ce que le document de
démarrage anticipait déjà — techniquement faisable et sans blocage
caché, mais gain modeste comparé à l'effort (réécriture non triviale
autour d'un redémarrage complet à chaque cycle) et comparé au facteur
limitant probable déjà identifié (vieillissement calendaire de la LiPo,
indépendant de la consommation une fois un certain seuil déjà atteint
par le correctif gyroscope). Avant de se lancer dans l'implémentation,
il serait plus rentable de d'abord mesurer le budget réel actuel au
PPK II (déjà en prochaine étape n°6 de `xiao_nrf54lm20a_project_notes.md`)
pour savoir si le firmware actuel est déjà proche du plancher
IMU-toujours-actif (~10-12 µA SoC+IMU, hors PMIC/radio) — auquel cas
cette optimisation ne vaudrait vraiment la peine que si la mesure
réelle s'écarte significativement de cette estimation théorique.
Décision de poursuivre ou non laissée à l'utilisateur — pas
d'implémentation entamée dans le cadre de cette recherche, conformément
à la règle de travail du document.

## Budget énergétique complet — scénario batterie 600 mAh (2026-08-23)

Objectif exprimé par l'utilisateur : passer à une batterie plus fine et
de moindre capacité (400-600 mAh au lieu de 1500 mAh) en gardant 3-4+
ans d'autonomie. Ceci nécessite de chiffrer **tous** les postes de
consommation, pas seulement le delta System ON/OFF étudié plus haut.
Pas de PPK II disponible pour le moment (confirmé par l'utilisateur) —
ce qui suit est un budget **calculé à partir de datasheets + du code
réel**, pas une mesure. Chaque ligne est marquée **VÉRIFIÉ** (chiffre
de datasheet lu directement) ou **ESTIMÉ** (calcul à partir du
comportement du firmware, non mesuré) — à ne pas confondre.

### Sources ajoutées pour ce budget

- `docs/nPM1300_ProductSpecification_v1.1.pdf` (Nordic, doc 4490_483
  v1.1, 2024-06-16) — récupéré via miroir MikroElektronika (le PDF
  direct nordicsemi.com/Mouser/DigiKey n'a pas été localisé ; seule
  une "Product Brief" marketing incomplète y est hébergée ; contenu du
  miroir vérifié identique par lecture directe page 16, même
  identifiant de document en pied de page).
- Table « Power consumption highlights » et §11.2.1.6 « RADIO
  transmitting/receiving » du datasheet nRF54LM20A déjà téléchargé
  (`docs/nRF54LM20A_nRF54LM20B_Datasheet_v1.0.pdf`), vérifiés
  directement (`pdftotext -raw`).

### Postes de consommation identifiés

| Poste | Soft actuel | Soft optimisé (System OFF) | Statut | Source |
|---|---|---|---|---|
| IMU LSM6DS3TR-C, low-power @12,5 Hz | 9 µA | 9 µA (inchangé — doit tourner en continu pour générer l'interruption de réveil) | **VÉRIFIÉ** | ST DocID030071 Rev 3, Table 4 p.24 |
| SoC nRF54LM20A, veille | 4,3 µA (`IONIDLE2`, System ON idle + GRTC + 512 KB RAM retenue) | 1,0 µA (`IOFF1`, System OFF + réveil GRTC) | **VÉRIFIÉ** | Nordic 4539_001 v1.0, §11.2.1.1 p.1253 + "Power consumption highlights" |
| PMIC nPM1300, quiescent | ≥0,8 µA (`IQBAT`, sans charge BUCK, VBUS déconnecté) | ≥0,8 µA (identique — le PMIC n'est pas concerné par le choix System ON/OFF du SoC) | **VÉRIFIÉ comme plancher, incomplet** | Nordic 4490_483 v1.1, Table 4 p.16 |
| Boucle logicielle (sondage 2s) | ~2,4 µA | ~0 µA (réveil uniquement sur IMU/GRTC) | **ESTIMÉ, non mesuré** | calcul ci-dessous |
| Radio BLE (rafales publicitaires, porte au repos) | 0,06-6 µA | 0,06-6 µA (identique — indépendant du choix System ON/OFF) | **ESTIMÉ, non mesuré** | calcul ci-dessous |
| **Total (repos, porte immobile)** | **~16,6 à 22,6 µA** (milieu ~19,6 µA) | **~10,9 à 16,9 µA** (milieu ~13,9 µA) | mixte | — |

**Réserve sur le PMIC** : `IQBAT` = 0,8 µA typ. est documenté "sans
charge BUCK, VBUS déconnecté" — c'est le courant de repos du circuit
de contrôle/monitoring du PMIC lui-même, **pas garanti inclure** la
consommation propre du LDO1 (qui alimente l'IMU, donc sous charge
réelle ~9 µA) ni du BUCK principal (qui alimente le SoC, sous charge
réelle). Aucun chiffre de quiescent current n'est documenté pour le
LDO1 sous faible charge dans la datasheet nPM1300 (vérifié
explicitement absent, Table 24 p.73). Le PMIC intègre un mode BUCK
hystérétique documenté comme efficace "down to 1 µA load currents",
ce qui limite le risque d'une surprise majeure, mais **ce poste doit
être traité comme un plancher, pas une valeur complète**, tant qu'une
mesure réelle ne le confirme pas.

### Calcul détaillé — boucle logicielle (soft actuel uniquement)

Sondage toutes les 2s (`POLL_INTERVAL_MS`, `main.c:45`) → 43 200
réveils/jour. Hypothèse (non mesurée) : ~2 ms de CPU actif par cycle
(lecture I2C de l'accéléromètre + calcul pitch/roll), au courant CPU
actif `IAPPCPU0` = 2,4 mA (Coremark @128MHz, Nordic 4539_001 v1.0
§11.2.1.4) comme ordre de grandeur — pas une mesure du code réel,
juste le seul point de référence CPU-actif disponible dans la
datasheet.

```
43200 cycles/jour × 2 ms = 86,4 s de CPU actif/jour
(86,4 / 86400) × 2,4 mA ≈ 2,4 µA de moyenne
```

Éliminé en soft optimisé, où le CPU ne se réveille que sur
interruption IMU réelle (mouvement) ou événement GRTC périodique
(trame santé 15 min) — fréquence bien plus faible, contribution
résiduelle jugée négligeable (<0,1 µA) sans calcul détaillé.

### Calcul détaillé — radio BLE (identique dans les deux architectures)

Cas « porte au repos », d'après le rythme réel du firmware
(`main.c:594` s.) : trame B toutes les 15 min (96/jour), trame A
heartbeat toutes les 60 min (24/jour) + trame C à chaque trame A
(24/jour) = **144 rafales publicitaires/jour**. Chaque rafale :
`bt_le_adv_start`/`bt_le_adv_stop` sur 700 ms, intervalle 100 ms
(`ADV_BURST_MS`/`ADV_INT`, `main.c:54-55`) → ~7 événements
publicitaires par rafale, 3 canaux primaires par événement.

Deux hypothèses de calcul, l'écart entre les deux illustrant
l'incertitude réelle :
- **Borne haute (grossière)** : courant radio TX (`IRADIO_TX0` = 5,0 mA
  @0 dBm, Nordic §11.2.1.6) appliqué à toute la fenêtre de 700 ms de
  chaque rafale (surestimation probable — la radio n'émet pas en
  continu pendant 700 ms) :
  `(144 × 0,7 s / 86400 s) × 5,0 mA ≈ 5,8 µA`
- **Borne basse (temps d'antenne réel)** : ~3 canaux × ~0,28 ms
  d'émission effective par événement (formule standard BLE 1M PHY :
  préambule 8 bits + adresse d'accès 32 bits + PDU ~25 octets + CRC 24
  bits ≈ 280 µs/canal), soit ~7 événements × 3 canaux × 0,28 ms ≈ 5,9 ms
  de temps d'antenne réel par rafale de 700 ms :
  `(144 × 5,9 ms / 86400000 ms) × 5,0 mA ≈ 0,06 µA`

Aucune des deux bornes ne compte le surcoût CPU/contrôleur BLE
(ordonnancement du Link Layer, rampe radio) entre les événements — la
vraie valeur se situe probablement entre les deux, plus proche de la
borne basse. **Plage retenue par prudence : 0,06 à 6 µA.** Une porte
qui s'ouvre/se ferme souvent ajoute des rafales A+C supplémentaires
(et une rafale « repos » différée) — ce poste augmente alors dans les
deux architectures de façon identique, sans changer la comparaison
soft actuel vs optimisé.

### Autonomie estimée sur 600 mAh

Hypothèse optimiste : 100 % de la capacité nominale utilisable (pas de
marge de fin de décharge, pas de dégradation de capacité liée au
vieillissement — à ne pas confondre avec le facteur limitant
« vieillissement calendaire » évoqué plus haut, qui lui reste valable
indépendamment de ce calcul).

```
autonomie (heures) = 600 000 µAh / courant moyen (µA)
```

| Scénario | Courant moyen | Autonomie |
|---|---|---|
| Soft actuel, milieu de plage (~19,6 µA) | 19,6 µA | **~3,5 ans** |
| Soft actuel, plage complète (16,6-22,6 µA) | — | 3,0 à 4,1 ans |
| Soft optimisé, milieu de plage (~13,9 µA) | 13,9 µA | **~4,9 ans** |
| Soft optimisé, plage complète (10,9-16,9 µA) | — | 3,9 à 6,4 ans |

### Conclusion pour l'objectif 600 mAh / 3-4+ ans

**Plausible dans les deux cas selon cette estimation**, avec une marge
nettement meilleure côté soft optimisé. Le soft actuel (sans
implémenter le System OFF) atteint déjà l'objectif bas (3 ans) dans la
plupart des hypothèses, mais reste tendu pour 4 ans si le poste radio
ou le poste PMIC se révèlent en réalité plus élevés que le plancher
estimé ici. Le soft optimisé donne une marge confortable même dans
l'hypothèse pessimiste.

**Ceci reste un calcul, pas une mesure** — les postes marqués ESTIMÉ
(polling, radio) et le plancher PMIC non confirmé sont les principales
sources d'incertitude. Avant de commander des cellules 600 mAh,
recommandation : mesurer le courant moyen réel du soft actuel sur le
matériel existant (PPK II ou, à défaut, un multimètre de precision en
série avec la batterie sur plusieurs heures/jours pour une moyenne) —
cela validerait ou invaliderait directement toute cette section sans
dépendre d'aucune des estimations ci-dessus.

## Suite — implémentation réalisée et validée (2026-08-30)

Décision prise par l'utilisateur : implémenter l'approche "soft
optimisé" (System OFF hybride) malgré le gain modeste estimé
ci-dessus, dans l'optique du projet 600 mAh. Implémentée et validée sur
matériel réel sur les **3 unités déployées** — détail complet (bugs
trouvés en testant, corrections, architecture finale) dans
`xiao_nrf54lm20a_project_notes.md` § « Implémentation System OFF ».

**Ce qui reste à faire, sans changement par rapport aux recommandations
de ce document** : mesure de consommation réelle au PPK II, pour
remplacer par des chiffres mesurés (a) le budget théorique calculé plus
haut (§ « Budget énergétique complet »), notamment les postes ESTIMÉ
(radio BLE, boucle logicielle résiduelle) et le plancher PMIC
incomplet, et (b) confirmer ou infirmer le gain réel de l'architecture
System OFF par rapport à l'ancien firmware toujours actif. Décision
finale sur la taille de batterie (400 vs 600 mAh) à prendre une fois
ces mesures disponibles.
