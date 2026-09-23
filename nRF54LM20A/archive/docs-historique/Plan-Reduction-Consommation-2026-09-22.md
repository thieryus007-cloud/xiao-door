# Plan de réduction de consommation — XIAO nRF54LM20A (`xiao_door_sensor`)

**Date :** 2026-09-22 — analyse Claude Opus 5.5, à la demande de l'utilisateur
(« analyse approfondie, toutes les pistes, sans a priori sur le travail déjà
réalisé »). **Destinataire : Claude Sonnet 5, qui met en œuvre ce plan.**

**Objectif :** passer de ~22 µA à ~13-15 µA (−30 à −40 %) sans toucher à la
réactivité (~1 s) ni au matériel.

**Lire d'abord :** `C:\ncs\CLAUDE.md` (règles absolues : S/N SWD avant tout
flash, jamais #01, attente falsifiable avant chaque test, pas de firmware de
diagnostic laissé flashé, PPK2 et USB jamais simultanés). Ce plan ne les
remplace pas, il s'y ajoute. Rappels spécifiques en §4.

---

## 0. Résumé

1. **Le « build non reproductible » n'existe pas (prouvé, §1).** Recompiler
   le commit `c63908d` redonne `unit01-verified-2026-08-30.bin` **octet pour
   octet** ; `c63908d` + le bit `H_LACTIVE` redonne
   `unit02-verified-2026-09-01-H_LACTIVE.bin` (image déployée sur 04-13)
   **octet pour octet**. Les rebuilds « à 33 µA » venaient d'un source
   différent : `k_msleep(40)` au lieu de `k_msleep(6)` dans `sample_motion()`
   (+34 ms de rail IMU allumé par cycle ≈ +11 µA). Le ticket Nordic repose
   sur une prémisse fausse ; plusieurs conclusions de test en découlent (§1.3).
2. **Où part l'énergie (mesuré, §2) :** régime établi 21,8 µA = plancher
   3,51 µA + **18,6 µC par cycle** de sondage / 1,017 s. Le courant statique
   du rail LDO1 (~250 µA) ne pèse que **~13 %** du cycle. Les postes
   dominants sont le **temps CPU actif** — surtout **3 transactions I2C
   bit-bang vers le nPM1300 par cycle (~36 %)** — puis la **recharge des
   condensateurs du rail** (C18 4,7 µF + C50 4,7 µF, ~19 %) et les
   **6 transactions I2C vers l'IMU** (~19 %).
3. **Plan (§5-§9), une variable par mesure :**

   | Étape | Changement | Prévision régime établi |
   |---|---|---|
   | Réf. | Image d'or H_LACTIVE (source réaligné, §5) | ~21,6 µA (18,4 µC/cycle) |
   | A | Bus I2C du PMIC 100 → 400 kHz (overlay seul) | ~17,5 µA (−20 %) |
   | B | IMU : 6 → 3 transactions I2C (écritures groupées, lecture brute) | ~16,2 µA (−26 %) |
   | C | LDO1 commandé par broche (P1.25 → GPIO0 nPM1300) + 1 lecture « errata » | **~13,7 µA (−37 %)** |
   | D | Accéléromètre haute performance 833 Hz, attente 6 → ~1,5 ms | **~12,5 µA (−43 %)** |
   | E, F | Options : attente de démarrage IMU réduite, LDO1 à 3,0 V | ~11,5-12 µA |

   Chemin de repli si C échoue (errata nPM1300) : A+B+D ≈ 14,9 µA (−32 %).
   Toutes ces valeurs sont des **prévisions** à confirmer au PPK2, avec
   critère de falsification donné à chaque étape.

---

## 1. Découverte préalable — reproductibilité du build (bloquant, résolu)

### 1.1 Preuves (vérifiées le 2026-09-22, sans flasher aucune carte)

- Désassemblage de `main()` dans les deux binaires du dossier
  `Nordic-Support-Ticket-2026-09-19/` : l'image d'or appelle
  `k_msleep.isra.0` avec `movs r0, #6` (adresse `0xb5ba`), le rebuild
  « 33 µA » avec `movs r0, #40` (`0xb5c2`). Les deux `main()` diffèrent dès
  leurs premières instructions (cadre de pile 244 vs 252 octets) : ce n'est
  pas une divergence « à partir de la ligne `angle_crossed` », c'est un
  source différent (logique de confirmation sur 2 cycles ajoutée en
  `1994112`).
- Rebuild du commit `c63908d` (toolchain `dcbdc366a1`, NCS 3.4.0, même
  commande `west build`) :
  `sha256 = 75b92e95d69396bf4715371d566a972087b59262ab7f31ca51aa2060a0703e78`
  = `golden-image/unit01-verified-2026-08-30.bin`.
- Même source + `H_LACTIVE` dans l'écriture de `CTRL3_C` :
  `sha256 = 0da087ae45d087afdc334828e95a8c44283b1182b1cce5dc1525d7133853f778`
  = `golden-image/unit02-verified-2026-09-01-H_LACTIVE.bin`.
- Historique : `c63908d` (2026-08-29 18:01, `k_msleep(6)`) a été flashé sur
  #01 ; `ccbe1b5` (18:55) a introduit `k_msleep(40)` dans le source **après**
  ce flash ; le dump d'or du 08-30 a donc capturé la version 6 ms. Le commit
  `1994112` (« main.c en phase avec l'image d'or ») et l'audit du 09-13 §7.5
  (« les images d'or datent d'après ce correctif ») sont erronés sur ce point.
