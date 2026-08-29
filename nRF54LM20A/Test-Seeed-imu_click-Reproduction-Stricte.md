# Test — reproduction stricte de l'exemple officiel Seeed (imu_click)

**Date** : 2026-08-27. Projet : `nRF54LM20A/xiao_seeed_imu_click/`.
Objectif : déterminer si le ~250-300 µA observé sur `xiao_door_sensor`
dès que `imu_vdd`/LDO1 est actif est un bug propre à notre firmware, ou
une caractéristique indépendante du code — en mesurant directement le
code du fournisseur, inchangé, dans nos conditions de mesure établies.

## Origine du code

Source : `wiki.seeedstudio.com/xiao_nrf54lm20a_with_onboard/`, exemple
`imu_click`. Fichier complet récupéré depuis
`files.seeedstudio.com/wiki/XIAO_nRF54LM20A/getting_start/RES/imu_click_main.c`
(copié verbatim, aucune modification du code applicatif). Devicetree
overlay et `prj.conf` copiés depuis la page wiki, avec un seul ajout
nécessaire pour compiler : désactivation de `usbhs`/`usbhs_wrapper`
(bug de build déjà rencontré et documenté sur `xiao_door_sensor`,
absent du fragment publié par Seeed mais requis sur cette carte/
toolchain -- sans rapport avec la consommation).

Le code active `power_en` et `imu_vdd`/LDO1 (`regulator-boot-on` +
`regulator_enable()` explicite), initialise le driver LSM6DSL
(`zephyr,deferred-init` + `device_init()` explicite), configure la
détection de tap directement par écriture registre (`TAP_CFG`,
`TAP_THS_6D`, `INT_DUR2`, `WAKE_UP_THS`, `MD1_CFG`), et **efface
explicitement `INT1_CTRL` à 0x00** après coup, avec ce commentaire du
fournisseur : *« The Zephyr driver sets DRDY_XL | DRDY_G during init;
we override this to prevent periodic wakeups and achieve ultra low
power »*.

## Différence architecturale majeure avec notre code

**Ce firmware n'appelle jamais `sys_poweroff()`.** La boucle principale
reste bloquée sur `k_sem_take(&tap_sem, K_FOREVER)` -- veille standard
Zephyr (« System ON idle »), pas le véritable System OFF matériel du
SoC que vise `xiao_door_sensor`. Ce n'est donc pas directement
comparable à notre architecture cible, mais c'est l'état de repos tel
que Seeed l'a conçu et publié.

## Vérification fonctionnelle (avant mesure)

Console/logs temporairement activés pour confirmer le bon
fonctionnement avant la mesure PPK2 propre. Capture série après reset :

```
[00:04:06.864,807] <inf> imu_wakeup: LED OFF - Entering sleep mode
[00:04:06.922,736] <inf> imu_wakeup: IMU device ready
[00:04:06.923,939] <inf> imu_wakeup: Tap detection configured on IMU hardware
[00:04:06.923,963] <inf> imu_wakeup: Entering ultra low power sleep - waiting for IMU tap...
```

Aucune erreur. `device_init()` réussit, la configuration tap
(écritures registre) réussit intégralement.

## Mesures PPK2 (Source meter, 3,7 V, BAT+/BAT- uniquement, USB-C
déconnecté, protocole standard du projet)

| Capture | Fenêtre | Moyenne | Max | Charge |
|---|---|---|---|---|
| Repos (avant tap) | 10 s | **325,31 µA** | 3,67 mA | 2,32 mC |
| Repos, zoom détail | 6,9 ms | 333,34 µA | 466,94 µA | 2,30 µC |
| Après un tap réel (LED verte confirmée) | 10 s | **319,22 µA** | 3,75 mA | 3,19 mC |

Le tracé zoomé (6,9 ms) montre un motif répété en dents de scie
(~350→470→300 µA sur quelques ms) plutôt qu'un plancher stable --
cohérent avec un CPU qui ne descend jamais en veille profonde (tick
système Zephyr, ≈10 ms par défaut) puisque `sys_poweroff()` n'est
jamais appelé. Le courant ne change pas de façon significative avant/
après un tap réel : la salve associée à l'événement (lecture registre,
clignotement LED) est négligeable face au plancher continu.

## Conclusion factuelle (pas de recommandation de stratégie ici)

- L'exemple officiel Seeed, **exécuté sans modification** sur l'unité
  #01, consomme **~320-350 µA** -- du même ordre de grandeur que nos
  propres mesures sur `xiao_door_sensor` (250-300 µA), pas plus bas.
- La page wiki source ne documente aucune mesure de consommation ; le
  chiffre ~4,76-4,93 µA évoqué antérieurement dans cette investigation
  ne provient donc pas de cet exemple précis.
- **Le ~250-350 µA associé à `imu_vdd`/LDO1 actif n'est pas propre à
  notre firmware** : le code du fournisseur, sur la même carte,
  présente le même ordre de grandeur. Ceci écarte une erreur de
  configuration spécifique à `xiao_door_sensor` comme explication
  unique.
- Cette mesure ne dit rien de plus sur la cause matérielle exacte, et
  ne permet pas de conclure qu'un objectif de 5-6 µA avec réveil IMU
  actif en continu soit ou non atteignable -- voir le document de plan
  séparé pour les prochaines étapes.
