# Audit consommation — XIAO nRF54LM20A, 2026-09-13

**Commandé explicitement par l'utilisateur** suite à la réponse de Nordic du
2026-09-07 (§8.3 point 5 du rapport support, `PP_OD` sans effet), jugée comme
fermant les pistes côté support. Objectif : audit complet, rigoureux,
impartial, **sans garder pour acquises les conclusions des tests précédents
ni les affirmations du code en place** — puis un plan d'amélioration de la
consommation qui préserve la réactivité actuelle (~1 s).

Méthode suivie : relecture du code source réellement déployé (pas
seulement la documentation), vérification des affirmations du rapport
Nordic et des docs internes contre le code source des drivers NCS/Zephyr
et les fiches techniques primaires (pas de confiance a priori), recherche
de pistes jamais testées.

---

## 1. Constat général

La conclusion du rapport Nordic (~20-22 µA, ~239 µA résiduel inexpliqué sur
le rail `imu_vdd`/LDO1, objectif 5-6 µA non atteint) est confirmée exacte
dans son résultat mesuré. Mais **deux pistes significatives n'ont jamais
été testées**, et **un problème d'ingénierie plus fondamental et non
résolu a été trouvé en cours d'audit** (reconstruction du firmware non
reproductible). Le tout est détaillé ci-dessous, par ordre de priorité.

---

## 2. TROUVAILLE PRINCIPALE — le microphone PDM n'a probablement jamais atteint son "Sleep Mode" documenté

### 2.1 Ce qui était déjà su, mais pas exploité

`archive/docs-historique/nPM1300-LDO1-quiescent-current-issue.md` (2026-08-27)
note déjà, sans aller au bout du raisonnement :

> Its own datasheet specifies 290 µA typ. in "Low-Power Mode" vs. 1 µA typ.
> in "Sleep Mode" depending on clock condition — **close to the measured
> anomaly** — so its clock pin was driven to a defined level (instead of
> left floating) as a test. No change in measured current, ruling this out.

Le micro partagé avec `imu_vdd` est un **MSM261D3526H1CPM** (MEMSensing).
Sa fiche technique (`files.seeedstudio.com/wiki/XIAO-BLE/mic-MSM261D3526H1CPM-ENG.pdf`,
vérifiée directement, pages 3 et 6 lues intégralement pour cet audit) donne :

| Mode | Condition d'horloge (fCLOCK) | Courant typique |
|---|---|---|
| Sleep Mode | 0 à 50 kHz | **1 µA** |
| Low-Power Mode | 150 à 900 kHz | **290 µA** |
| Standard Performance Mode | 1,1 à 4,0 MHz | 670 µA |
| Powered Down | VDD = 0 V | — |

**290 µA (Low-Power Mode) est quasiment identique aux ~239-275 µA mesurés
sur le rail complet.** Ce n'est pas une coïncidence négligeable — c'est la
seule explication candidate dont l'ordre de grandeur correspond exactement,
sur tout l'historique de test.

### 2.2 Ce qui n'a PAS été vérifié, et qui change la conclusion

Le test archivé a interprété "niveau bas fixe = pas d'horloge = doit
tomber en Sleep Mode" — une hypothèse jamais confrontée à la fiche
technique du micro elle-même. Le **diagramme d'états** de cette fiche
(page 6, lu intégralement pour cet audit) montre :

```
                    Standard Performance Mode
                    (1,1-4,0 MHz)
                      ↗              ↖
                     ↗                ↖
        Low-Power Mode  ←──────────→  Sleep Mode
        (150-900 kHz)                 (≤50 kHz)

        Powered Down Mode (VDD=0V) — état isolé, pas de flèche
        vers/depuis les trois autres états.
```

Sleep Mode et Low-Power Mode sont bien reliés directement l'un à l'autre —
mais les deux ne sont représentés comme atteignables **qu'à partir de
Standard Performance Mode**, c'est-à-dire après que la puce ait vu une
horloge active dans une plage caractérisée. Aucune flèche ne relie
"Powered Down" (mise sous tension, VDD=0→3,3V) directement à Sleep Mode.

