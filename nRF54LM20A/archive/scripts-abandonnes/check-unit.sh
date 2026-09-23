#!/usr/bin/env bash
# check-unit.sh -- Etape 1/2 du deploiement en lot. Lecture SEULE du
# numero de serie du pont SWD de l'unite actuellement branchee. Aucune
# ecriture, aucun flash. A lancer AVANT flash-unit.sh, systematiquement,
# meme si une seule carte est branchee -- voir C:\ncs\CLAUDE.md, regle
# absolue "verifier le numero de serie SWD avant TOUT flash".
#
# Usage : ./check-unit.sh
set -euo pipefail

OPENOCD_BIN="/c/ncs/tools/xpack-openocd-0.12.0-7/bin"
BOARD_DIR="C:/ncs/vendor/platform-seeedboards/zephyr/boards/arm/xiao_nrf54lm20a"
LOG_FILE="$(dirname "$0")/deployment-log.csv"

# Numeros de serie de production existants -- JAMAIS reflasher ces
# unites avec l'image d'or generique de ce lot (voir
# Configuration-nRF54LM20A-System-ON-IDLE.md §7).
FORBIDDEN_SERIALS=("C5F0E209" "9C4A557D" "4587B5C1")

export PATH="$OPENOCD_BIN:$PATH"

# Le pont SAMD11 est parfois intermittent (connexion DP echoue,
# "cannot read IDR") sans rapport avec le numero de serie lui-meme, deja
# lu avec succes avant cet echec -- documente dans ce projet, se resout
# en relancant, jusqu'a 5-10 essais. set -e desactive ici pour ne pas
# planter sur le code de sortie d'OpenOCD, la seule chose qui compte est
# de trouver la ligne "Serial#" dans la sortie.
run_openocd_readonly() {
	set +e
	openocd -s "$BOARD_DIR/support" -f "$BOARD_DIR/support/openocd.cfg" \
	  -c "cmsis-dap vid_pid 0x2886 0x0068" -c "cmsis-dap backend hid" -c "adapter speed 500" \
	  -c "init" -c "exit" 2>&1
	set -e
}

echo "=== Lecture du numero de serie (lecture seule, aucune ecriture) ==="
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
	echo "ERREUR : impossible de lire un numero de serie apres 5 tentatives. Sortie OpenOCD :"
	echo "$RAW"
	exit 1
fi

echo ""
echo "SERIAL DETECTE : $SERIAL"
echo ""

for f in "${FORBIDDEN_SERIALS[@]}"; do
	if [ "$SERIAL" == "$f" ]; then
		echo "!!! ARRET : ce numero de serie correspond a une unite de PRODUCTION existante (#01/#02/#03)."
		echo "!!! Ne JAMAIS flasher cette carte avec l'image de lot generique."
		exit 1
	fi
done

if [ -f "$LOG_FILE" ] && grep -q ",${SERIAL}," "$LOG_FILE" 2>/dev/null; then
	echo "ATTENTION : ce numero de serie est deja present dans le journal de deploiement :"
	grep ",${SERIAL}," "$LOG_FILE"
	echo "Cette unite a peut-etre deja ete flashee lors d'un passage precedent."
fi

echo "Numero de serie propre (pas #01/#02/#03, format attendu pour un pont SAMD11)."
echo ""
echo "Etape suivante : ./flash-unit.sh <etiquette, ex: unit04> $SERIAL"
