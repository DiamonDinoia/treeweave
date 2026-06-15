#!/usr/bin/env python3
"""Bump the treeweave release version across every version-bearing file.

Usage::

    python scripts/bump_version.py X.Y.Z

The single source of truth is the root ``VERSION`` file: the root
``CMakeLists.txt`` ``project()`` reads it, and the Python wheel version is
derived from it by scikit-build-core's regex metadata provider — so neither is
edited here. The remaining files carry their own copy and must move in lockstep:

- ``include/treeweave_version.h``            ``TREEWEAVE_VERSION_{MAJOR,MINOR,PATCH}`` + ``_STRING``
- ``bindings/julia/Treeweave/Project.toml``  ``version = "X.Y.Z"``
- ``bindings/js/package.json``               ``"version": "X.Y.Z"``

``treeweave_version.h`` is normally regenerated from ``VERSION`` by CMake
(``cmake/treeweave_generate_version.cmake``); its macros are updated here too so
the committed copy is in sync immediately (its ``TREEWEAVE_VERSION_FULL`` -dev.N
suffix is recomputed from git on the next configure / pre-commit run). The C and
C++ headers (treeweave.h / treeweave.hpp) ``#include`` it and need no edit.

Each substitution must match exactly once or the script aborts (fail-loud, like
``.github/scripts/set_dev_version.py``). Re-running with the same version is a
no-op, so it is safe to invoke idempotently.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
VERSION_FILE = ROOT / "VERSION"
HEADER_VERSION = ROOT / "include" / "treeweave_version.h"
JULIA_PROJECT = ROOT / "bindings" / "julia" / "Treeweave" / "Project.toml"
JS_PACKAGE = ROOT / "bindings" / "js" / "package.json"


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

    # VERSION — the single source of truth.
    VERSION_FILE.write_text(f"{version}\n")

    # include/treeweave_version.h — committed generated header (macros only).
    text = HEADER_VERSION.read_text()
    text = _sub_once(
        HEADER_VERSION,
        r"#define TREEWEAVE_VERSION_MAJOR \d+",
        f"#define TREEWEAVE_VERSION_MAJOR {major}",
        text,
    )
    text = _sub_once(
        HEADER_VERSION,
        r"#define TREEWEAVE_VERSION_MINOR \d+",
        f"#define TREEWEAVE_VERSION_MINOR {minor}",
        text,
    )
    text = _sub_once(
        HEADER_VERSION,
        r"#define TREEWEAVE_VERSION_PATCH \d+",
        f"#define TREEWEAVE_VERSION_PATCH {patch}",
        text,
    )
    text = _sub_once(
        HEADER_VERSION,
        r'#define TREEWEAVE_VERSION_STRING "\d+\.\d+\.\d+"',
        f'#define TREEWEAVE_VERSION_STRING "{version}"',
        text,
    )
    HEADER_VERSION.write_text(text)

    # bindings/julia/Treeweave/Project.toml — package version.
    text = JULIA_PROJECT.read_text()
    text = _sub_once(
        JULIA_PROJECT,
        r'^version = "\d+\.\d+\.\d+"',
        f'version = "{version}"',
        text,
    )
    JULIA_PROJECT.write_text(text)

    # bindings/js/package.json — package version.
    text = JS_PACKAGE.read_text()
    text = _sub_once(
        JS_PACKAGE,
        r'"version":\s*"\d+\.\d+\.\d+"',
        f'"version": "{version}"',
        text,
    )
    JS_PACKAGE.write_text(text)

    print(
        f"bumped treeweave version to {version} across VERSION, "
        "include/treeweave_version.h, bindings/julia/Treeweave/Project.toml, "
        "bindings/js/package.json"
    )
    print(f"next: git commit -am 'chore(release): bump version to {version}'")
    return 0


if __name__ == "__main__":
    sys.exit(main())
