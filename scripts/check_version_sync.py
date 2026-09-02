#!/usr/bin/env python3
"""Fail loudly if treeweave's hand-synced version strings disagree.

The single source of truth for the release version is the root ``VERSION`` file
(which the root ``CMakeLists.txt`` ``project()`` reads). Two other files
hard-code the same version and must match:

- ``bindings/julia/Treeweave/Project.toml``  ``version``
- ``bindings/js/package.json``               ``version``

``include/treeweave_version.h`` is not checked: it is generated from ``VERSION``
on every CMake configure and is gitignored, so it cannot drift and is absent in a
fresh checkout.

The Python wheel version is derived from the same ``VERSION`` file by
scikit-build-core's regex metadata provider, so a green ``VERSION`` check also
covers the wheel.

Usage::

    python scripts/check_version_sync.py          # all files must agree
    python scripts/check_version_sync.py X.Y.Z    # ...and equal X.Y.Z

Nonzero exit + a clear message on any mismatch.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def _search(path: Path, pattern: str) -> re.Match[str]:
    m = re.search(pattern, path.read_text(), flags=re.MULTILINE)
    if not m:
        raise SystemExit(f"error: no version match in {path} (pattern {pattern!r})")
    return m


def main() -> int:
    expected = sys.argv[1].lstrip("v") if len(sys.argv) > 1 else None

    version_file = (ROOT / "VERSION").read_text().strip()
    if not re.fullmatch(r"\d+\.\d+\.\d+", version_file):
        raise SystemExit(
            f"error: VERSION must be MAJOR.MINOR.PATCH, got: {version_file!r}"
        )

    julia = _search(
        ROOT / "bindings" / "julia" / "Treeweave" / "Project.toml",
        r'^version = "(\d+\.\d+\.\d+)"',
    ).group(1)

    js = _search(
        ROOT / "bindings" / "js" / "package.json",
        r'"version":\s*"(\d+\.\d+\.\d+)"',
    ).group(1)

    found = {
        "VERSION": version_file,
        "bindings/julia/Treeweave/Project.toml": julia,
        "bindings/js/package.json": js,
    }

    versions = set(found.values())
    if len(versions) != 1:
        print("error: version strings disagree:", file=sys.stderr)
        for name, v in found.items():
            print(f"  {name}: {v}", file=sys.stderr)
        return 1

    canonical = versions.pop()
    if expected is not None and canonical != expected:
        print(
            f"error: version is {canonical}, expected {expected}",
            file=sys.stderr,
        )
        return 1

    print(f"version sync OK: {canonical}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
