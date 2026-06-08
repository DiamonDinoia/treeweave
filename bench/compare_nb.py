#!/usr/bin/env python3
"""Compare two nanobench runs side-by-side.

Parses the markdown-table output (one cell per row, last column is the
backtick-wrapped name) and aligns by name. Prints A vs B in MEvals/s and
flags cells whose MdAPE is high enough to make the comparison weak.

Usage: ./compare_nb.py baseline_nanobench.txt phase2_nanobench.txt
"""
import re
import sys
from pathlib import Path


# Match a nanobench result row. Columns are pipe-separated; the last cell
# carries the test name in backticks, optionally prefixed by a wavy-dash
# instability marker.
ROW = re.compile(
    r"\|\s*([\d.,]+%?)?\s*\|\s*([\d.,]+)\s*\|\s*([\d.,]+)\s*\|\s*([\d.]+)%\s*\|"
    r".*?`([^`]+)`"
)


def parse(path):
    out = {}
    for line in Path(path).read_text().splitlines():
        m = ROW.search(line)
        if not m:
            continue
        _rel, _ns, evals, err, name = m.groups()
        out[name] = (float(evals.replace(",", "")), float(err))
    return out


def main():
    a = parse(sys.argv[1])
    b = parse(sys.argv[2])
    print(f"# A = {sys.argv[1]}    B = {sys.argv[2]}")
    print(f"{'cell':<40s}  {'A MEvals/s':>11s}  {'A err%':>6s}  {'B MEvals/s':>11s}  {'B err%':>6s}  {'B/A':>6s}")
    for name in sorted(set(a) & set(b)):
        ame, aerr = a[name]
        bme, berr = b[name]
        ame_m = ame / 1e6
        bme_m = bme / 1e6
        ratio = bme / ame if ame > 0 else float("nan")
        flag = " !" if max(aerr, berr) > 5.0 else ""
        print(f"{name:<40s}  {ame_m:>11.2f}  {aerr:>5.1f}%  {bme_m:>11.2f}  {berr:>5.1f}%  {ratio:>6.2f}{flag}")


if __name__ == "__main__":
    main()