**Le firmware actuel (`sample_motion()`, `main.c`) n'active jamais
`configure_pdm_pins_for_system_off()`** — cette fonction existe dans le
code mais n'est jamais appelée (retirée le 2026-08-28 par précaution après
un pic ~200 mA confondu avec deux autres changements simultanés, jamais
réisolée depuis — voir commentaire ligne 289-297 de `main.c`). PDM_CLK
flotte donc actuellement, sans aucune configuration. Le seul test qui a
mis PDM_CLK à un niveau bas fixe (2026-08-27, archive) le faisait **dès la
mise sous tension**, sans jamais faire transiter la puce par Standard
Performance Mode au préalable.

**Hypothèse testable et falsifiable** : depuis la mise sous tension du
rail, le micro ne reçoit jamais d'épisode d'horloge valide (que ce soit
dans le firmware actuel — horloge flottante — ou dans le test archivé —
horloge basse fixe dès le départ), et reste donc dans un état non
caractérisé par la fiche technique (ni Sleep, ni Low-Power, ni Standard —
un état de "power-on" indéterminé), dont le courant mesuré (~239-275 µA)
est cohérent avec un état proche de Low-Power Mode plutôt que du Sleep
Mode réellement visé.

### 2.3bis Résultat de la mesure (2026-09-13, 3 passages PPK2)

**Hypothèse INFIRMÉE par la mesure.** Trois captures PPK2 successives
(`ppk-20260913T175827.csv`, `T181221.csv`, `T182143.csv` — la troisième
avec une ligne de base propre à ~6 µA après correction d'un bug du
firmware de diagnostic lui-même, sans rapport avec la production, voir
§3bis) montrent systématiquement **aucune différence mesurable** entre le
segment 1 (horloge basse fixe dès la mise sous tension, ~223,8-238 µA
selon la session) et le segment 2 (salve de réveil dans la plage
Low-Power/Standard puis repos, même valeur à chaque fois). La séquence de
réveil proposée ne réduit pas le courant.

**Ne pas considérer cette hypothèse comme définitivement close** : la
limite documentée en §2.4 (fréquence d'horloge générée par bit-banging
logiciel, jamais vérifiée à l'oscilloscope) reste entière. Avant
d'abandonner la piste micro, vérifier au scope que la salve de réveil
atteint effectivement 150 kHz-4 MHz sur P1.13 pendant le segment 2.

### 2.3 Test proposé (méthode, déjà exécutée — voir résultat ci-dessus)

Séquence : `regulator_enable(imu_vdd)` → attendre le temps de démarrage
micro documenté (**Power-up Time : typ. 6 ms, max 20 ms**, fiche p.3) →
générer une horloge dans la plage Low-Power (150-900 kHz) ou Standard
(1,1-4 MHz) pendant au moins le **Mode-Change Time documenté (max 10 ms)**
→ ramener l'horloge à un niveau bas fixe (0 Hz, dans la plage Sleep
≤50 kHz) → attendre le **Fall-asleep Time documenté (max 30 µs)** → seulement
alors couper `imu_vdd`.

