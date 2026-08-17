#!/usr/bin/env python3
"""
Génère un discriminator + passcode + salt SPAKE2+ uniques et aléatoires pour
une unité physique XIAO, et les écrit dans un fichier de config Kconfig local
(jamais commit -- voir .gitignore) à passer au build via -DEXTRA_CONF_FILE=.

Contexte : les valeurs par défaut du SDK (discriminator 0xF00, passcode
20202021, salt fixe) sont identiques sur TOUTE compilation qui ne les
surcharge pas explicitement -- ce n'est pas un bug de génération aléatoire
cassée, juste des valeurs de développement documentées comme telles dans le
SDK (voir modules/lib/matter/config/zephyr/Kconfig), jamais surchargées
jusqu'ici dans ce projet. Sur un parc de ~25 unités, un passcode partagé
veut dire qu'une seule fuite (ex: étiquette QR d'une porte) compromet la
fenêtre de commissioning de tout le parc.

Le SPAKE2+ verifier n'a pas besoin d'être calculé ici : CONFIG_CHIP_FACTORY_DATA_GENERATE_SPAKE2_VERIFIER=y
(actif par défaut dans ce SDK) le régénère automatiquement au build à partir
du passcode/salt/iteration count configurés.

Usage :
    python3 generate_unit_secrets.py unit-03
    python3 generate_unit_secrets.py unit-03 --force   # régénère même si le fichier existe déjà

Le fichier généré (firmware/apps/lock/unit-secrets/<nom>.conf) reste en
permanence local -- ne le commit jamais, ne le colle jamais dans un fichier
suivi par git (devices/unit-XX.md, KNOWN-ISSUES.md, etc.).
"""
import argparse
import base64
import secrets
import sys
from pathlib import Path

INVALID_PASSCODES = {
    0, 11111111, 22222222, 33333333, 44444444, 55555555,
    66666666, 77777777, 88888888, 99999999, 12345678, 87654321,
}
DEFAULT_DISCRIMINATOR = 0xF00  # valeur d'exemple du SDK, à éviter

SECRETS_DIR = Path(__file__).parent / "unit-secrets"


def existing_values():
    """Lit les discriminators/passcodes déjà attribués aux autres unités pour éviter les collisions."""
    discriminators = set()
    passcodes = set()
    if SECRETS_DIR.is_dir():
        for f in SECRETS_DIR.glob("*.conf"):
            for line in f.read_text().splitlines():
                if line.startswith("CONFIG_CHIP_DEVICE_DISCRIMINATOR="):
                    discriminators.add(int(line.split("=", 1)[1], 16))
                elif line.startswith("CONFIG_CHIP_DEVICE_SPAKE2_PASSCODE="):
                    passcodes.add(int(line.split("=", 1)[1]))
    return discriminators, passcodes


def gen_discriminator(taken: set) -> int:
    while True:
        d = secrets.randbelow(4096)  # 12 bits
        if d != DEFAULT_DISCRIMINATOR and d not in taken:
            return d


def gen_passcode(taken: set) -> int:
    while True:
        p = secrets.randbelow(99999998) + 1
        if p not in INVALID_PASSCODES and p not in taken:
            return p


def gen_salt() -> str:
    return base64.b64encode(secrets.token_bytes(24)).decode()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("unit", help="Nom de l'unité, ex: unit-03")
    parser.add_argument("--force", action="store_true", help="Régénère même si le fichier existe déjà")
    args = parser.parse_args()

    SECRETS_DIR.mkdir(exist_ok=True)
    out_path = SECRETS_DIR / f"{args.unit}.conf"

    if out_path.exists() and not args.force:
        print(f"{out_path} existe déjà -- utilise --force pour régénérer (invalidera l'ancien code QR/manuel).")
        sys.exit(1)

    discriminators, passcodes = existing_values()
    discriminator = gen_discriminator(discriminators)
    passcode = gen_passcode(passcodes)
    salt = gen_salt()

    out_path.write_text(
        f"# Données de commissioning uniques pour {args.unit} -- généré par generate_unit_secrets.py\n"
        f"# NE JAMAIS COMMIT CE FICHIER (voir .gitignore) NI COLLER SES VALEURS DANS UN FICHIER SUIVI PAR GIT.\n"
        f"CONFIG_CHIP_DEVICE_DISCRIMINATOR=0x{discriminator:03X}\n"
        f"CONFIG_CHIP_DEVICE_SPAKE2_PASSCODE={passcode}\n"
        f'CONFIG_CHIP_DEVICE_SPAKE2_SALT="{salt}"\n'
    )

    print(f"Écrit : {out_path}")
    print(f"Ajouter au build : -DEXTRA_CONF_FILE={out_path}")


if __name__ == "__main__":
    main()
