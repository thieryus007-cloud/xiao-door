#!/usr/bin/env bash
# flash-unit.sh -- Etape 2/2 du deploiement en lot. Flashe l'image d'or
# generique sur l'unite branchee, MAIS SEULEMENT si le numero de serie lu
# maintenant correspond EXACTEMENT a celui passe en argument (obtenu via
# check-unit.sh juste avant) -- interdit tout flash "a l'aveugle" et
# rattrape le cas ou une autre carte aurait ete branchee entre les deux
# etapes. Voir C:\ncs\CLAUDE.md, regle absolue.
#
# Usage : ./flash-unit.sh <etiquette> <serial_attendu>
# Exemple : ./flash-unit.sh unit04 A1B2C3D4
set -euo pipefail

LABEL="${1:?Usage: flash-unit.sh <etiquette> <serial_attendu>}"
EXPECTED_SERIAL="${2:?Usage: flash-unit.sh <etiquette> <serial_attendu>}"

OPENOCD_BIN="/c/ncs/tools/xpack-openocd-0.12.0-7/bin"
BOARD_DIR="C:/ncs/vendor/platform-seeedboards/zephyr/boards/arm/xiao_nrf54lm20a"
GOLDEN_HEX="C:/ncs/projects/nRF54LM20A/xiao_door_sensor/golden-image/unit01-verified-2026-09-23-ABD-VBUSFIX-15uA.hex"
LOG_FILE="$(dirname "$0")/deployment-log.csv"

FORBIDDEN_SERIALS=("C5F0E209" "9C4A557D" "4587B5C1")

export PATH="$OPENOCD_BIN:$PATH"

# Voir check-unit.sh pour l'explication de ce contournement (pont SAMD11
# parfois intermittent, sans rapport avec le numero de serie lui-meme).
run_openocd_readonly() {
	set +e
	openocd -s "$BOARD_DIR/support" -f "$BOARD_DIR/support/openocd.cfg" \
	  -c "cmsis-dap vid_pid 0x2886 0x0068" -c "cmsis-dap backend hid" -c "adapter speed 500" \
	  -c "init" -c "exit" 2>&1
	set -e
}

echo "=== Re-verification du numero de serie (obligatoire, independante de check-unit.sh) ==="
SERIAL=""
for attempt in 1 2 3 4 5; do
	RAW=$(run_openocd_readonly)
	SERIAL=$(echo "$RAW" | grep "CMSIS-DAP: Serial#" | sed -E 's/.*Serial# = ([A-Za-z0-9]+).*/\1/' || true)
	if [ -n "$SERIAL" ]; then
		break
	fi
	echo "(tentative $attempt/5 : pont SWD intermittent, connu du projet -- nouvel essai)"
done

if [ -z "$SERIAL" ]; then
	echo "ERREUR : impossible de lire un numero de serie apres 5 tentatives. Rien flashe."
	exit 1
fi

for f in "${FORBIDDEN_SERIALS[@]}"; do
	if [ "$SERIAL" == "$f" ]; then
		echo "!!! ARRET : $SERIAL correspond a une unite de PRODUCTION existante (#01/#02/#03). Rien flashe."
		exit 1
	fi
done

if [ "$SERIAL" != "$EXPECTED_SERIAL" ]; then
	echo "!!! ARRET : le numero de serie lu maintenant ($SERIAL) ne correspond PAS"
	echo "!!! au numero attendu ($EXPECTED_SERIAL, passe en argument)."
	echo "!!! Une autre carte a probablement ete branchee entre check-unit.sh et flash-unit.sh."
	echo "!!! Rien flashe -- relancer check-unit.sh pour cette carte."
	exit 1
fi

echo "Serial confirme : $SERIAL == $EXPECTED_SERIAL (attendu) -- OK"
echo ""
echo "=== Flash de l'image d'or ($GOLDEN_HEX) sur '$LABEL' (S/N $SERIAL) ==="

# Le pont SAMD11 echoue parfois a la connexion DP ("cannot read IDR")
# avant meme d'ecrire quoi que ce soit -- symptome deja documente sur ce
# projet, se resout systematiquement en relancant la meme commande
# (jusqu'a 5-10 essais). Retente donc le flash complet plutot que
# d'echouer sur un hoquet materiel connu et benin.
RESULT=""
for attempt in 1 2 3 4 5; do
	set +e
	RESULT=$(openocd -s "$BOARD_DIR/support" -f "$BOARD_DIR/support/openocd.cfg" \
	  -c "cmsis-dap vid_pid 0x2886 0x0068" -c "cmsis-dap backend hid" -c "adapter speed 500" \
	  -c "init" -c "reset halt" \
	  -c "nrf54lm20a-load \"$GOLDEN_HEX\"" \
	  -c "reset halt" \
	  -c "verify_image \"$GOLDEN_HEX\"" \
	  -c "reset" -c "exit" 2>&1)
	set -e
	if echo "$RESULT" | grep -q "^verified"; then
		break
	fi
	echo "(tentative $attempt/5 : echec flash/verify, pont SWD intermittent connu -- nouvel essai)"
done

echo "$RESULT"

if echo "$RESULT" | grep -q "^verified"; then
	STATUS="verify_image OK"
else
	STATUS="ECHEC verify_image -- NE PAS DEPLOYER, reflasher ou investiguer"
fi

TIMESTAMP=$(date -u +"%Y-%m-%dT%H:%M:%SZ")
if [ ! -f "$LOG_FILE" ]; then
	echo "timestamp,label,serial,image,status" > "$LOG_FILE"
fi
echo "${TIMESTAMP},${LABEL},${SERIAL},$(basename "$GOLDEN_HEX"),${STATUS}" >> "$LOG_FILE"

echo ""
echo "=== Resultat : $STATUS ==="
echo "Journal : $LOG_FILE"
echo ""
echo "Etapes manuelles suivantes :"
echo "  1. Debrancher puis rebrancher completement l'USB-C (sortir du Debug Interface Mode)."
echo "  2. Verifier au moins la presence des trames BTHome dans Home Assistant."
echo "  3. (Recommande sur un sous-echantillon, pas obligatoire sur les 10) mesure PPK2 ~20-22 uA."
