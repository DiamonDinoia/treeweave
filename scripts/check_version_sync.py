#!/usr/bin/env python3
"""Fail loudly if treeweave's hand-synced version strings disagree.

The single source of truth for the release version is ``CMakeLists.txt``'s
``project(treeweave VERSION X.Y.Z)``. Three other files hard-code the same
version and must match:

- ``include/treeweave.h``                  ``TREEWEAVE_VERSION_{MAJOR,MINOR,PATCH}`` (+ ``_STRING``)
- ``include/treeweave/treeweave.hpp``      ``version_{major,minor,patch}``
- ``bindings/julia/Treeweave/Project.toml``  ``version``

The Python wheel version is derived from ``CMakeLists.txt`` by
scikit-build-core's regex metadata provider, so a green ``CMakeLists.txt`` check
also covers the wheel.

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

    cmake = _search(
        ROOT / "CMakeLists.txt", r"project\(treeweave VERSION (\d+\.\d+\.\d+)"
    ).group(1)

    hc = ROOT / "include" / "treeweave.h"
    major = _search(hc, r"#define TREEWEAVE_VERSION_MAJOR (\d+)").group(1)
    minor = _search(hc, r"#define TREEWEAVE_VERSION_MINOR (\d+)").group(1)
    patch = _search(hc, r"#define TREEWEAVE_VERSION_PATCH (\d+)").group(1)
    header_c = f"{major}.{minor}.{patch}"
    header_c_string = _search(
        hc, r'#define TREEWEAVE_VERSION_STRING "(\d+\.\d+\.\d+)"'
    ).group(1)

    hpp = ROOT / "include" / "treeweave" / "treeweave.hpp"
    cmajor = _search(hpp, r"version_major = (\d+);").group(1)
    cminor = _search(hpp, r"version_minor = (\d+);").group(1)
    cpatch = _search(hpp, r"version_patch = (\d+);").group(1)
    header_cpp = f"{cmajor}.{cminor}.{cpatch}"

    julia = _search(
        ROOT / "bindings" / "julia" / "Treeweave" / "Project.toml",
        r'^version = "(\d+\.\d+\.\d+)"',
    ).group(1)

    found = {
        "CMakeLists.txt": cmake,
        "include/treeweave.h (MAJOR/MINOR/PATCH)": header_c,
        "include/treeweave.h (_STRING)": header_c_string,
        "include/treeweave/treeweave.hpp": header_cpp,
        "bindings/julia/Treeweave/Project.toml": julia,
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
