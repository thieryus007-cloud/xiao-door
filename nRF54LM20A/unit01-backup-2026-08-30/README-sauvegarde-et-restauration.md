# Sauvegarde complète unité #01 — 2026-08-30

Sauvegarde faite **avant** tout nouvel essai de firmware de diagnostic sur
#01 (autorisé explicitement par l'utilisateur le 2026-08-30, dans le cadre
de l'investigation de l'écart de consommation #01 vs #02 — voir
`xiao_nrf54lm20a_project_notes.md` et `Configuration-nRF54LM20A-System-ON-IDLE.md`
pour le contexte complet). Ce dossier est dédié à cette sauvegarde et à sa
restauration — ne pas y mélanger d'autres fichiers de test.

## Identification de l'unité sauvegardée

- Serial pont USB↔SWD (SAMD11) : `C5F0E209`, confirmé par `Get-PnpDevice`
  ET par le log OpenOCD (`CMSIS-DAP: Serial# = C5F0E209`) juste avant le
  dump.
- Correspond à l'unité #01 du tableau de déploiement
  (`Configuration-nRF54LM20A-System-ON-IDLE.md` §7).
- Port COM série (SAMD11 CDC) au moment de la sauvegarde : COM3.

## Fichiers de ce dossier

| Fichier | Contenu |
|---|---|
| `unit01-full-rram-backup-2026-08-30.bin` | Dump binaire brut, lecture seule SWD, **adresse 0x0, longueur 2 084 864 octets = intégralité de la RRAM du SoC nRF54LM20A** (2036 Ko, `zephyr/dts/vendor/nordic/nrf54lm20_a_b.dtsi:917`), pas seulement la taille de l'image applicative connue — sauvegarde volontairement maximaliste. |
| `unit01-full-rram-backup-2026-08-30.hex` | Conversion Intel HEX du fichier ci-dessus (`objcopy -I binary -O ihex --change-address 0x0`), prêt à flasher tel quel. |

## Vérification d'intégrité effectuée

Les 117 396 premiers octets de ce dump ont été comparés (`cmp` + SHA256)
à `xiao_door_sensor/golden-image/unit01-verified-2026-08-30.bin` (la
référence déjà en usage pour cloner #02) : **identiques bit à bit**
(SHA256 `75b92e95d69396bf4715371d566a972087b59262ab7f31ca51aa2060a0703e7`
des deux côtés). Confirme que #01 n'a subi aucune dérive depuis cette
référence.

Au-delà de l'octet 117396 (fin de l'image applicative connue), le dump
contient des octets non uniformes (ni tout à 0xFF, ni tout à 0x00) —
comportement attendu de la RRAM non programmée sur ce SoC (voir la mise
en garde déjà documentée dans `Procedure-Clonage-XIAO-nRF54LM20A.md` :
« jamais `dump_image`+`cmp` pour une vérification post-flash, faux
positifs sur les trous RRAM »). Ce contenu résiduel n'a pas été
interprété individuellement — il est conservé tel quel dans la
sauvegarde intégrale par précaution.

## Procédure de restauration (si besoin de revenir à l'état actuel de #01)

**Vérifier d'abord le numéro de série avant d'écrire quoi que ce soit :**

```powershell
Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -like "*VID_2886*" } | Select-Object FriendlyName, InstanceId, Status
```
→ confirmer `C5F0E209` sur la ligne « Périphérique USB composite ».

**Flash complet (réécrit l'intégralité des 2036 Ko, pas seulement la zone
applicative) :**

```bash
export PATH="/c/ncs/tools/xpack-openocd-0.12.0-7/bin:$PATH"
BOARD_DIR="C:/ncs/vendor/platform-seeedboards/zephyr/boards/arm/xiao_nrf54lm20a"
HEX="C:/ncs/projects/nRF54LM20A/unit01-backup-2026-08-30/unit01-full-rram-backup-2026-08-30.hex"

openocd -s "$BOARD_DIR/support" -f "$BOARD_DIR/support/openocd.cfg" \
  -c "cmsis-dap vid_pid 0x2886 0x0068" -c "cmsis-dap backend hid" -c "adapter speed 500" \
  -c "init" -c "reset halt" \
  -c "nrf54lm20a-load \"$HEX\"" \
  -c "reset halt" \
  -c "verify_image \"$HEX\"" \
  -c "reset" -c "exit"
```

`verify_image` doit confirmer 2 084 864 octets. **Après ce flash,
débrancher puis rebrancher complètement l'USB-C de #01** avant de
considérer la restauration terminée (Debug Interface Mode, voir
`Configuration-nRF54LM20A-System-ON-IDLE.md`).

Alternative plus rapide si seule l'image applicative doit être restaurée
(117 396 premiers octets, suffisant dans la quasi-totalité des cas) :
utiliser directement `xiao_door_sensor/golden-image/unit01-verified-2026-08-30.hex`
(déjà vérifié identique, voir ci-dessus) au lieu du fichier complet de ce
dossier.

## Historique

| Date | Action |
|---|---|
| 2026-08-30 08:03-08:05 | Dump complet 2 084 864 octets (70,2 s), conversion .hex, vérification SHA256 contre l'image d'or existante — OK, aucune dérive. |
| 2026-08-30 08:06 | Flash du firmware de diagnostic (durée fenêtre `imu_vdd` + codes retour I2C, `trace_timing_unit01.bin` dans ce dossier) pour comparaison directe avec #02 — résultat : identique à #02 (50 ms/cycle, 0 erreur I2C). |
| 2026-08-30 08:08 | **Restauration de #01 à `xiao_door_sensor/golden-image/unit01-verified-2026-08-30.hex`, `verify_image` OK (117396 octets) — #01 revenu à son état de référence.** |
