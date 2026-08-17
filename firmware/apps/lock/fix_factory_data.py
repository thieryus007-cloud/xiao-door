#!/usr/bin/env python3
"""
Contourne un bug de génération des factory data Matter du nRF Connect SDK
v3.2.1 (voir KNOWN-ISSUES.md) qui empêche le firmware de démarrer le stack
Matter/BLE.

Symptôme : le firmware compile et flashe, mais `PlatformMgr().InitChipStack()`
s'exécute puis `DoInitChipServer()` (matter_init.cpp) échoue silencieusement
avec CHIP_ERROR_PERSISTED_STORAGE_VALUE_NOT_FOUND avant d'atteindre
`Server::Init()` -> aucune fenêtre de commissioning BLE ne s'ouvre jamais.

Cause : `generate_factory_data.cmake` ajoute toujours `--product_finish`
(CONFIG_CHIP_DEVICE_PRODUCT_FINISH, défaut "other") comme dernier argument,
et le dernier champ du blob CBOR généré échoue systématiquement à se décoder
sur cible (`ParseFactoryData()` dans FactoryDataParser.c, confirmé par
breakpoints matériels sur zcbor_list_map_end_force_decode jamais atteint).
Vérifié en supprimant ce champ : le stack s'initialise alors correctement
(CHIP_NO_ERROR) et l'appareil devient visible en BLE ("MatterLock").

Ce script prend le factory_data.bin déjà généré par le build normal,
retire l'entrée "product_finish" du CBOR, et réécrit ce blob corrigé
directement dans merged.hex (et factory_data.hex/.bin) à sa place.

Usage :
    python3 fix_factory_data.py <build_dir>

<build_dir> est le dossier passé à `west build -d` / `west flash -d`
(ex: /tmp/build-lock). Le script modifie merged.hex, factory_data.hex et
factory_data.bin dans <build_dir>/lock/zephyr/ et <build_dir>/ en place —
à lancer après `west build`, avant `west flash`.
"""
import sys
import re
from pathlib import Path

import cbor2
from intelhex import IntelHex

REMOVED_KEYS = ["product_finish"]


def find_factory_data_partition(build_dir: Path):
    """Lit l'adresse et la taille de la partition factory_data depuis pm_config.h."""
    pm_config = build_dir / "lock" / "zephyr" / "include" / "generated" / "pm_config.h"
    text = pm_config.read_text()
    addr = int(re.search(r"#define PM_FACTORY_DATA_ADDRESS (0x[0-9a-fA-F]+)", text).group(1), 16)
    size = int(re.search(r"#define PM_FACTORY_DATA_SIZE (0x[0-9a-fA-F]+)", text).group(1), 16)
    return addr, size


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(1)

    build_dir = Path(sys.argv[1])
    zephyr_dir = build_dir / "lock" / "zephyr"
    factory_data_bin = zephyr_dir / "factory_data.bin"
    merged_hex = build_dir / "merged.hex"

    if not factory_data_bin.exists():
        print(f"Introuvable : {factory_data_bin} (as-tu lancé `west build` avec CONFIG_CHIP_FACTORY_DATA=y ?)")
        sys.exit(1)

    addr, size = find_factory_data_partition(build_dir)

    original = factory_data_bin.read_bytes()
    data = cbor2.loads(original)

    removed = [k for k in REMOVED_KEYS if k in data]
    if not removed:
        print(f"Aucune des clés {REMOVED_KEYS} trouvée dans le CBOR — rien à corriger (déjà appliqué ?).")
        sys.exit(0)

    for key in removed:
        del data[key]

    fixed = cbor2.dumps(data)
    print(f"factory_data.bin : {len(original)} -> {len(fixed)} octets (retiré : {removed})")

    if len(fixed) > size:
        print(f"ERREUR : le blob corrigé ({len(fixed)} octets) dépasse la partition ({size} octets)")
        sys.exit(1)

    # Réécrit factory_data.bin/.hex pour rester cohérent avec le reste du build
    factory_data_bin.write_bytes(fixed)
    ih_partition = IntelHex()
    ih_partition.puts(addr, fixed)
    ih_partition.write_hex_file(str(zephyr_dir / "factory_data.hex"), True)

    # Réécrit merged.hex : efface toute la partition puis réécrit le blob corrigé,
    # important car la RRAM interne ne s'efface pas automatiquement à l'écriture
    # (contrairement à de la NOR flash) - un ancien octet resterait sinon.
    ih_merged = IntelHex(str(merged_hex))
    ih_merged.puts(addr, bytes([0xFF]) * size)
    ih_merged.puts(addr, fixed)
    ih_merged.write_hex_file(str(merged_hex), True)

    print(f"OK : {merged_hex} corrigé (partition factory_data à {hex(addr)}, {size} octets).")


if __name__ == "__main__":
    main()
