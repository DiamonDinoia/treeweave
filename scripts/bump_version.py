#!/usr/bin/env python3
"""Bump the treeweave release version across every hand-synced file.

Usage::

    python scripts/bump_version.py X.Y.Z

The single source of truth is ``CMakeLists.txt``'s ``project(treeweave VERSION
...)``, but three other files hard-code the same version and must move in
lockstep:

- ``include/treeweave.h``                  ``TREEWEAVE_VERSION_{MAJOR,MINOR,PATCH}`` + ``_STRING``
- ``include/treeweave/treeweave.hpp``      ``version_{major,minor,patch}``
- ``bindings/julia/Treeweave/Project.toml``  ``version = "X.Y.Z"``

The Python wheel version is NOT edited here: scikit-build-core reads it from
``CMakeLists.txt`` via its regex metadata provider, so bumping CMake covers the
wheel.

Each substitution must match exactly once or the script aborts (fail-loud, like
``.github/scripts/set_dev_version.py``). Re-running with the same version is a
no-op, so it is safe to invoke idempotently.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CMAKELISTS = ROOT / "CMakeLists.txt"
HEADER_C = ROOT / "include" / "treeweave.h"
HEADER_CPP = ROOT / "include" / "treeweave" / "treeweave.hpp"
JULIA_PROJECT = ROOT / "bindings" / "julia" / "Treeweave" / "Project.toml"


def _sub_once(path: Path, pattern: str, repl: str, text: str) -> str:
    new, n = re.subn(pattern, repl, text, count=1, flags=re.MULTILINE)
    if n != 1:
        raise SystemExit(
            f"error: pattern {pattern!r} matched {n} time(s) in {path} (expected 1)"
        )
    return new


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} X.Y.Z", file=sys.stderr)
        return 2
    version = sys.argv[1].lstrip("v")
    m = re.fullmatch(r"(\d+)\.(\d+)\.(\d+)", version)
    if not m:
        print(f"invalid version (want numeric X.Y.Z): {version!r}", file=sys.stderr)
        return 2
    major, minor, patch = m.groups()

    # CMakeLists.txt — the source of truth.
    text = CMAKELISTS.read_text()
    text = _sub_once(
        CMAKELISTS,
        r"project\(treeweave VERSION \d+\.\d+\.\d+",
        f"project(treeweave VERSION {version}",
        text,
    )
    CMAKELISTS.write_text(text)

    # include/treeweave.h — version macros.
    text = HEADER_C.read_text()
    text = _sub_once(
        HEADER_C,
        r"#define TREEWEAVE_VERSION_MAJOR \d+",
        f"#define TREEWEAVE_VERSION_MAJOR {major}",
        text,
    )
    text = _sub_once(
        HEADER_C,
        r"#define TREEWEAVE_VERSION_MINOR \d+",
        f"#define TREEWEAVE_VERSION_MINOR {minor}",
        text,
    )
    text = _sub_once(
        HEADER_C,
        r"#define TREEWEAVE_VERSION_PATCH \d+",
        f"#define TREEWEAVE_VERSION_PATCH {patch}",
        text,
    )
    text = _sub_once(
        HEADER_C,
        r'#define TREEWEAVE_VERSION_STRING "\d+\.\d+\.\d+"',
        f'#define TREEWEAVE_VERSION_STRING "{version}"',
        text,
    )
    HEADER_C.write_text(text)

    # include/treeweave/treeweave.hpp — constexpr ints.
    text = HEADER_CPP.read_text()
    text = _sub_once(
        HEADER_CPP,
        r"inline constexpr int version_major = \d+;",
        f"inline constexpr int version_major = {major};",
        text,
    )
    text = _sub_once(
        HEADER_CPP,
        r"inline constexpr int version_minor = \d+;",
        f"inline constexpr int version_minor = {minor};",
        text,
    )
    text = _sub_once(
        HEADER_CPP,
        r"inline constexpr int version_patch = \d+;",
        f"inline constexpr int version_patch = {patch};",
        text,
    )
    HEADER_CPP.write_text(text)

    # bindings/julia/Treeweave/Project.toml — package version.
    text = JULIA_PROJECT.read_text()
    text = _sub_once(
        JULIA_PROJECT,
        r'^version = "\d+\.\d+\.\d+"',
        f'version = "{version}"',
        text,
    )
    JULIA_PROJECT.write_text(text)

    print(
        f"bumped treeweave version to {version} across CMakeLists.txt, "
        "include/treeweave.h, include/treeweave/treeweave.hpp, "
        "bindings/julia/Treeweave/Project.toml"
    )
    print(f"next: git commit -am 'chore(release): bump version to {version}'")
    return 0


if __name__ == "__main__":
    sys.exit(main())
