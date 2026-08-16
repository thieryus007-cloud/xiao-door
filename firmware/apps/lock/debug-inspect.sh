#!/bin/bash
# Inspecte l'état live du firmware Matter via GDB, sans RTT/UART (qui ne
# fonctionnent pas de façon fiable sur cette carte/sonde — connexions
# systématiquement coupées, voir KNOWN-ISSUES.md).
#
# Fonctionne en mode "instantané" : connexion GDB courte (attach, lire,
# detach) plutôt qu'une session continue — c'est la seule approche fiable
# trouvée avec cette sonde CMSIS-DAP + OpenOCD sur ce SoC.
#
# Usage : ./debug-inspect.sh <chemin-vers-zephyr.elf>
# Exemple : ./debug-inspect.sh /tmp/build-lock/lock/zephyr/zephyr.elf

set -e
ELF="${1:?Usage: $0 <chemin-vers-zephyr.elf>}"
BOARD_SUPPORT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../boards/xiao_nrf54lm20a" && pwd)"
GDB="/opt/nordic/ncs/toolchains/322ac893fe/opt/zephyr-sdk/arm-zephyr-eabi/bin/arm-zephyr-eabi-gdb"

pkill -f openocd 2>/dev/null || true
sleep 1

/opt/homebrew/bin/openocd \
  -f "${BOARD_SUPPORT_DIR}/support/openocd.cfg" \
  -c "init" > /tmp/openocd-gdb.log 2>&1 &
sleep 3

"$GDB" -batch \
  -ex "set pagination off" \
  -ex "set print pretty on" \
  -ex "target extended-remote localhost:3333" \
  -ex "print bt_dev.flags" \
  -ex "print bt_dev.hci_version" \
  -ex "print 'chip::DeviceLayer::Internal::BLEManagerImpl::sInstance'.mFlags" \
  -ex "print 'chip::DeviceLayer::Internal::BLEManagerImpl::sInstance'.mServiceMode" \
  -ex "print 'chip::DeviceLayer::PlatformManagerImpl::sInstance'.chip::DeviceLayer::PlatformManager::mInitialized" \
  -ex "print 'chip::DeviceLayer::PlatformManagerImpl::sInstance'.mChipThreadStack" \
  -ex "detach" \
  "$ELF" 2>&1

pkill -f openocd 2>/dev/null || true
