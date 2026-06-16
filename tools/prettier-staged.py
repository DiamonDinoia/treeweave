#!/usr/bin/env python3
"""Format staged JS/TS hunks only by forwarding char ranges to prettier.

Mirrors tools/gersemi-staged.py, but prettier takes char offsets
(--range-start/--range-end) and only one range per invocation, so each staged
hunk is converted from 1-based inclusive line ranges to char offsets and run
separately. Hunks are processed bottom-to-top, recomputing offsets each time, so
reformatting one hunk never invalidates the offsets of the hunks above it.
"""

from __future__ import annotations

import os
import subprocess
import sys

from staged_ranges import get_staged_line_ranges
from staged_ranges import git_root
from staged_ranges import repo_relative_paths


def line_offsets(text: str) -> list[int]:
    """Char offset of the start of each 1-based line (index 0 unused/=0)."""
    offsets = [0, 0]
    for line in text.splitlines(keepends=True):
        offsets.append(offsets[-1] + len(line))
    return offsets


def prettier_bin() -> str:
    return os.path.join(git_root(), "bindings", "js", "node_modules", ".bin", "prettier")


def main() -> int:
    paths = sys.argv[1:]
    ranges_by_path = get_staged_line_ranges(paths)
    prettier = prettier_bin()
    modified = []

    for path, relpath in zip(paths, repo_relative_paths(paths)):
        ranges = ranges_by_path.get(relpath, [])
        if not ranges:
            continue

        before = open(path, "rb").read()

        # Bottom-to-top so reformatting a hunk can't shift offsets above it.
        for line_start, line_end in sorted(ranges, reverse=True):
            text = open(path, encoding="utf-8").read()
            offsets = line_offsets(text)
            start = offsets[line_start]
            end = min(offsets[line_end + 1], len(text))
            result = subprocess.run(
                [
                    prettier,
                    "--range-start",
                    str(start),
                    "--range-end",
                    str(end),
                    "--write",
                    path,
                ],
                capture_output=True,
                text=True,
            )
            if result.returncode != 0:
                output = (result.stdout + result.stderr).strip()
                if output:
                    print(output, file=sys.stderr)
                return result.returncode

        after = open(path, "rb").read()
        if after != before:
            modified.append(path)

    if modified:
        for path in modified:
            print(f"reformatted staged JS/TS hunks in {path}")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
