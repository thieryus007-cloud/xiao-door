# Étape 0 — Préparation de l'environnement

Objectif : avoir une chaîne de compilation/flash fonctionnelle pour le XIAO nRF54LM20A Sense avant de toucher au code Matter.

**Statut : ✅ terminée** (16/08/2026)

## Checklist

- [x] Installer nRF Connect SDK — **v3.2.1** déjà installée (`/opt/nordic/ncs/v3.2.1`), toolchain `322ac893fe` via `nrfutil sdk-manager`
- [ ] Installer VS Code + extension **nRF Connect for VS Code** (optionnel si on reste en CLI/`west`)
- [x] Board support Seeed pour le XIAO nRF54LM20A Sense — vendorisé dans `firmware/boards/xiao_nrf54lm20a/` (voir `firmware/boards/README.md` pour l'origine et le patch appliqué)
- [x] Compiler un exemple simple (Blinky) pour la board XIAO nRF54LM20A Sense — succès
- [x] **Connecter le XIAO en USB-C** et flasher l'exemple compilé — unité `9C4A557D` (voir `devices/unit-01/`)
- [x] Vérifier que la LED clignote correctement — confirmé par l'utilisateur (16/08/2026)

## Compiler et flasher (procédure validée)

```bash
source firmware/build-env.sh

# Compiler
west build -p always -b xiao_nrf54lm20a/nrf54lm20a/cpuapp \
  -d /tmp/build-blink \
  firmware/examples/blink/zephyr \
  -- -DBOARD_ROOT=$(pwd)/firmware

# Flasher (OpenOCD requis — voir note ci-dessous)
export PATH="/opt/homebrew/bin:$PATH"   # pour openocd
west flash -d /tmp/build-blink --runner openocd
```

Résultat compilation : FLASH 30132 B (1.45%), RAM 6648 B (1.27%).

## Problèmes rencontrés et corrigés

1. **Symbole Kconfig SoC incompatible avec NCS v3.2.1** — le portage de board Seeed amont ciblait `SOC_NRF54LM20A_CPUAPP`, qui n'existe pas dans NCS v3.2.1 (celui-ci ne supporte que la variante silicium ingénierie A, `SOC_NRF54LM20A_ENGA_CPUAPP`). Patch appliqué dans `Kconfig.xiao_nrf54lm20a` et `board.cmake`. Détails dans `firmware/boards/README.md`.
2. **`west flash` par défaut utilise le runner `openocd`, non installé** → `brew install openocd`.
3. **Le runner `nrfutil` ne fonctionne pas** — il ne détecte que les sondes SEGGER J-Link (filtre sur `traits.jlink`). La sonde de debug intégrée au XIAO est un CMSIS-DAP générique (VID:PID `0x2886:0x0068`), pas un J-Link. Toujours utiliser `--runner openocd` explicitement.
4. **Au premier flash, OpenOCD a rencontré `AP lock engaged`** (carte neuve, jamais flashée) — récupéré automatiquement par OpenOCD (`trying recover` → `successfully erased and unlocked`), sans action manuelle nécessaire.
5. Après flash, la carte reste parfois arrêtée par OpenOCD/GDB — un débranchement/rebranchement USB-C suffit à relancer le firmware (reset matériel).

## 🔌 Quand connecter le XIAO

Pour les prochaines unités, la connexion USB-C est nécessaire uniquement à l'étape de flash (compilation possible sans matériel connecté). Le repère **🔌 CONNECTER LE XIAO MAINTENANT** sera utilisé explicitement le moment venu.

## État d'avancement

| Sous-étape | Statut |
| --- | --- |
| Installation nRF Connect SDK | ✅ fait |
| Extension VS Code | ⬜ à faire (optionnel) |
| Board support Seeed | ✅ fait (vendorisé + patché) |
| Compilation Blinky | ✅ fait |
| Flash + validation matérielle | ✅ fait (unit-01) |

## Notes techniques

- SDK : nRF Connect SDK v3.2.1, toolchain `322ac893fe`, installés via `nrfutil sdk-manager` (préexistant sur cette machine)
- Board identifier Zephyr : `xiao_nrf54lm20a/nrf54lm20a/cpuapp`
- Environnement de build : `firmware/build-env.sh` (à `source`r avant tout `west build`/`west flash`)
- OpenOCD (Homebrew, `/opt/homebrew/bin/openocd`) requis pour le flash — pas fourni par le toolchain NCS
