# Test — exemple Seeed (imu_click) après nos optimisations

**Date** : 2026-08-27. Projet : `nRF54LM20A/xiao_seeed_imu_click/`.
Suite de `Test-Seeed-imu_click-Reproduction-Stricte.md` (~320-350 µA,
code fournisseur inchangé). Objectif : appliquer nos propres
optimisations connues, sans changer l'architecture (pas de System OFF
à ce stade — voir § Ce qui n'a volontairement pas été fait).

## Modifications apportées

### Test #19 -- retrait du code LED

LED verte non nécessaire au projet. Retiré intégralement de
`src/main.c` : includes GPIO devenus inutiles, définitions
`LED_NODE`/`LED_PORT`/`LED_PIN`/`LED_FLAGS`, configuration/allumage/
extinction au boot, clignotement sur détection de tap. Aucune autre
logique touchée (détection de tap, lecture posture, etc. conservées
à l'identique).

Vérification fonctionnelle (console temporaire) après retrait :
```
[00:10:55.690,670] <inf> imu_wakeup: IMU device ready
[00:10:55.691,883] <inf> imu_wakeup: Tap detection configured on IMU hardware
[00:10:55.691,908] <inf> imu_wakeup: Entering ultra low power sleep - waiting for IMU tap...
```
Aucune erreur -- le retrait n'a rien cassé.

**Mesure PPK2 (repos, avant tap)** : moyenne **319,48 µA** (fenêtre
10 s), max 3,69 mA, charge 3,19 mC. Motif en dents de scie toujours
présent (zoom 13,74 ms : 308,50 µA moyenne, 474,70 µA max).

### Test #20 -- CONFIG_PM_DEVICE / CONFIG_PM_DEVICE_RUNTIME

Absents du `prj.conf` publié par Seeed, présents dans `xiao_door_sensor`
(notre propre projet). Ajoutés tels quels, aucun autre changement
(même `src/main.c` que le test #19).

**Mesure PPK2 (repos, avant tap)** : moyenne **315 µA**.

## Comparatif

| Étape | Changement | Moyenne mesurée |
|---|---|---|
| Test #18 (référence) | Code Seeed inchangé | 325,31-333,34 µA |
| Test #19 | LED retirée | 319,48 µA |
| Test #20 | + `CONFIG_PM_DEVICE`/`_RUNTIME` | 315 µA |

**Aucune des deux optimisations n'a d'effet mesurable** (les trois
valeurs sont dans la même plage, à la variabilité normale de mesure
près). Ceci écarte :
- Le code LED comme contributeur significatif (attendu -- actif
  seulement au boot et sur événement, négligeable face à un plancher
  continu).
- Un bus I2C resté actif entre transactions comme cause du motif
  périodique observé -- `PM_DEVICE_RUNTIME` est censé permettre sa
  suspension automatique entre transactions, sans effet ici.

## Ce qui n'a volontairement pas été fait à ce stade

Le levier qui, dans toute cette investigation, a systématiquement fait
la différence entre quelques µA et plusieurs centaines de µA est
`sys_poweroff()` (vrai System OFF matériel) -- jamais une optimisation
de la veille Zephyr ordinaire (« System ON idle »), même avec
`PM_DEVICE`/`PM_DEVICE_RUNTIME` actifs. Faire entrer cet exemple en
vrai System OFF avec réveil GPIO sur INT1 changerait l'architecture du
firmware, pas juste sa configuration -- ce n'est pas fait ici
volontairement, pour ne pas anticiper la décision de stratégie
(document séparé, à venir).

## Prochaine étape

Décision entre les options de stratégie identifiées (document de plan,
`XIAO-nRF54LM20A-Solution-System-OFF.md` § Plan en cours) -- pas de
recommandation ici.
