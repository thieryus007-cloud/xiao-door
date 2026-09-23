#!/usr/bin/env python3
"""Analyse d'une capture PPK2 (export CSV) du firmware xiao_door_sensor.

Decoupe la capture en salves (courant > seuil) et separe :
  - les salves periodiques de sondage IMU (une par cycle de ~1 s),
  - les autres evenements (advertising BLE, trame sante, demarrage...),
  - le plancher entre salves.

Sortie : moyenne globale, plancher, charge par cycle (moyenne/mediane),
periode, et moyenne "regime etabli" = plancher + charge_moyenne/periode.
C'est cette derniere valeur (et la charge par cycle en uC) qui sert a
comparer deux firmwares : elle ne depend pas de la presence fortuite
d'une trame BLE dans la fenetre de capture.

Usage :
  python ppk_cycle_stats.py capture.csv [--thr 30] [--gap-ms 2]
                            [--min-ms 3] [--max-ms 60] [--start-s 0] [--end-s 1e9]

Format attendu (export PPK2) : "Timestamp(ms),Current(uA),D0-D7".
Bibliotheque standard uniquement (pas de numpy), ~6 s pour 7 M lignes.
"""
import argparse
import statistics


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("csv")
    ap.add_argument("--thr", type=float, default=30.0,
                    help="seuil (uA) au-dessus duquel un echantillon est 'actif'")
    ap.add_argument("--gap-ms", type=float, default=2.0,
                    help="duree sous le seuil (ms) qui clot une salve")
    ap.add_argument("--min-ms", type=float, default=3.0,
                    help="duree min (ms) d'une salve de sondage")
    ap.add_argument("--max-ms", type=float, default=60.0,
                    help="duree max (ms) d'une salve de sondage")
    ap.add_argument("--start-s", type=float, default=0.0)
    ap.add_argument("--end-s", type=float, default=1e9)
    args = ap.parse_args()

    dt = None
    t_prev = None
    n = 0
    total = 0.0
    bursts = []  # (t_start_s, dur_ms, charge_uC, peak_uA)
    in_b = False
    below = 0
    b_start = 0.0
    b_q = 0.0
    b_pk = 0.0
    b_last_active = 0.0
    floor_sum = 0.0
    floor_n = 0

    with open(args.csv) as f:
        next(f)
        for line in f:
            p = line.split(",")
            t_ms = float(p[0])
            if dt is None:
                if t_prev is not None:
                    dt = (t_ms - t_prev) / 1000.0
                t_prev = t_ms
            t_s = t_ms / 1000.0
            if t_s < args.start_s:
                continue
            if t_s > args.end_s:
                break
            c = float(p[1])
            n += 1
            total += c
            step = dt if dt is not None else 1e-5
            gap_samples = int(args.gap_ms / 1000.0 / step)
            if c > args.thr:
                if not in_b:
                    in_b = True
                    b_start = t_s
                    b_q = 0.0
                    b_pk = 0.0
                below = 0
                b_last_active = t_s
            if in_b:
                b_q += c * step
                b_pk = max(b_pk, c)
                if c <= args.thr:
                    below += 1
                    if below >= gap_samples:
                        # retire la queue sous le seuil comptee apres la fin reelle
                        bursts.append((b_start, (b_last_active - b_start) * 1000.0, b_q, b_pk))
                        in_b = False
            else:
                floor_sum += c
                floor_n += 1

    step = dt if dt is not None else 1e-5
    dur_s = n * step
    print(f"fenetre            : {dur_s:.2f} s ({n} echantillons, pas {step*1e6:.1f} us)")
    print(f"moyenne globale    : {total / max(n, 1):.3f} uA  (charge {total * step:.1f} uC)")
    floor = floor_sum / max(floor_n, 1)
    print(f"plancher (hors salves, seuil {args.thr} uA) : {floor:.3f} uA sur {floor_n * step:.2f} s")

    # Une salve de sondage est periodique (~1 s) : on garde celles qui ont
    # une voisine a +/- une periode (tolerance 30 ms). Les evenements
    # d'advertising (~3-4 ms tous les ~100 ms) et les trames ponctuelles
    # sont ainsi exclus meme si leur duree tombe dans [min-ms, max-ms].
    cand = [b for b in bursts if args.min_ms <= b[1] <= args.max_ms]
    gaps = [cand[i + 1][0] - cand[i][0] for i in range(len(cand) - 1)]
    gaps = [g for g in gaps if 0.8 <= g <= 1.3]
    per_est = statistics.median(gaps) if gaps else 1.0
    starts = [b[0] for b in cand]

    def periodic(t):
        return any(abs(abs(u - t) - per_est) < 0.03 for u in starts if u != t)

    polls = [b for b in cand if periodic(b[0])]
    # Deuxieme filtre : duree coherente avec la salve de sondage type
    # (un evenement d'advertising peut tomber par hasard a ~1 s d'un autre).
    if polls:
        d_med = statistics.median([b[1] for b in polls])
        polls = [b for b in polls if 0.6 * d_med <= b[1] <= 1.6 * d_med]
    others = [b for b in bursts if b not in polls]
    print(f"salves detectees   : {len(bursts)} (sondage {len(polls)}, autres {len(others)})")
    if len(polls) >= 2:
        qs = [b[2] for b in polls]
        ds = [b[1] for b in polls]
        periods = [polls[i + 1][0] - polls[i][0] for i in range(len(polls) - 1)]
        periods = [p for p in periods if p < 1.5]  # ignore les trous (evenement intercale)
        period = statistics.median(periods) if periods else 1.0
        q_mean = statistics.mean(qs)
        print(f"charge/cycle       : moyenne {q_mean:.2f} uC, mediane {statistics.median(qs):.2f} uC,"
              f" min {min(qs):.2f}, max {max(qs):.2f}")
        print(f"duree salve        : mediane {statistics.median(ds):.2f} ms")
        print(f"periode            : mediane {period * 1000:.1f} ms")
        print(f"REGIME ETABLI      : {floor + q_mean / period:.2f} uA"
              f"  (= plancher {floor:.2f} + {q_mean:.2f} uC / {period:.4f} s)")
    if others:
        print("autres evenements  :")
        for b in others[:40]:
            print(f"  t={b[0]:8.3f} s  duree={b[1]:8.2f} ms  q={b[2]:8.2f} uC  pic={b[3]:9.1f} uA")


if __name__ == "__main__":
    main()
