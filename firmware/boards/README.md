# Board support — XIAO nRF54LM20A

Origine : [Seeed-Studio/platform-seeedboards](https://github.com/Seeed-Studio/platform-seeedboards) (`zephyr/boards/arm/xiao_nrf54lm20a/`), licence Apache-2.0. Copié ici (au lieu d'être référencé en submodule) pour figer une version qui compile de manière fiable sur les ~20 unités, indépendamment de l'évolution du dépôt amont.

## Patch appliqué

Le portage amont ciblait à l'origine le nom Kconfig `SOC_NRF54LM20A_CPUAPP` / `SOC_NRF54LM20A_CPUFLPR`, qui n'existe pas dans nRF Connect SDK **v3.2.1** (silicium final). Le SDK v3.2.1 ne connaît que la variante silicium ingénierie A : `SOC_NRF54LM20A_ENGA_CPUAPP` / `SOC_NRF54LM20A_ENGA_CPUFLPR` (voir `zephyr/soc/nordic/nrf54l/Kconfig.soc` dans le SDK).

Sans correction, `west build` échoue avec une cascade d'avertissements Kconfig (`CACHE_MANAGEMENT`, `ARM_MPU`, `UART_CONSOLE`, etc. non satisfaits) car le symbole SoC sélectionné n'existe pas et la chaîne de config ne se met jamais en place.

Fichiers modifiés par rapport à l'amont :

- `Kconfig.xiao_nrf54lm20a` — `select SOC_NRF54LM20A_CPUAPP` → `select SOC_NRF54LM20A_ENGA_CPUAPP` (idem pour CPUFLPR)
- `board.cmake` — `if(CONFIG_SOC_NRF54LM20A_CPUAPP)` → `if(CONFIG_SOC_NRF54LM20A_ENGA_CPUAPP)` (idem CPUFLPR)
- `nrf54lm20a_cpuapp_common.dtsi` — `#include <nordic/nrf54lm20a_cpuapp.dtsi>` → `#include <nordic/nrf54lm20a_enga_cpuapp.dtsi>` (déjà présent avant cette session)

**Si une future version du nRF Connect SDK ajoute le support du silicium final** (`SOC_NRF54LM20A_CPUAPP` sans `_ENGA`), il faudra retester si l'amont Seeed a aussi été mis à jour et potentiellement revenir aux symboles non-ENGA.

## Build de test (validé)

```bash
source firmware/build-env.sh
west build -p always -b xiao_nrf54lm20a/nrf54lm20a/cpuapp \
  -d /tmp/build-blink \
  firmware/examples/blink/zephyr \
  -- -DBOARD_ROOT=$(pwd)/firmware
```

Voir `firmware/build-env.sh` pour l'activation de l'environnement toolchain nRF Connect SDK.
