#!/bin/bash
# Active l'environnement nRF Connect SDK (toolchain + west) pour ce projet.
# Usage : source firmware/build-env.sh

NCS_TOOLCHAIN_ID="322ac893fe"
NCS_TOOLCHAIN_ROOT="/opt/nordic/ncs/toolchains/${NCS_TOOLCHAIN_ID}"

export PATH="${NCS_TOOLCHAIN_ROOT}/bin:${NCS_TOOLCHAIN_ROOT}/usr/bin:${NCS_TOOLCHAIN_ROOT}/usr/local/bin:${NCS_TOOLCHAIN_ROOT}/opt/bin:${NCS_TOOLCHAIN_ROOT}/opt/nanopb/generator-bin:${NCS_TOOLCHAIN_ROOT}/nrfutil/bin:${NCS_TOOLCHAIN_ROOT}/opt/zephyr-sdk/arm-zephyr-eabi/bin:${NCS_TOOLCHAIN_ROOT}/opt/zephyr-sdk/riscv64-zephyr-elf/bin:$PATH"
export NRFUTIL_HOME="${NCS_TOOLCHAIN_ROOT}/nrfutil/home"
export ZEPHYR_SDK_INSTALL_DIR="${NCS_TOOLCHAIN_ROOT}/opt/zephyr-sdk"
export ZEPHYR_TOOLCHAIN_VARIANT="zephyr"
export ZEPHYR_BASE="/opt/nordic/ncs/v3.2.1/zephyr"

# openocd (Homebrew) est requis pour le flash — la sonde CMSIS-DAP du XIAO
# n'est pas reconnue par le runner "nrfutil" (filtré sur les sondes J-Link).
export PATH="/opt/homebrew/bin:$PATH"

echo "nRF Connect SDK v3.2.1 environment activated (toolchain ${NCS_TOOLCHAIN_ID})"
west --version
echo "Rappel flash : west flash -d <build-dir> --runner openocd"