- Modèle : +34 ms × (rail ~250 µA + accéléromètre 85 µA) ≈ +11 µC/cycle
  ≈ +11 µA → 21,8 + 11 ≈ 33 µA : correspond aux ~30-33 µA mesurés.

### 1.2 Conséquences

- `west build` est déterministe : on peut recompiler pour la production,
  **à condition** de contrôler le SHA-256 quand le source est censé être
  inchangé, et de mesurer au PPK2 quand il change (règle §5 existante).
- Le patch binaire d'un octet n'est plus nécessaire.

### 1.3 Conclusions antérieures à réviser (toutes construites avec `k_msleep(40)`)

| Document / test | Conclusion écrite | Statut |
|---|---|---|
| `Configuration…md` §11, ticket Nordic 2026-09-19 | toolchain non déterministe | **Faux** — source différent |
| `Configuration…md` §9 (`xiao_no_charge_test/`, 32 µA) | « désactiver la charge augmente la consommation » | **Non établi** — ce firmware contient `k_msleep(40)` (vérifié) ; +10 µA expliqués par le délai. À re-tester si l'option pile non rechargeable revient. |
| Plan de résilience 2026-09-19 (35-38 µA) | rejeté pour régression | La base (HEAD, 40 ms) explique ~+11 µA ; le plan lui-même n'a jamais été mesuré sur la bonne base. |
| `Configuration…md` §3.3 (« attente 6 ms ») | — | **Correct** pour l'image déployée. |
| En-tête de `main.c` (« NE PAS RECOMPILER ») | — | À réécrire (§5). |

---

## 2. Où part l'énergie aujourd'hui (mesures existantes, ré-analysées)

### 2.1 Référence

