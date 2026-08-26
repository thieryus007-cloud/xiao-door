# Transition — Optimisation consommation XIAO nRF52840 Sense

**Document de démarrage pour une nouvelle conversation**, dédiée à la suite
de la réduction de consommation du firmware `xiao_nrf52840_door_sensor`.
Lire en premier `C:\ncs\CLAUDE.md` (règles de travail) et
`C:\ncs\projects\Transition-nRF52840-Sense-Demarrage.md` (historique complet
du portage, architecture System ON, tout ce qui a déjà été validé sur les
3 unités — motion, batterie, BLE, HA). Ce document-ci se concentre
uniquement sur l'optimisation de consommation, sujet en cours.

**Ne pas confondre avec le projet séparé nRF54LM20A** (`xiao_door_sensor`,
doc `PPK-Mesures-Transition.md`) — deux cartes différentes, deux docs de
transition différents.

## État au 2026-08-30 (à vérifier au démarrage, pas à supposer)

- **Firmware actuellement flashé sur l'unité #3** : configuration finale,
  aucun changement temporaire de diagnostic en place :
  - `CONFIG_BT=y` (BLE actif), logging/console/USB natifs désactivés
    (`CONFIG_LOG=n`, `CONFIG_SERIAL=n`, `CONFIG_CONSOLE=n`,
    `CONFIG_USB_DEVICE_STACK=n` — **définitif**, pas un correctif
    temporaire).
  - Régulateur interne `&reg1` en **DC-DC** (défaut de la carte, confirmé
    meilleur que LDO après test chiffré, voir plus bas).
  - Flash QSPI externe à son état par défaut (active mais jamais utilisée
    par l'appli — testée désactivée aussi, sans effet, voir plus bas).
  - Binaire : `C:/ncs/projects/xiao_nrf52840_door_sensor/build/xiao_nrf52840_door_sensor/zephyr/zephyr.uf2`.
- **Montage de mesure établi, volontaire, ne pas remettre en cause** :
  unité #3 (`DC:07:4F:94:1C:F3`) sur **PPK2** (série `EF23F2044470`),
  mode **Source Meter**, **3700 mV**, injection via les pastilles B+/B-
  identifiées au multimètre, câble USB-C débranché pendant la mesure.
  Application nRF Connect for Desktop → Power Profiler, mode
  **Data Logger** (pas "Scope").
- **Problème non résolu** : au repos (pas de mouvement), un train de pics
  périodiques revient toutes les ~20-25 ms, avec une moyenne mesurée
  autour de **18 µA** — nettement au-dessus de l'objectif ~2 µA visé pour
  cette carte en System ON (cf. doc principal, recherche `CONFIG_PM=y`).
  **Root cause non identifiée à ce jour** malgré une élimination
  méthodique de plusieurs pistes (détail ci-dessous).

## Pistes éliminées pour les pics ~20-25 ms (vérifié, pas supposé)

Chaque ligne = un changement isolé testé sur matériel réel avec capture
PPK2 avant/après, en respectant la règle « un seul changement à la fois ».

| Piste | Test fait | Résultat |
|---|---|---|
| Contrôleur BLE / MPSL | Firmware avec `CONFIG_BT=n` (MPSL absent du build, vérifié dans les libs liées) | Pics identiques, présents sans BT compilé |
| BLE réactivé | Revenu à `CONFIG_BT=y` (MPSL/BLE controller bien liés cette fois) | Pics identiques à nouveau — donc **BLE/MPSL exclu dans les deux sens** |
| Régulateur DC-DC vs LDO | `&reg1` en LDO (`ppk-20260826T094921.csv`) vs DC-DC (`ppk-20260826T085609.csv`) | Pics présents dans les deux modes, juste mis à l'échelle (LDO : +8% moyenne, +43% pic, +8% charge — LDO moins efficace, pas la cause des pics) → **DC-DC confirmé meilleur, conservé** |
| Flash QSPI externe (P25Q16H) | `&qspi { status = "disabled"; }` (`ppk-20260826T101648.csv`) | 17,66-18,03 µA, quasi identique à la référence → **exclu**, changement retiré après test |
| Trigger interruption IMU | `.config` : `CONFIG_LSM6DSL_TRIGGER_NONE=y` confirmé | Pas d'interruption IMU compilée, ODR appliquée seulement par l'appli au runtime → exclu par construction |
| Timer applicatif périodique | Recherche dans `main.c` | Aucun `k_timer`/`k_work_schedule`/`k_work_reschedule` dans le fichier → exclu |
| Calibration LFCLK RC interne | `.config` : `CONFIG_CLOCK_CONTROL_NRF_K32SRC_XTAL=y` confirmé | Cristal externe utilisé, pas le RC interne (donc pas de calibration RC périodique) → exclu |
| Générateur d'entropie matériel | Test BT=n utilisait `CONFIG_TEST_RANDOM_GENERATOR` (PRNG logiciel, pas de HW RNG) et montrait déjà les pics | Le driver d'entropie matériel n'était même pas sollicité → exclu par construction |

**Recherche externe faite (2026-08-30)** : plusieurs sources confirment
qu'une consommation idle "System ON" nRF52 plus élevée que le datasheet
(~2 µA visés) est un problème connu et récurrent avec Zephyr/nRF Connect
SDK (Nordic DevZone, issue GitHub zephyrproject-rtos/zephyr#12367), mais
**aucune source trouvée ne documente précisément la périodicité ~20-25 ms**
ni n'identifie de cause unique confirmée — les threads consultés pointent
vaguement vers "interrupt controller, timer, serial, RNG drivers" sans
conclusion ferme. Cette piste ne peut probablement pas être creusée
davantage par recherche seule.

## Pistes restantes, non testées (pour la prochaine session)

1. **`CONFIG_PM=y` lui-même** : jamais testé en l'isolant (`CONFIG_PM=n`
   temporairement) pour voir si les pics sont un artefact du framework de
   gestion d'énergie de Zephyr plutôt que du matériel — test simple à
   faire, un seul changement.
2. **Régulateurs fixes toujours actifs de la carte** : `msm261d3526hicpm-c-en`
   (alimentation du micro PDM, jamais utilisé par ce firmware) — vérifier
   son état par défaut (`regulator-boot-on` ou non) dans
   `xiao_ble_common.dtsi`/`xiao_ble_nrf52840_sense.dts` ; jamais désactivé
   ni mesuré isolément à ce jour.
3. **Traçage matériel** : les pistes Kconfig/devicetree "faciles" sont
   épuisées. Une confirmation définitive demanderait probablement un outil
   de traçage (RTT/ITM, ou un analyseur logique sur les lignes
   d'interruption du nRF52840) plutôt qu'une nouvelle itération de
   build/mesure — à évaluer si le sujet reste prioritaire.
4. Comparer avec une nRF52840 DK nue (sans les circuits spécifiques à la
   carte XIAO Sense — régulateurs IMU/micro, pont USB, etc.) pourrait
   isoler si le phénomène est générique au SoC/Zephyr ou spécifique à
   cette carte.

## Autre mesure utile obtenue en cours de route

**Capture avec mouvement réel (2026-08-30)**, config finale (BT actif) :
154,84 µA moyen, pic 9,35 mA, 243,28 µC sur 1,571 s — confirme que le
contrôleur BLE fonctionne et transmet (rafales visibles à 6-9 mA). Donnée
utile pour le poste "radio BLE" du budget énergétique, mais ne remplace
pas la mesure au repos ci-dessus.

## Chronologie complète des mesures (résumé)

1. Firmware complet d'origine (BLE actif, logging/USB actifs) : ~3,1 mA de
   repos — bien trop élevé.
2. Logging/console/USB désactivés (correctif définitif conservé) :
   descendu à ~18-20 µA de moyenne au repos, mais toujours ~9-10x l'objectif
   ~2 µA, avec les pics ~20-25 ms toujours présents.
3. Diagnostic BT=n, test LDO/DC-DC, test QSPI désactivée, retour BT=y :
   voir tableau d'élimination ci-dessus — aucune de ces pistes n'explique
   les pics.

*Une première analyse plus ancienne du 2026-08-26 (ci-dessous, conservée
pour référence) avait avancé l'hypothèse du régulateur DC-DC en mode
pulse-skip comme cause du "gazon" ~40-50 Hz observé — cette hypothèse a
depuis été testée et infirmée (voir tableau d'élimination). Les chiffres
d'inventaire ci-dessous restent utiles comme ordre de grandeur mais la
conclusion sur le régulateur est dépassée :*

<details>
<summary>Inventaire historique du 2026-08-26 (hypothèse régulateur depuis infirmée)</summary>

Inventaire complet des pics identifiés, avec quantification de leur
contribution réelle. En croisant le CSV exporté (résolution 10 µs) avec
les chiffres globaux des captures précédentes (55,05 µC / 3,000 s pour la
vue large, 3,90 µC / 100 ms pour la vue zoomée) :

| # | Événement | Fréquence | Durée | Pic | Charge/occurrence | Contribution moyenne |
|---|---|---|---|---|---|---|
| 1 | Rafale I2C (`read_accel()`, boucle principale) | 1×/s (`POLL_INTERVAL_MS`) | 0,76 ms | 6,95 mA | 2,19 µC | ~2,2 µA |
| 2 | Rafale secondaire (effet consécutif, ~1,16 ms après #1) | 1×/s (accolée à #1) | 1,54 ms | 0,88 mA | 0,28 µC | ~0,3 µA |
| 3 | "Gazon" continu (~40-50 Hz, visible sur toute capture) | continu | — | jusqu'à ~470 µA | — | ~14-16 µA ← dominant |
| 4 | Heartbeat horaire (`FRAME_A_HEARTBEAT_MS`) | 1×/h | ~250 ms | ~0,9 mA (spec) | ~0,23 µC | ~0,06 µA (calculé) |
| 5 | Lecture batterie/ADC (`FRAME_B_INTERVAL_MS`) | 1×/15 min | quelques ms | faible | négligeable | <<0,1 µA (calculé) |

Calcul de vérification (double recoupement) : sur la fenêtre de 3,000 s
(3 rafales visibles), 3 × 2,47 µC (rafales 1+2) = 7,41 µC, il reste
47,64 µC pour le gazon → 15,9 µA de moyenne. Sur la fenêtre de 100 ms
(1 rafale visible), 3,90 − 2,47 = 1,43 µC de gazon sur ~99 ms → 14,4 µA
de moyenne. Les deux recoupements s'accordaient (~14-16 µA).

Hypothèse avancée à l'époque (depuis infirmée par le test LDO/DC-DC
chiffré) : `xiao_ble_common.dtsi:61`, `regulator-initial-mode =
<NRF5X_REG_MODE_DCDC>` — le régulateur DC-DC basculerait en mode
rafale/pulse-skip sous charge légère, causant le "gazon". Le test LDO a
montré que les pics persistent identiquement en LDO (juste mis à
l'échelle par l'efficacité de conversion différente), donc **cette
hypothèse ne tient plus**.

</details>

## Procédure de flash pour cette carte (UF2 — établie, ne pas reposer la question)

Le firmware tourne alimenté par le PPK2 en Source Meter, USB-C débranché.
Le bootloader UF2 a besoin de l'USB pour apparaître en disque — séquence à
suivre pour ne jamais avoir PPK2 (source active) et USB-C branchés en même
temps sur la carte :

1. Couper la sortie du PPK2 (bouton "Stop"/toggle dans l'appli) ou
   débrancher les pastilles B+/B- avant de toucher à l'USB.
2. Brancher le câble USB-C.
3. Double-tap rapide sur le bouton RESET physique → le disque amovible
   `XIAO-SENSE` apparaît.
4. **Copier `zephyr.uf2` — c'est Claude qui le fait**, pas l'utilisateur
   (accès USB fonctionnel depuis l'environnement d'exécution, confirmé le
   2026-08-30 après une fausse alerte initiale). Le lecteur n'apparaît que
   quelques secondes après le double-tap — scruter en boucle, pas un
   `Get-Volume` ponctuel :
   ```powershell
   # Lancer cette boucle AVANT que l'utilisateur fasse le double-tap,
   # dire "prêt, faites le double-tap maintenant", laisser tourner :
   $found = $false
   for ($i = 0; $i -lt 20; $i++) {
       $vol = Get-Volume | Where-Object { $_.DriveType -eq 'Removable' -and $_.DriveLetter }
       if ($vol) { $found = $true; $vol | Format-Table DriveLetter,FileSystemLabel; break }
       Start-Sleep -Milliseconds 1500
   }
   # dangerouslyDisableSandbox: true nécessaire pour voir le vrai matériel USB.
   # Une fois trouvé (ex. D:) :
   Copy-Item -Path "<chemin>\zephyr.uf2" -Destination "D:\" -Force
   # Le lecteur disparaît automatiquement (carte qui redémarre) -- signal
   # de succès attendu, pas une erreur.
   ```
5. Attendre le redémarrage automatique (le disque disparaît).
6. Débrancher l'USB-C (utilisateur).
7. Rebrancher le PPK2 sur les mêmes pastilles B+/B-, remettre Source
   Meter/3700 mV, relancer une capture Data Logger (utilisateur).

## Méthode pour analyser un export CSV PPK2

Format `Timestamp(ms),Current(uA)`. Les figures précises de ce document
sont lues directement dans les compteurs "WINDOW"/"SELECTION" de l'appli
Power Profiler (déjà calculés, fiables) plutôt que recalculées à la main —
plus rapide et tout aussi précis pour les comparaisons avant/après.

## Rappel des règles de travail (cf. CLAUDE.md)

- Communiquer tout changement/mesure en détail avant d'agir, vérifier
  l'état réel du matériel avant de supposer qu'il tient toujours.
- Un seul changement à la fois entre deux mesures PPK2, jamais plusieurs
  hypothèses testées simultanément (leçon d'une régression passée causée
  par un changement combiné `CONFIG_PM_DEVICE=y`).
- Le dépôt GitHub `https://github.com/thieryus007-cloud/xiao_nrf52840_door_sensor`
  contient le code + ce document, à tenir à jour avec des commits/push sur
  demande explicite de l'utilisateur (pas automatique).
