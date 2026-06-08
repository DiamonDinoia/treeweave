#!/usr/bin/env python3
"""Compare two treeweave_microbench runs side-by-side.

Usage: ./compare.py A_baseline.txt B_phase2.txt
"""
import re
import sys
from pathlib import Path

ROW = re.compile(
    r"(\S+)\s+deg=\s*(\d+)\s+dim=(\d+)\s+N=(\d+)\s+([\d.]+)\s+MEvals/s\s+([\d.]+)\s+cyc/pt"
)


def parse(path):
    out = {}
    for line in Path(path).read_text().splitlines():
        m = ROW.match(line.strip())
        if not m:
            continue
        label, deg, dim, n, me, cyc = m.groups()
        out[(label, int(deg), int(dim), int(n))] = (float(me), float(cyc))
    return out


def main():
    a = parse(sys.argv[1])
    b = parse(sys.argv[2])
    print(f"# A = {sys.argv[1]}    B = {sys.argv[2]}")
    print(f"{'cell':<40s}  {'A MEvals/s':>11s}  {'B MEvals/s':>11s}  {'B/A':>6s}  {'A cyc':>7s}  {'B cyc':>7s}")
    keys = sorted(set(a) & set(b))
    for k in keys:
        ame, acyc = a[k]
        bme, bcyc = b[k]
        ratio = bme / ame if ame > 0 else float("nan")
        label = f"{k[0]:<14s} deg={k[1]:>2d} dim={k[2]} N={k[3]:<8d}"
        print(f"{label:<40s}  {ame:>11.2f}  {bme:>11.2f}  {ratio:>6.2f}  {acyc:>7.2f}  {bcyc:>7.2f}")


if __name__ == "__main__":
    main()