**Attente précise et falsifiable** : la fiche technique donne le courant
Low-Power Mode à 290 µA typ. et Sleep Mode à 1 µA typ. — un rapport
d'environ 290:1. Si l'hypothèse est correcte, la séquence ci-dessus doit
faire chuter le courant du rail (en isolation, sans trafic I2C vers
l'IMU, comme dans le test de contrôle §8.1 du rapport Nordic) de
~239 µA vers un plancher de quelques µA à quelques dizaines de µA (marge
généreuse pour le courant propre du bloc LDO1, non documenté, et
d'éventuelles tolérances). **Si le courant reste proche de 239 µA malgré
la séquence, l'hypothèse est infirmée** — il faudrait alors vérifier au
oscilloscope que la broche atteint effectivement la fréquence visée
(cette mesure logicielle n'a pas été calibrée avec un instrument externe,
voir limite au §2.4), avant d'abandonner la piste.

Firmware de diagnostic implémenté pour ce test : voir §6.

### 2.4 Limite connue de ce test

La génération d'horloge dans le firmware de diagnostic (§6) utilise un
basculement logiciel de broche GPIO cadencé par `k_busy_wait()`, pas un
périphérique matériel PWM/TIMER avec fréquence garantie. La fréquence
réelle obtenue n'a pas été vérifiée à l'oscilloscope. Elle est calée pour
viser le milieu de la plage Low-Power (150-900 kHz, plage large, facteur
6) avec une marge généreuse contre la variabilité d'exécution logicielle
— mais si le test #1 ne montre aucun effet, **vérifier la fréquence
réelle à l'oscilloscope avant de conclure à l'infirmation de
l'hypothèse** : un résultat négatif pourrait aussi bien signifier
"hypothèse fausse" que "fréquence hors plage visée par manque de
précision logicielle".

---

## 3. Test de contrôle complémentaire — isolement LDO2

**Jamais testé.** Tous les tests d'isolation à ce jour (13+ selon le
rapport Nordic) portent sur `imu_vdd`/LDO1 exclusivement. Aucun n'a testé
si **n'importe quel canal LDO/LOADSW de ce nPM1300, activé sans aucune
charge**, présenterait le même ~250-275 µA — ce qui distinguerait un
défaut intrinsèque au bloc PMIC (chaque activation de LDO coûte ~250 µA,
quel que soit le canal) d'un défaut spécifique au réseau électrique de
LDO1 (le microphone partagé, hypothèse §2).

LDO2 n'apparaît nulle part dans les fichiers de carte Seeed
(`vendor/platform-seeedboards`, vérifié par recherche exhaustive) — il
n'est câblé à aucune charge connue sur ce board. L'activer via
`regulator_enable()` (après déclaration d'un nœud devicetree dédié,
absent de l'overlay actuel) revient à mesurer le courant propre du bloc
LDO du PMIC, sans aucune charge externe.

**Attente précise et falsifiable** : si LDO2 seul (sans charge) affiche un
courant proche de la ligne de base sans aucun rail actif (**~3,3-4,3 µA**,
valeur déjà établie et répétée dans tout l'historique de mesure), cela
confirme que le ~250-275 µA de LDO1 est spécifique à ce qui y est câblé
(le micro, hypothèse §2) — pas un défaut générique du bloc PMIC. Si LDO2
affiche lui aussi ~250-275 µA malgré l'absence de toute charge connue,
cela pointerait vers un coût intrinsèque au bloc LDO/LOADSW du PMIC
lui-même (scénario que le rapport Nordic évoquait comme question ouverte
en dernier recours), indépendant du micro — la piste §2 resterait
pertinente mais ne suffirait plus à elle seule.

Inclus dans le même firmware de diagnostic (§6) pour ne pas multiplier
les cycles flash+mesure.

### 3bis Résultat de la mesure (2026-09-13)

**Hypothèse CONFIRMÉE.** LDO2 seul, sans aucune charge, lit exactement la
même valeur que la ligne de base (~5,9 µA contre ~6,0 µA OFF, dans le
bruit de mesure) — alors que LDO1 (même bloc PMIC, même registres,
réseau électrique différent) ajoute +217 µA dans la même session.
**L'anomalie est donc spécifique au réseau de LDO1**, pas un coût
intrinsèque au bloc LDO/LOADSW du nPM1300. Renforce l'hypothèse micro
(§2) comme cause la plus probable, même si la séquence de correction
testée ne l'a pas confirmée (§2.3bis) — quelque chose de spécifique au
réseau de LDO1 (micro le plus probable candidat vu la correspondance de
courant déjà établie) reste la piste la plus solide, sans solution
logicielle trouvée à ce stade.

---

## 4. Trouvaille indépendante — reconstruction du firmware non reproductible (risque d'ingénierie, pas de consommation)

**Correction (2026-09-22)** : le paragraphe « la cause réelle des 64159
octets de différence sur rebuild reste inconnue » ci-dessous est
**résolu** — voir `Plan-Reduction-Consommation-2026-09-22.md` §1 et
`Configuration-nRF54LM20A-System-ON-IDLE.md` §11. Ce n'était pas un
défaut de `west build` : le commit `ccbe1b5` avait changé
`k_msleep(6)` → `k_msleep(40)` dans `sample_motion()` **après** le
flash de l'image d'or, jamais appliqué à celle-ci. Rebuild `--pristine`
de `c63908d` + H_LACTIVE = image déployée octet pour octet (SHA-256
vérifié). Le reste de cette section (analyse `CONFIG_PM`/`HAS_PM`,
toujours exacte et sans rapport avec ce bug) reste valable.

**Ceci n'a aucun rapport direct avec la consommation, mais c'est un
problème sérieux découvert en vérifiant le code source réel plutôt que la
documentation.** L'en-tête de `xiao_door_sensor/src/main.c` (lignes 1-23)
prévient : depuis le 2026-09-01, `west build` sur ce fichier produit un
binaire qui **mesure ~33 µA au lieu de ~20-22 µA**, avec **64159 octets de
différence sur 117396** par rapport à l'image d'or — plus de la moitié du
binaire diffère. **L'image actuellement déployée sur #01/#02 n'a jamais
été produite par une compilation propre depuis ce fichier source** : le
correctif `H_LACTIVE` a été appliqué par **patch binaire d'un seul octet**
directement sur l'image d'or précédente (`Procédure-Clonage-XIAO-nRF54LM20A.md`
§ « Patcher un binaire existant »), précisément parce que la reconstruction
normale est cassée.

**Vérification indépendante faite pour cet audit** (pas une simple
relecture de la note existante) : la documentation attribue la
reconstruction cassée en partie à `CONFIG_PM` absent du `.config`
généré, mais l'écarte comme cause ("confirmé déjà vrai au moment de
l'image d'or"). J'ai vérifié directement dans le Kconfig de ce snapshot
NCS 3.4.0 (`zephyr/soc/nordic/*/Kconfig`) : **`HAS_PM` n'est sélectionné
que pour les familles nRF92 et nRF54H — jamais pour nRF54L**, donc
`CONFIG_PM=y` est structurellement sans effet sur ce SoC quel que soit le
build. Ceci confirme que l'écartement de cette piste par l'équipe était
correct, mais révèle au passage que **l'explication documentée du gain de
consommation de l'architecture System ON IDLE ("`CONFIG_PM=y` active
l'idle tickless WFI", répétée dans `prj.conf`, `main.c` et la
documentation) est techniquement inexacte pour ce SoC sur ce snapshot** —
`CONFIG_PM` n'a jamais été actif, dans aucun des builds mesurés jusqu'ici,
image d'or comprise. Le vrai mécanisme du gain (confirmé par ailleurs dans
`zephyr/subsys/pm/Kconfig` : `CONFIG_PM_DEVICE`/`CONFIG_PM_DEVICE_RUNTIME`
n'ont **aucune** dépendance à `PM`/`HAS_PM`, contrairement à ce qu'on
pourrait supposer) est probablement l'suppression du reboot périodique
(plus de réinit driver/MFD/BLE à chaque cycle) combiné au comportement
d'idle par défaut de Zephyr (`k_cpu_idle()`, WFI), pas la sélection d'état
de puissance par le sous-système PM. Les gains mesurés (30,85 µA après
pivot d'architecture) restent réels et ne sont pas remis en cause — seule
l'explication du "pourquoi" dans la documentation est corrigée ici.

**La cause réelle des 64159 octets de différence sur rebuild était
`k_msleep(40)` au lieu de `k_msleep(6)` — résolu le 2026-09-22, voir la
note de correction en tête de section.** ~~Ce n'est pas une piste que
cet audit a résolue — c'est un signal d'alarme à traiter : tant que ce
n'est pas compris, aucune modification de code (y compris les tests
proposés dans ce document) ne peut être compilée avec confiance et
comparée directement à l'image d'or sans un contrôle explicite~~ (voir
méthode au §6 : chaque firmware de diagnostic construit pour cet audit
inclut un segment de reproduction de la référence connue — cette
précaution restait de toute façon une bonne pratique, indépendamment de
la cause).

**Recommandation (2026-09-13), suivie et close le 2026-09-22** : isoler
cette régression avec un diff de `.config` complet entre un rebuild
propre et retrouver, si possible, la dernière révision de
`main.c`/`prj.conf`/overlay qui compilait à l'identique de l'image d'or
— fait via `git log`/bissection manuelle, cause trouvée (`ccbe1b5`). Le
déploiement des ~17 unités restantes par clonage (pas par rebuild)
restait la bonne décision tant que ce point n'était pas résolu.

---

## 5. Ce qui est confirmé solide — à ne pas retester

Vérifié indépendamment pour cet audit (pas une reprise de la conclusion
du rapport Nordic) :

- **Workaround errata [38]** : lu directement dans
  `zephyr/drivers/regulator/regulator_npm13xx.c`,
  `regulator_npm13xx_enable()` — le délai de 2 ms + lecture
  `LDSW_OFFSET_STATUS` est bien appliqué par défaut (actif tant que la
  propriété devicetree `nordic,anomaly38-disable-workaround` n'est pas
  positionnée, ce qu'elle n'est pas dans l'overlay du projet). Rien dans
  ce driver n'expose de réglage supplémentaire lié au courant de repos —
  aucune piste manquée côté registres numériques.
- **`H_LACTIVE=1`** (correctif INT1, ~33 µA) : gain réel, confirmé par
  calcul (3,3 V / 100 kΩ ≈ 33 µA, correspond à moins de 1 % à la mesure).
  Ne pas revenir dessus.
- **`PP_OD`** : testé par Nordic, aucun gain au-delà de `H_LACTIVE` seul
  (239,082 vs 239,009 µA) — confirmé dans le bruit de mesure, ne pas
  retester.
- **RAM_POWER_DOWN_LIBRARY, deferred-init IMU/chargeur, `CONFIG_PM_DEVICE_RUNTIME`
  pour le bus TWIM** : ces optimisations restent valides et actives
  (voir §4 pour la nuance sur `CONFIG_PM_DEVICE_RUNTIME` — actif
  indépendamment du `HAS_PM` manquant).
- **Broches CS/SDA/SCL** : comportement confirmé correct par Nordic et
  par le firmware `xiao_imu_pin_diag` (lu pour cet audit) — flottent
  seulement quand `imu_vdd` est actif, pas de fuite via une autre rail.

---

## 6. Firmware de diagnostic implémenté pour cet audit

Nouveau projet `xiao_pdm_ldo_diag/` (même convention que `xiao_imu_pin_diag`),
silencieux (`CONFIG_SERIAL=n`), aucune fonctionnalité de production — un
seul passage séquentiel en boucle, segments longs (15-20 s chacun) pour
une lecture PPK2 sans ambiguïté, même méthode que le rapport Nordic §8.1 :

| Segment | Contenu | Attente |
|---|---|---|
| 0 | `imu_vdd` OFF (ligne de base) | ~3-4 µA (référence connue) |
| 1 | `imu_vdd` ON, `CTRL3_C` écrit (`H_LACTIVE=1`), PDM_CLK bas fixe dès le début — **reproduction exacte du test archivé 2026-08-27** | ~239 µA si la reconstruction n'introduit pas d'écart (voir §4) — sert de contrôle de validité du build |
| 2 | Même état `imu_vdd`, PDM_CLK basculé en salve (~500 kHz visé, voir limite §2.4) puis reposé bas fixe — **séquence de réveil-sommeil proposée §2.3** | Chute significative si hypothèse §2 confirmée (cible : quelques µA à quelques dizaines de µA) |
| 3 | `imu_vdd` OFF | retour à ~3-4 µA (contrôle) |
| 4 | `imu_vdd` OFF, LDO2 seul activé (nœud devicetree ajouté pour ce test) | voir §3 |
| 5 | LDO2 OFF | retour à ~3-4 µA (contrôle) |

Boucle infinie sur ces 6 segments — la mesure PPK2 peut démarrer à
n'importe quel moment, un seul passage complet (~70-90 s) suffit.

Code source : `xiao_pdm_ldo_diag/src/main.c`,
`xiao_pdm_ldo_diag/boards/xiao_nrf54lm20a_nrf54lm20a_cpuapp.overlay`,
`xiao_pdm_ldo_diag/prj.conf`.

### 6bis Deux bugs trouvés et corrigés dans CE firmware de diagnostic (sans rapport avec la production)

Deux itérations ont été nécessaires avant d'obtenir une ligne de base
exploitable, **aucune des deux ne concerne le firmware de production**
(`xiao_door_sensor`), qui n'a jamais eu ces problèmes :

1. `CONFIG_PM_DEVICE`/`CONFIG_PM_DEVICE_RUNTIME` absents du premier
   `prj.conf` du diagnostic — hypothèse testée comme cause du plancher
   élevé (~20 µA au lieu de ~3-4 µA), **infirmée par la mesure**
   (`ppk-20260913T181221.csv` : aucun changement après ajout).
2. La flash SPI externe (`py25q64`, `status="disabled"` par défaut sur ce
   board) n'était jamais activée ni mise en veille profonde par ce
   diagnostic minimal, contrairement à la production qui le fait déjà
   depuis toujours (`suspend_external_flash()`, `main.c`). Une fois
   ajoutée (même code que la production), la ligne de base est passée de
   ~20 µA à **~6 µA** (`ppk-20260913T182143.csv`) — **confirmé comme la
   cause**. Sans conséquence sur la comparaison relative des segments
   (le même biais d'environ +14 µA s'appliquait uniformément à tous les
   segments dans les deux premières mesures, donc les conclusions
   différentielles — §2.3bis, §3bis — étaient déjà valables avant ce
   correctif).

---

## 7. Plan d'action priorisé — état au 2026-09-13 après mesure

**Résultat des tests (3 captures PPK2, voir §2.3bis et §3bis) : aucune
réduction de consommation trouvée à ce stade.** Le ~20-22 µA de
production reste la référence. Ce qui a été gagné est un diagnostic plus
précis, pas une solution :

- **Test LDO2 (§3) : CONFIRMÉ.** L'anomalie est spécifique au réseau de
  LDO1, pas un coût générique du bloc LDO du PMIC.
- **Test séquence de réveil micro (§2) : INFIRMÉ.** La correction
  logicielle proposée ne change rien au courant mesuré.

1. **Avant d'abandonner la piste micro** : vérifier au scope externe si
   la salve de réveil logicielle (bit-banging, P1.13) atteint réellement
   150 kHz-4 MHz pendant le segment 2 — limite documentée §2.4, jamais
   levée. Si la fréquence est confirmée correcte et que l'infirmation
   tient, la piste micro devient caduque par manque de mécanisme
   d'action, malgré la correspondance de courant toujours non expliquée
   autrement.
2. **Ne pas porter la séquence de réveil dans `sample_motion()`
   (production)** — elle n'a montré aucun effet, l'ajouter ne ferait
   qu'ajouter de la complexité et de la latence (~20 ms) sans bénéfice
   mesuré.
3. **Ne pas reconsidérer l'architecture par interruption matérielle**
   (IMU laissé alimenté en continu) tant que le coût du rail LDO1 reste
   à ~220-240 µA en continu — largement supérieur au coût actuel du
   sondage dupliqué à 1 Hz (~18-20 µA de moyenne). Cette option reste
   fermée tant qu'aucune réduction du plancher LDO1 n'est trouvée.
4. **En parallèle, indépendamment de la piste consommation** : traiter la
   régression de reconstruction (§4) comme un chantier séparé avant toute
   nouvelle évolution de firmware significative — c'est un risque
   d'ingénierie qui dépasse la seule question de consommation.
5. ~~Mettre à jour `Configuration-nRF54LM20A-System-ON-IDLE.md` §3.3 (délai
   ODR encore documenté à 6 ms, alors que le code déployé utilise 40 ms
   depuis le correctif du 2026-08-30 — écart de documentation trouvé
   pendant cet audit, sans conséquence sur la consommation mesurée
   puisque les images d'or datent d'après ce correctif, mais source de
   confusion pour la suite).~~ **Erroné (correction 2026-09-22, voir
   `Plan-Reduction-Consommation-2026-09-22.md` §1.1)** : c'est l'inverse.
   Les images d'or (dump du 2026-08-30 04:46 et suivants) ont capturé la
   version **6 ms** (`c63908d`, flashée 18:01) ; le commit `ccbe1b5`
   (18:55) a introduit `k_msleep(40)` dans le **source** après ce flash,
   sans jamais être recompilé dans une image d'or. La documentation
   6 ms (`Configuration...md` §3.3) était donc déjà correcte pour
   l'image déployée — c'est le `main.c` du 2026-09-13 qui divergeait
   silencieusement, et cette divergence de 34 ms est la cause principale
   de l'anomalie ~30-38 µA de la section §4 ci-dessus, pas un détail
   sans conséquence.

---

## 8. Ce qu'il ne faut pas retenter

- Pull-down GPIO sur INT1 combiné à une reconfiguration NFC — a produit un
  pic ~200 mA inexpliqué (2026-08-28), jamais isolé proprement. Ne pas
  reproduire cette combinaison ; le test §6 segment 2 ne touche que
  PDM_CLK, isolément.
- Désactivation de `charging-enable` comme méthode de réduction de
  consommation — mesuré pire (~32 µA), infirmé par la mesure (§9 de
  `Configuration-nRF54LM20A-System-ON-IDLE.md`).
- Mode Load Switch au lieu de LDO pour `imu_vdd` — mesuré pire (275,67 µA
  contre 253 µA).
