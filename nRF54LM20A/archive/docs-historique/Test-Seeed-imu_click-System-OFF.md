# Test — exemple Seeed (imu_click) avec vrai System OFF (sys_poweroff())

**Date** : 2026-08-27. Suite de `Test-Seeed-imu_click-Optimisations.md`
(315 µA). Modification : remplacement de la boucle infinie
(`k_sem_take(K_FOREVER)`, CPU jamais éteint) par un vrai
`sys_poweroff()`, réveil armé directement sur la broche INT1 de l'IMU
(`gpio_pin_interrupt_configure_dt(&imu_int1, GPIO_INT_LEVEL_ACTIVE)`),
même mécanisme que l'exemple officiel Zephyr
`samples/boards/nordic/system_off`.

## Vérification fonctionnelle (avant mesure)

Mode de log immédiat (`CONFIG_LOG_MODE_IMMEDIATE=y`, diagnostic
temporaire) : séquence complète confirmée à chaque cycle --
`enable_imu_power()` ret=0, `device_init()` ret=0, "IMU device ready",
"Tap detection configured on IMU hardware", "Entering System OFF".
Lecture registre CPU par sonde SWD (`halt` + `reg pc`) : PC pointait
dans `nrf_regulators_system_off` (`nrf_regulators.h:573`) -- le SoC
exécute bien l'appel matériel réel de mise hors tension, aucun plantage.

Le redémarrage en boucle observé ensuite sous USB-C (`reset_cause` =
"Reset by debugger" à chaque tour, ~145 ms d'intervalle) est le Debug
Interface Mode déjà documenté sur ce projet -- artefact du branchement
SWD/USB-C, pas un bug du firmware.

## Mesure PPK2 (après coupure d'alimentation réelle, protocole standard)

**312 µA.**

## Conclusion (utilisateur) : pas de System OFF effectif

Malgré un `sys_poweroff()` qui s'exécute réellement côté SoC, le
courant mesuré ne baisse pas -- quasi identique aux tests précédents
(315-319 µA, sans `sys_poweroff()`).

## Cause identifiée dans le code

`imu_vdd`/LDO1 est activé une seule fois (`regulator-boot-on` dans
l'overlay + `regulator_enable()` explicite dans `enable_imu_power()`)
et **n'est jamais désactivé nulle part dans le code**, y compris juste
avant `sys_poweroff()`. Fait déjà établi dans cette investigation (voir
mémoire projet `nPM1300 register persistence`) : **le régulateur du
nPM1300 reste alimenté en continu par la batterie, indépendamment du
System OFF du SoC.** Un vrai System OFF côté SoC n'éteint donc pas le
rail `imu_vdd` -- son surcoût (~250-300 µA, isolé et documenté tout au
long de cette investigation) continue de s'appliquer sans interruption,
peu importe l'état de sommeil du SoC.

## Implication structurelle

Pour que l'IMU génère une interruption de réveil asynchrone (tap ou
mouvement), son alimentation doit rester active en permanence -- il n'y
a pas de moyen de couper `imu_vdd` tout en gardant la capacité de
réveil par interruption. Sur ce matériel, maintenir `imu_vdd` actif en
continu coûte ~250-300 µA, indépendamment de tout ce que fait le SoC
(System OFF, veille Zephyr ordinaire, optimisations PM_DEVICE) --
c'est un plancher fixé par le régulateur/IMU, pas par le firmware.
