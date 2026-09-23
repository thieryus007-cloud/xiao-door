#!/usr/bin/env python3
"""Profil fin d'une portion de capture PPK2 (export CSV), par tranches.

Sert a decomposer une salve de sondage en phases (reveil, I2C PMIC,
appel de charge du rail, attente IMU...) et a verifier ou part la charge.

Usage :
  python ppk_profile.py capture.csv T0_s T1_s BIN_us

Exemple (une salve de l'image d'or, tranches de 50 us) :
  python ppk_profile.py "mesures #01/#01-30s-ppk-20260830T044631.csv" 2.5203 2.5380 50
"""
import sys


def main():
    fn = sys.argv[1]
    t0 = float(sys.argv[2])
    t1 = float(sys.argv[3])
    bin_us = float(sys.argv[4])
    vals = []
    step = None
    t_first = None
    with open(fn) as f:
        next(f)
        for line in f:
            p = line.split(",")
            t_ms = float(p[0])
            if t_first is None:
                t_first = t_ms
            elif step is None:
                step = (t_ms - t_first) / 1000.0
            t_s = t_ms / 1000.0
            if t_s < t0:
                continue
            if t_s >= t1:
                break
            vals.append(float(p[1]))
    step = step if step is not None else 1e-5
    nb = max(1, int(round(bin_us * 1e-6 / step)))
    cum = 0.0
    for k in range(0, len(vals), nb):
        chunk = vals[k:k + nb]
        q = sum(chunk) * step
        cum += q
        print(f"{(t0 + k * step) * 1000:10.2f} ms  moy={sum(chunk) / len(chunk):9.1f} uA"
              f"  max={max(chunk):9.1f}  q={q:7.3f} uC  cumul={cum:7.3f} uC")


if __name__ == "__main__":
    main()