Capture `mesures #01/#01-30s-ppk-20260830T044631.csv` (image d'or sans
H_LACTIVE, #01, 70 s, 100 kHz), analysée avec `tools/ppk_cycle_stats.py` :

| Grandeur | Valeur |
|---|---|
| Moyenne globale (inclut les trames du démarrage) | 23,78 µA |
| Plancher entre salves | 3,51 µA |
| Salves de sondage | 67 ; **moyenne 18,62 µC**, médiane 18,13 µC ; durée 17,2 ms |
| Période | 1016,7 ms |
| **Régime établi** | **21,83 µA** (= 3,51 + 18,62/1,0167) |

Avec H_LACTIVE (image déployée) : −~0,25 µC/cycle (INT1 ne tire plus 33 µA
entre l'écriture de `CTRL3_C` et la coupure) → ~21,6 µA attendus.

### 2.2 Anatomie d'une salve (profil 50 µs, `tools/ppk_profile.py`, salve à t = 2,5205 s, 17,05 µC)

| Phase (code de `c63908d`) | Durée | Charge | Ce qui consomme |
|---|---|---|---|
| Réveil + `regulator_enable()` → écriture bit-bang `TASKLDSW1SET` | 0,95 ms | 2,9 µC | CPU 128 MHz en attente active + pull-ups 4,7 kΩ du bus PMIC (~4,4 mA côté batterie) |
| `k_msleep(2)` du contournement errata [38] | 1,75 ms | 0,35 µC | rail en montée lente (errata) ; l'appel de charge principal n'apparaît qu'à la lecture suivante |
| Réveil + lecture bit-bang `LDSWSTATUS` + démarrage réel du LDO | 0,9 ms | 3,9 µC | lecture ~2,4 µC + appel de charge du rail |
| Fin de charge du rail (sommeil) | 1,35 ms | 1,2 µC | charge de C18/C50/C19/C49/C51 |
| Reste de `k_msleep(5)`, rail allumé | 3,45 ms | 0,75 µC | ~250 µA statiques |
| Réveil + 5 transactions TWIM de configuration IMU | 1,45 ms | 2,3 µC | CPU ~1,9 mA ; `CTRL3_C`, `CTRL6_C` (lect.+écr.), ODR (lect.+écr.) |
| `k_msleep(6)`, rail + accéléromètre 208 Hz | 5,5 ms | 1,9 µC | ~250 + 85 µA |
| Réveil + lecture XYZ + calculs | 0,65 ms | 1,2 µC | |
| Écriture bit-bang `TASKLDSW1CLR` + fin | 0,9 ms | 2,5 µC | |

La moyenne sur 67 salves (18,6 µC) dépasse cette salve de ~1,5 µC : les pics
d'appel de charge du rail (>100 mA, très brefs) ne sont échantillonnés
qu'aléatoirement à 100 kHz — l'écart leur revient.

### 2.3 Regroupement par cause (±1 µC par poste)

| Poste | µC/cycle | Part |
|---|---|---|
| I2C bit-bang PMIC, 3 transactions à 100 kHz | ~6,5-7 | ~36 % |
| Appel de charge du rail `IMU&MIC_3V3` | ~3-4 | ~19 % |
| IMU : 6 transactions TWIM + calcul | ~3,5 | ~19 % |
| Courant statique du rail (~250 µA hors phases CPU) | ~2,5 | ~13 % |
| Réveils CPU (4 par cycle), part non comptée ci-dessus | ~1-1,5 | ~7 % |
| Courant propre de l'accéléromètre (85 µA × 6 ms) | ~0,5 | ~3 % |

### 2.4 Recoupements indépendants (tous cohérents)

- Test #11 (rapport Nordic §8) : une lecture `LDSWSTATUS` de plus → +2,24 µC.
  **Une transaction PMIC bit-bang à 100 kHz ≈ 2,2 µC.**
- Test #32 (attente rail 20 → 5 ms) : −3,78 µA = 15 ms × ~252 µA →
  **chaque ms de rail allumé ≈ 0,25 µC.**
- Test #39 (1000 → 1500 ms) : modèle charge fixe par cycle ≈ 17 µC.
- Capture `ppk-20260919T085215-test1.csv` : un réveil « vide » (boucle
  `k_sleep` seule) coûte **~0,5 µC net** (pic ~0,9 µC puis déficit de
  récupération du BUCK2).
- Transaction TWIM IMU (400 kHz, matériel) : ~0,45 µC, dominée par le
  logiciel (driver + PM runtime), pas par le bus.

### 2.5 Faits matériels vérifiés sur le schéma Seeed (V1.0, 2026-04-09)

Source : `files.seeedstudio.com/wiki/XIAO_nRF54LM20A/getting_start/RES/XIAO_nRF54LM20A_Schematic.pdf`.

- SoC alimenté en **3,3 V par BUCK2** (VOUT2, R10 = 470 kΩ) ; VOUT1 inutilisé.
- **GPIO0 du nPM1300 ↔ P1.25**, **GPIO1 ↔ P1.26** (étiquettes `npm_GPIO0/1`).
  Jamais utilisées par le firmware ni le board Seeed.
- LDO1 alimenté par **VSYS** (R14 0 Ω ; R11 non monté) → condition de
  l'errata [38] remplie.
- Rail `IMU&MIC_3V3` : C18 4,7 µF + C19 100 nF, puis FB3 → IMU (C49 100 nF,
  pull-ups R28/R29 4,7 kΩ SDA/SCL, R37 100 kΩ CS, R38 100 kΩ INT1) et
  FB4 → micro (C50 4,7 µF, C51 100 nF).
- **Le micro est un MSM261DGT006** (U12), pas le MSM261D3526H1CPM étudié
  jusqu'ici. Modèle frère MSM261DGT003 : sommeil ≤ 50 kHz = 1 µA, mise sous
  tension 6 ms typ. / 20 ms max.
- IMU : SDO/SA0, SDx, SCx à la masse ; INT2 non connecté.
- Bus PMIC P1.17/P1.18, pull-ups R12/R13 4,7 kΩ vers VSYS_3V3. nPM1300 TWI
  spécifié de 100 kHz à 1 MHz (PS v1.1 §7.6).
- Flash : pull-ups R42/R43/R44 sur CS, WP, HOLD, tous pilotés à l'état haut
  par `configure_spi_pins_for_system_off()` → pas de fuite. SAMD11 alimenté
  seulement en présence de VBUS (U3). LED rouge D2 sur LED1 du nPM1300
  (indicateur de charge) → éteinte sans VBUS.

---

## 3. Pistes examinées (toutes, sans a priori)

### 3.1 Retenues

| Id | Piste | Gain estimé (µC/cycle) | Risque |
|---|---|---|---|
| A | Bus PMIC bit-bang à 400 kHz | −3,5 à −5 | faible |
| B | IMU : écritures groupées + lecture brute (6 → 3 transactions) | −1,0 à −1,8 | faible |
| C | LDO1 par broche P1.25 → GPIO0 nPM1300 + une seule lecture « errata » | −2,0 à −3,0 | moyen (errata [38]) |
| D | Accéléromètre HP 833 Hz, lecture du 1er échantillon (~1,5 ms au lieu de 6) | −1,0 à −1,6 | faible-moyen |
| E | Attente de démarrage IMU 5 → ~3 ms (après validation) | −0,4 à −0,6 | moyen |
| F | LDO1 3,3 → 3,0 V | −0,3 à −0,8 | faible-moyen |

### 3.2 Écartées (avec raison)

| Piste | Raison |
|---|---|
| Allonger la période de sondage | Contrainte produit (~1 s). Pour mémoire : 1,5 s = −26 % (test #39). |
| Rail toujours allumé + réveil matériel IMU | Rail ≥ 218-250 µA tant que sa cause n'est pas supprimée. |
| TWIM matériel pour le PMIC | Après C il ne reste qu'une transaction PMIC par cycle ; gain résiduel < 0,5 µC pour un recâblage de broches incertain. |
| Fréquence CPU, RRAM en veille pendant la salve | Gains < 0,3 µC, non démontrables proprement. |
| Plancher 3,51 µA | Tous les chemins DC vérifiés sur schéma ; reste = SoC idle + nPM1300 + flash (niveau compatible avec sa veille profonde). Rien d'actionnable identifié. |
| Trames BLE (7,3 µC par événement d'advertising, `MPSL_HFCLK_LATENCY=1650`) | < 0,2 µA au repos ; hors cible. |
| Retirer FB4 (isoler le micro) | Modification matérielle refusée à ce stade — option future si la piste R1 confirme le micro. |
| Passer le SoC (VOUT2) à 1,8 V | Impact système (flash, niveaux, LEDs) sans gain démontré. |

### 3.3 Piste de recherche R1 (optionnelle, hors chemin critique)

Origine des ~220 µA statiques du rail (le micro est le premier suspect, mais
les analyses précédentes utilisaient la fiche d'un autre modèle). Test non
destructif proposé : générer l'horloge avec le **périphérique PDM matériel**
(`pdm20`, P1.13, 1,024-2,4 MHz exacts, ~30 ms) puis l'arrêter, rail maintenu
allumé, comme test1 du 09-19. Attendu si le micro est en cause : chute vers
≤ 20 µA. Même confirmée, l'architecture « rail toujours allumé + réveil IMU »
ne ferait pas mieux que ce plan (IMU 12,5 Hz ≈ 9 µA + plancher ≈ 13 µA) ;
intérêt surtout diagnostique (clore la question Nordic).

---

## 4. Règles de conduite pour l'implémentation

1. **Unité de test : #02 uniquement (S/N pont SWD `9C4A557D`). #01
   (`C5F0E209`) n'est jamais touchée.** Avant chaque flash : commande
   OpenOCD en lecture seule, lire `CMSIS-DAP: Serial# = …`, comparer
   explicitement, l'écrire à l'utilisateur, puis flasher avec
   `-c "adapter serial 9C4A557D"`. Les scripts `deploy-scripts/` refusent
   #02 par construction (déploiement de lot) : ne pas les détourner, utiliser
   la commande §5 de `Configuration…md` + `adapter serial`.
2. **Tout flash est une action matérielle : annoncer (unité, S/N, fichier,
   SHA-256) et attendre l'accord explicite de l'utilisateur.** Chercher,
   compiler, analyser : sans autorisation.
3. **Avant chaque mesure :** `CONFIG_SERIAL=n`, `CONFIG_CONSOLE=n`,
   `CONFIG_UART_CONSOLE=n`, `CONFIG_PRINTK=n`, pas de `CONFIG_LOG=y`, vérifiés
   dans `build/xiao_door_sensor/zephyr/.config` (pas seulement `prj.conf`).
4. **Avant chaque test : valeur attendue, raisonnement, et ce que le résultat
   contraire prouverait** (les prévisions de ce plan servent de point de
   départ).
5. **Une seule variable par mesure.** Métriques : charge moyenne par cycle
   (µC) et régime établi, avec `python tools/ppk_cycle_stats.py <csv>`.
   Capture ≥ 70 s à 100 kHz, export CSV, même tension de source PPK2 que la
   référence (la noter). Pour les écarts attendus < 1,5 µA (E, F), mesurer
   référence et candidat dans la même session.
6. **Firmware de diagnostic** (caractérisations C/D/E) : dossier séparé,
   console autorisée (aucune mesure PPK2 dessus), **reflasher le candidat
   courant immédiatement après**, noter la restauration.
7. Protocole PPK2 inchangé (USB et PPK2 jamais simultanés ; débrancher et
   rebrancher l'USB après flash avant tout test réel).
8. Après chaque étape validée : commit (code + mesure dans le message) et
   mise à jour de `Configuration-nRF54LM20A-System-ON-IDLE.md` §1/§3 et des
   notes de projet.
9. **Construire dans `xiao_door_sensor/build`** (chemin court) : un
   répertoire de build à chemin long échoue sous Windows (limite 260
   caractères, constaté le 2026-09-22 : erreur `ar` sur
   `libcracen_psa_driver.a`).

---

## 5. Phase 0 — Réaligner le source sur l'image déployée (aucun flash requis)

**But :** que `main.c` compile exactement l'image `unit02-verified-2026-09-01-H_LACTIVE`.

1. **Sauvegarder le travail en cours non commité** (plan de résilience) sans
   toucher aux autres fichiers modifiés (docs, logs) : copier
   `xiao_door_sensor/src/main.c`, `prj.conf` et l'overlay vers
   `archive/xiao_door_sensor-logs-et-backups/reference/` sous les noms
   `main_resilience-wip_2026-09-19.c.bak`,
   `prj_resilience-wip_2026-09-19.conf.bak`,
   `overlay_resilience-wip_2026-09-19.overlay.bak`.
2. **Restaurer les trois fichiers depuis `c63908d`** (depuis `C:\ncs\projects`) :
   ```bash
   git checkout c63908d -- nRF54LM20A/xiao_door_sensor/src/main.c \
     nRF54LM20A/xiao_door_sensor/prj.conf \
     nRF54LM20A/xiao_door_sensor/boards/xiao_nrf54lm20a_nrf54lm20a_cpuapp.overlay
   ```
   (`prj.conf` et l'overlay de `c63908d` sont identiques à HEAD.)
3. **Appliquer H_LACTIVE** dans `main.c` :
   ```c
   #define LSM6DSL_CTRL3_C_BDU        BIT(6)
   #define LSM6DSL_CTRL3_C_H_LACTIVE  BIT(5)
   ...
   rc = i2c_reg_write_byte_dt(&imu_i2c, LSM6DSL_REG_CTRL3_C,
                              LSM6DSL_CTRL3_C_BDU | LSM6DSL_CTRL3_C_H_LACTIVE |
                              LSM6DSL_CTRL3_C_IF_INC);
   ```
   Réécrire l'en-tête du fichier : retirer l'avertissement « NE PAS
   RECOMPILER », expliquer §1 en quelques lignes, citer le SHA-256 attendu.
   Ajouter des commentaires ne change pas le binaire (vérifié le 2026-09-22 :
   `#define` ajouté + 6 lignes de commentaire en tête → binaire identique).
4. **Compiler** (commande §5 de `Configuration…md`, avec `--pristine`) puis :
   ```bash
   sha256sum build/xiao_door_sensor/zephyr/zephyr.bin
   # attendu : 0da087ae45d087afdc334828e95a8c44283b1182b1cce5dc1525d7133853f778
   ```
   Si différent : **ne pas continuer**. Comparer `main()` avec
   `arm-zephyr-eabi-objdump` (méthode §1.1) avant toute autre hypothèse.
5. **Commit** « source réaligné sur l'image déployée, rebuild identique ».
6. **Corriger la documentation** (liste §1.3) : en-tête `main.c`,
   `Configuration…md` §9 et §11, `Suivi-Nordic-Reproductibilite-Build-2026-09-19.md`
   (résolution), notes de projet item 8, note de correction en tête de
   `Audit-Consommation-2026-09-13.md` (§4 et §7.5),
   `Procedure-Clonage…md` (patch binaire obsolète). Préparer pour
   l'utilisateur le message de clôture Nordic (§11) — **c'est l'utilisateur
   qui l'envoie.**
7. **Mesure de référence de la campagne** : #02 tourne actuellement
   `unit01-verified-2026-08-30.bin` (sans H_LACTIVE, restaurée le 09-19).
   Flasher sur #02 le binaire de l'étape 4 (identique à l'image H_LACTIVE
   déployée sur 04-13), après accord et contrôle du S/N, puis PPK2.
   **Attendu : 18,1-18,6 µC/cycle, régime établi 21,3-21,9 µA, plancher
   3,4-3,7 µA.** Hors de ces bornes : trouver pourquoi avant l'étape A
   (session PPK2, tension source, firmware réellement flashé).

---

## 6. Étape A — Bus I2C du PMIC à 400 kHz (overlay seul)

**Changement** (overlay du projet) :
```dts
&pmic_i2c {
	clock-frequency = <400000>;   /* I2C_BITRATE_FAST */
};
```
Vérifier dans `build/xiao_door_sensor/zephyr/zephyr.dts` que le nœud
`pmic-i2c` porte `clock-frequency = < 0x61a80 >`.

**Raisonnement :** `i2c_bitbang.c` attend `T_LOW`/`T_HIGH` en cycles du
compteur GRTC (1 MHz) : 5+5 µs par bit à 100 kHz, 2+1 µs à 400 kHz. Mesuré
aujourd'hui ≈ 11-12 µs par bit (0,45 ms pour l'écriture de 4 octets), donc
~1,5-3 µs de surcoût logiciel par bit → à 400 kHz ≈ 3,5-5 µs par bit, soit
**2,3 à 3,5× plus court.** Le nPM1300 accepte jusqu'à 1 MHz ; 4,7 kΩ
conviennent pour 400 kHz.

**Attendu :** chaque transaction PMIC 0,45-0,65 ms → 0,15-0,25 ms ;
**−3,5 à −5 µC/cycle → 13,4-14,9 µC ; régime établi ~16,7-18,2 µA.**

**Falsification :** gain < 2 µC → profiler une salve (`ppk_profile.py`,
50 µs) : la fenêtre bit-bang du début de salve (aujourd'hui ~0,45 ms à
~4,4 mA) doit passer sous ~0,25 ms. Si elle n'a pas raccourci : propriété
non appliquée (vérifier `zephyr.dts`) ou surcoût `k_cycle_get_32()`/API GPIO
dominant — l'étape C devient alors le levier principal. Gain > 6 µC : le
modèle sous-estime le bit-bang, rien d'inquiétant.

**Validation fonctionnelle :** mouvement réel → trames A/C dans HA ; trame B
(batterie, tension plausible) — forcer une trame santé au démarrage ou
attendre 15 min ; aucune période de silence (échec `sample_motion()` ⇒
aucune trame).

---

## 7. Étape B — IMU : 6 → 3 transactions I2C

**Aujourd'hui :** `CTRL3_C` (écr.), `CTRL6_C` (lect.+écr.), ODR via
`sensor_attr_set()` (lect.+écr.), lecture XYZ via l'API capteur.

**Changement dans `sample_motion()`** (garder `device_init(imu_dev)` au
premier cycle, **avant** ces écritures, pour que l'init du driver ne les
écrase pas) :
```c
#define LSM6DSL_REG_CTRL1_XL   0x10
#define LSM6DSL_REG_OUTX_L_XL  0x28
#define CTRL1_XL_208HZ_2G      0x50   /* ODR_XL=0101, FS=+/-2 g */
#define ACCEL_MS2_PER_LSB      (61e-6f * 9.80665f)  /* 0,061 mg/LSB a +/-2 g */

/* Une seule ecriture, auto-increment (IF_INC=1 des le reset) :
 * CTRL3_C, CTRL4_C, CTRL5_C, CTRL6_C. */
uint8_t ctrl3_6[5] = {
	LSM6DSL_REG_CTRL3_C,
	LSM6DSL_CTRL3_C_BDU | LSM6DSL_CTRL3_C_H_LACTIVE | LSM6DSL_CTRL3_C_IF_INC, /* 0x64 */
	0x00,                       /* CTRL4_C : reset */
	0x00,                       /* CTRL5_C : reset */
	LSM6DSL_CTRL6_C_XL_HM_MODE, /* 0x10 : mode normal (comme aujourd'hui) */
};
rc = i2c_write_dt(&imu_i2c, ctrl3_6, sizeof(ctrl3_6));
/* Puis l'ODR, APRES XL_HM_MODE (pas de passage transitoire en HP). */
rc = i2c_reg_write_byte_dt(&imu_i2c, LSM6DSL_REG_CTRL1_XL, CTRL1_XL_208HZ_2G);
k_msleep(6);
uint8_t raw[6];
rc = i2c_burst_read_dt(&imu_i2c, LSM6DSL_REG_OUTX_L_XL, raw, sizeof(raw));
out->accel.ax = (int16_t)sys_get_le16(&raw[0]) * ACCEL_MS2_PER_LSB;
out->accel.ay = (int16_t)sys_get_le16(&raw[2]) * ACCEL_MS2_PER_LSB;
out->accel.az = (int16_t)sys_get_le16(&raw[4]) * ACCEL_MS2_PER_LSB;
```
Conserver tous les chemins d'erreur existants (coupure du rail avant
`return`). Chemins gyroscope (événements) et température (santé) :
inchangés à cette étape.

**Attendu :** −3 transactions × ~0,45 µC + suppression de la couche API
capteur → **−1,0 à −1,8 µC/cycle ; régime établi ~15,3-16,9 µA.**

**Falsification :** gain < 0,6 µC → le coût TWIM n'est pas proportionnel au
nombre de transactions (coût fixe PM runtime / réveil) : profiler la fenêtre
de configuration (aujourd'hui ~1,45 ms à ~1,9 mA ; attendu ~0,6 ms).

**Validation fonctionnelle (build console, jamais mesuré au PPK2) :** afficher
X/Y/Z et pitch/roll sur 30 cycles au repos et en inclinant, comparer au
firmware de référence dans la même position (écart ≤ 0,05 m/s² par axe).

---

## 8. Étape C — LDO1 commandé par broche (P1.25 → GPIO0 du nPM1300)

### 8.1 Pourquoi c'est possible

- nPM1300 PS v1.1 §6.4 : les LOADSW/LDO se commandent par registre **ou par
  GPIO** ; en commande GPIO, **fronts** : bas→haut = marche, haut→bas =
  arrêt. GPIO0 au reset : entrée, pull-down 500 kΩ, anti-rebond désactivé
  (registres `GPIOMODE`, `GPIOPDEN`, `GPIODEBOUNCE`) → seul `LDSW1GPISEL`
  (= 1) est à écrire, une fois au démarrage.
- Zephyr le supporte : `dvs-gpios` sur le nœud parent `regulators` +
  `enable-gpio-config` sur LDO1 ; `regulator_npm13xx_init()` écrit
  `LDSW1GPISEL`, `regulator_parent_dvs_state_set()` pilote la broche.
- Plus aucune écriture I2C `TASKLDSW1SET`/`CLR` par cycle, plus de
  `k_msleep(2)` ni du réveil qui le suit.

### 8.2 La contrainte : errata [38]

Texte du binding Zephyr (`nordic,npm13xx-regulator-common.yaml`) : « When
nPM13xx is in ULP mode, LDO is supplied from VSYS and then LDO is enabled, it
can take long time until the LDO output has reached its target voltage. To
avoid this, an i2c read is performed shortly after an LDO is enabled. »
LDO1 est alimenté par VSYS (R14) → **s'applique.** Le profil de l'image d'or
va dans ce sens : l'essentiel de l'appel de charge du rail apparaît au moment
de la lecture `LDSWSTATUS`, pas à l'écriture d'activation. La commande par
broche ne déclenche pas ce contournement : il
faut garder **une** lecture I2C par cycle (à 400 kHz après l'étape A :
~0,7-0,9 µC au lieu de ~2,4).

### 8.3 Caractérisation préalable (firmware de diagnostic `xiao_ldo_gpio_char/`)

Mesurer, 20 fois par variante, le temps de montée du rail `t_up` : du front
au moment où **SDA (P0.08) lit « 1 »**. R28 (4,7 kΩ) tire SDA vers le rail
IMU, donc SDA = 1 signifie rail > VIH SoC (2,31 V). Configurer P0.08 en
entrée sans pull pendant la mesure (TWIM30 suspendu) ; horodatage
`k_cycle_get_32()` (GRTC 1 MHz), attente active (on ne mesure pas le courant
ici). Variantes, commande par broche configurée à l'exécution
(`mfd_npm13xx_reg_write(pmic, 0x08, 0x05, 1)` pour `LDSW1GPISEL`,
`gpio_pin_set` sur P1.25) :

| Var. | Séquence | Attendu |
|---|---|---|
| V0 | `regulator_enable()` actuel (référence) | ≥ 2 ms (attente errata incluse) |
| V1 | front P1.25 seul, aucun I2C | lent (≫ 2 ms) si l'errata s'applique |
| V2 | front + lecture `LDSWSTATUS` immédiate | ~0,7-1,5 ms |
| V3 | front + 1 ms + lecture | ~1-2,5 ms |
| V4 | front + adressage seul (écriture de 0 octet à 0x6B) | à découvrir (kick moins cher si ça suffit) |

Après chaque montée : `t_up + 5 ms` → lecture `WHO_AM_I` (0x0F = 0x6A),
puis rail coupé (front descendant ou `regulator_disable()`), 1 s d'attente
(décharge). Revenir à `LDSW1GPISEL = 0` avant V0.

**Décision :** retenir la variante la moins coûteuse avec `t_up ≤ 2 ms` sur
20/20 et `WHO_AM_I` correct. Si V1 est déjà rapide : pas de lecture du tout
(−0,8 µC de plus). Si aucune variante n'atteint ≤ 3 ms de façon fiable :
**abandonner C** (garder A+B, passer à D) et le documenter.

### 8.4 Changement de production

Overlay :
```dts
&pmic {
	regulators {
		dvs-gpios = <&gpio1 25 GPIO_ACTIVE_HIGH>;
		imu_vdd: LDO1 {
			regulator-min-microvolt = <3300000>;
			regulator-max-microvolt = <3300000>;
			enable-gpio-config = <0 GPIO_ACTIVE_HIGH>;   /* GPIO0 du nPM1300 */
		};
	};
};
```
`main.c` :
```c
#include <zephyr/drivers/mfd/npm13xx.h>

static const struct device *const pmic_dev = DEVICE_DT_GET(DT_NODELABEL(pmic));
static const struct device *const pmic_regs = DEVICE_DT_GET(DT_PARENT(DT_NODELABEL(imu_vdd)));

#define NPM13XX_LDSW_BASE      0x08
#define NPM13XX_LDSW_TASK1CLR  0x01
#define NPM13XX_LDSW_STATUS    0x04

static int imu_rail_on(void)
{
	uint8_t st;
	int rc = regulator_parent_dvs_state_set(pmic_regs, 1);   /* front montant P1.25 */

	if (rc < 0) {
		return rc;
	}
	/* Errata nPM1300 [38] : une lecture TWI juste apres l'activation
	 * (variante retenue en 8.3). */
	return mfd_npm13xx_reg_read(pmic_dev, NPM13XX_LDSW_BASE, NPM13XX_LDSW_STATUS, &st);
}

static void imu_rail_off(void)
{
	(void)regulator_parent_dvs_state_set(pmic_regs, 0);      /* front descendant */
}
```
- Remplacer **tous** les `regulator_enable/disable(imu_vdd_dev)` de
  `sample_motion()` (chemins d'erreur compris) ; ne plus jamais appeler
  `regulator_enable()` sur `imu_vdd`.
- Au démarrage (une fois, dans `main()`) : forcer LDO1 à l'arrêt par
  `mfd_npm13xx_reg_write(pmic_dev, 0x08, NPM13XX_LDSW_TASK1CLR, 1)` — la
  commande est sur front et l'état du PMIC survit à un reset du SoC.
- Garder `k_msleep(5)` après `imu_rail_on()` (même marge qu'aujourd'hui après
  la montée réelle ; à réduire seulement en E).
- Sécurité : après un reset du SoC, P1.25 redevient une entrée non
  connectée, le pull-down 500 kΩ du nPM1300 la tire à 0 → front descendant →
  rail coupé.

### 8.5 Attendu et falsification

**Attendu :** −2 transactions bit-bang (~0,6-0,9 µC chacune à 400 kHz), −1
réveil (~0,5 µC), −2 ms de rail (~0,3 µC) → **−2,0 à −3,0 µC/cycle → ~9,5-11,5
µC ; régime établi ~12,9-14,9 µA.** Profil : plus aucune fenêtre bit-bang
après la lecture XYZ (fin de salve < 0,3 ms après), appel de charge en début
de salve.

**Falsification :** si le plancher entre salves passe de ~3,5 µA à ~220 µA,
le LDO ne se coupe plus (logique de fronts) → revenir en arrière
immédiatement. Si la charge/cycle ne baisse pas d'au moins 1,5 µC : le rail
monte plus lentement qu'en 8.3 (vérifier la variante et son instant).

---

## 9. Étape D — Accéléromètre haute performance 833 Hz

### 9.1 Caractérisation (`xiao_accel_odr_char/`, console)

Pour chaque configuration — 208 Hz normal (référence), 833 Hz HP, 1,66 kHz
HP — 200 cycles au repos (carte immobile) : après l'écriture de config,
interroger `STATUS_REG` (0x1E, bit XLDA) pour horodater le 1er échantillon,
lire les échantillons n° 1, n° 2, puis 5 à 10. Erreur de n° 1 et n° 2 par
rapport à la moyenne de 5 à 10, par axe.

**Critère :** |erreur| ≤ 0,05 m/s² (~5 mg) sur 99,5 % des cycles (seuil de
mouvement 0,3 m/s², hystérésis d'angle 2° ≈ 35 mg). Bruit HP : 90 µg/√Hz
→ ~1,8 mg rms à 833 Hz, très en dessous des seuils.

### 9.2 Changement de production

`CTRL6_C` laissé à son reset (HP) → **une seule écriture** à partir de 0x10 :
```c
uint8_t cfg[4] = {
	LSM6DSL_REG_CTRL1_XL,
	0x70,   /* CTRL1_XL : ODR_XL=0111 -> 833 Hz (HP), +/-2 g */
	0x00,   /* CTRL2_G  : gyroscope arrete */
	LSM6DSL_CTRL3_C_BDU | LSM6DSL_CTRL3_C_H_LACTIVE | LSM6DSL_CTRL3_C_IF_INC,
};
rc = i2c_write_dt(&imu_i2c, cfg, sizeof(cfg));
k_usleep(ACCEL_FIRST_SAMPLE_US);   /* valeur issue de 9.1 : ~1500 (n° 1) ou ~2700 (n° 2) */
```
(Au premier cycle, l'init du driver écrit `CTRL6_C.XL_HM_MODE=1` : sans effet
à 833 Hz, toujours HP d'après la table 52 de la fiche.)

**Température (trames santé uniquement) :** la fiche donne une cadence de
52 Hz hors mode basse consommation (table 5, note 2). La lecture actuelle à
~6,5 ms risque donc de précéder la 1ʳᵉ conversion ; **vérifier dans
l'historique HA si la température varie ou reste figée.** Dans le nouveau
code, sur les cycles santé seulement : interroger `STATUS_REG.TDA` (bit 2)
toutes les 2 ms (délai max 40 ms), puis lire `OUT_TEMP` (0x20-0x21),
`temp_c = 25 + raw/256`. Coût ≈ 10 µC toutes les 15 min, négligeable.

**Attendu :** fenêtre accéléromètre 6 ms → ~1,5-2,7 ms à (250 + 150 µA),
−1 transaction → **−1,0 à −1,6 µC/cycle ; régime établi ~11,5-13,5 µA.**
**Falsification :** gain < 0,6 µC → l'attente n'était pas le poste supposé :
profiler la fenêtre entre écriture et lecture.

---

## 10. Options E et F (seulement si utiles après D)

- **E — attente de démarrage IMU.** Même firmware de caractérisation :
  délais 1 à 5 ms après la montée du rail (SDA = 1), puis config + lecture ;
  comparer à une référence à 35 ms (200 cycles : décalage moyen < 1 mg, écart
  type inchangé, `WHO_AM_I` correct). Retenir 2× le minimum fonctionnel,
  **jamais < 3 ms.** Gain : ~0,25 µC par ms retirée.
- **F — LDO1 à 3,0 V** (`regulator-min/max-microvolt = <3000000>`). Marges :
  VIH SoC = 0,7 × 3,3 = 2,31 V ; IMU ≥ 1,71 V ; micro ≥ 1,6 V. Attendu :
  appel de charge −9 % (~−0,3 µC) et probablement courant statique du rail
  en baisse. Garder seulement si ≥ 0,3 µC mesurés et aucune erreur I2C en
  1 h de fonctionnement.

---

## 11. Clôture du ticket Nordic (brouillon à faire envoyer par l'utilisateur)

> Subject: Re: Non-reproducible `west build` — resolved on our side, please close
>
> Hi, following up on our 2026-09-19 report: the cause was ours, not the
> toolchain. Rebuilding the exact commit that produced our reference image
> gives a byte-identical `zephyr.bin` (SHA-256 `75b92e95…`), and the same
> commit plus the one-bit `H_LACTIVE` change reproduces our second reference
> byte for byte (`0da087ae…`). The "bad" rebuild came from a later commit in
> which the accelerometer settling delay had been changed from 6 ms to
> 40 ms (`movs r0,#6` vs `movs r0,#40` before the `k_msleep` call), plus
> unrelated logic changes. We wrongly believed the reference had been built
> from that later source. The extra 34 ms of IMU-rail on-time per 1 s cycle
> fully explains the ~+10 µA. `west build` with NCS 3.4.0 / toolchain
> `dcbdc366a1` is deterministic in our tests. Sorry for the noise — this
> ticket can be closed. The LDO1-rail current question remains separate.

---

## 12. Livraison finale

1. Candidat final sur #02 : PPK2 ≥ 5 min incluant une trame santé ; tests
   fonctionnels HA complets (mouvement réel, angle, bouton, IMU brut,
   batterie, température).
2. Nouvelle image d'or `golden-image/unit02-verified-<date>-<desc>.bin/.hex`
   (SHA-256 noté dans `Procedure-Clonage…md`), puis — **sur décision de
   l'utilisateur** — redéploiement des unités 04-13 avec les scripts de lot
   (mettre à jour `GOLDEN_HEX` dans `flash-unit.sh`).
3. `Configuration-nRF54LM20A-System-ON-IDLE.md` : nouvelle architecture de
   cycle, mesures, historique des étapes.

---

## 13. Observations annexes (hors consommation, à trancher avec l'utilisateur)

- **Yaw :** `yaw += gz × Δt` avec Δt = temps écoulé depuis la lecture gyro
  précédente (minutes, heures) : ce n'est pas une intégration, la valeur
  envoyée n'a pas de sens physique. Supprimer le yaw supprimerait aussi la
  rafale gyroscope de 200 ms par événement (~0,25 mC). Décision produit.
- **Confirmation sur 2 cycles** (HEAD, commit `1994112`) : absente de
  l'image déployée ; ajoutée contre des « fausses détections » en réalité
  dues à un firmware de diagnostic resté sur #02. Ne pas la réintroduire sans
  preuve.
- **Plan de résilience** (watchdog, `RESET_ON_FATAL_ERROR`, compteurs) : sa
  « régression » venait de la base à 40 ms. À ré-appliquer après ce plan, avec
  sa propre mesure (coût attendu < 0,5 µA).
- **Option pile non rechargeable :** refaire le test « charge désactivée »
  sur la bonne base avant toute conclusion (§1.3).

---

## 14. Outils fournis

- `tools/ppk_cycle_stats.py <csv>` : moyenne globale, plancher, charge par
  cycle (moyenne/médiane), période, régime établi ; liste des autres
  événements (advertising, trames).
- `tools/ppk_profile.py <csv> T0_s T1_s BIN_us` : profil fin d'une salve,
  pour attribuer un gain à une phase.

Référence obtenue avec ces outils : §2.1.
