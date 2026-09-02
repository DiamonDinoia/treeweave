#!/usr/bin/env python3
"""Fail loudly when a hand-copied mirror of a treeweave fact has drifted.

Three facts are written in more than one file, and every copy is checked here
against the one file that owns it:

- the release version. ``VERSION`` owns it; ``bindings/julia/Treeweave/Project.toml``
  and ``bindings/js/package.json`` mirror it. ``include/treeweave_version.h`` is
  generated from ``VERSION`` on every configure and gitignored, so it cannot drift.
  The Python wheel version comes from ``VERSION`` through scikit-build-core.
- the ``tol_kind`` and dtype enumerators. ``include/treeweave.h`` owns them; the C++
  enum and all five bindings mirror them.
- the fit-option defaults. ``treeweave_default_opts`` in ``src/capi/treeweave.cpp``
  owns them; ``bindings/matlab/treeweave.m`` hard-codes them, because the MEX
  gateway has no entry point that reads them at run time. Python, Julia, JS and
  Fortran call the C function instead and are not checked here.

Every extractor fails when its block is missing, so a mirror that is renamed or
deleted is an error, never a silent pass.

Usage::

    python scripts/check_sync.py               # every mirror must agree
    python scripts/check_sync.py X.Y.Z         # ...and the version must equal X.Y.Z
    python scripts/check_sync.py --unpublished X.Y.Z
                                               # ...and no tag or PyPI release claims X.Y.Z
    python scripts/check_sync.py --self-test   # prove each check fires on a drifted copy

Nonzero exit + a message naming the file and the field on any mismatch.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
import urllib.error
import urllib.request
from pathlib import Path
from typing import Callable

ROOT = Path(__file__).resolve().parent.parent

TOL_KINDS = (
    "RELATIVE_TAIL",
    "ABSOLUTE_TAIL",
    "RELATIVE_MAX",
    "ABSOLUTE_MAX",
    "RELATIVE_L2",
    "ABSOLUTE_L2",
)


class Drift(Exception):
    """A mirror disagrees with the file that owns the fact."""


def _block(text: str, start: str, end: str, where: str) -> str:
    i = text.find(start)
    j = text.find(end, i + 1) if i >= 0 else -1
    if i < 0 or j < 0:
        raise Drift(f"{where}: no block between {start!r} and {end!r}")
    return text[i + len(start) : j]


def _pairs(text: str, pattern: str, where: str, count: int) -> dict[str, int]:
    found = {m.group(1).upper(): int(m.group(2)) for m in re.finditer(pattern, text)}
    if len(found) != count:
        raise Drift(f"{where}: expected {count} entries, found {sorted(found)}")
    return found


# ---- the files that own each fact -------------------------------------------


def c_tol_kinds(text: str) -> dict[str, int]:
    body = _block(text, "BEGIN DOCS_TOL_KIND_C", "END DOCS_TOL_KIND_C", "treeweave.h")
    return _pairs(body, r"TREEWEAVE_(\w+)\s*=\s*(\d+)", "treeweave.h", len(TOL_KINDS))


def c_dtypes(text: str) -> dict[str, int]:
    body = _block(text, "typedef enum {", "} treeweave_dtype_t;", "treeweave.h")
    return _pairs(body, r"TREEWEAVE_(F\d+)\s*=\s*(\d+)", "treeweave.h dtype", 2)


def c_defaults(text: str) -> dict[str, int]:
    body = _block(text, "void treeweave_default_opts", "}\n", "treeweave.cpp")
    out: dict[str, int] = {}
    for m in re.finditer(r"\.(\w+)\s*=\s*(TREEWEAVE_\w+|-?\d+)", body):
        value = m.group(2)
        out[m.group(1)] = int(value) if value.lstrip("-").isdigit() else -1
    tol = re.search(r"\.tol_kind\s*=\s*TREEWEAVE_(\w+)", body)
    if tol is None or len(out) != 5:
        raise Drift(f"treeweave_default_opts: expected 5 fields, found {sorted(out)}")
    out["tol_kind"] = TOL_KINDS.index(tol.group(1))
    return out


# ---- the mirrors -------------------------------------------------------------


def cpp_tol_kinds(text: str) -> dict[str, int]:
    body = _block(text, "BEGIN DOCS_TOL_KIND", "END DOCS_TOL_KIND", "tol_kind.hpp")
    # RelativeTail -> RELATIVE_TAIL, RelativeL2 -> RELATIVE_L2.
    found = {
        re.sub(r"(?<=[a-z])(?=[A-Z])", "_", m.group(1)).upper(): int(m.group(2))
        for m in re.finditer(r"(\w+)\s*=\s*(\d+),", body)
    }
    if len(found) != len(TOL_KINDS):
        raise Drift(f"tol_kind.hpp: expected {len(TOL_KINDS)} enumerators, found {sorted(found)}")
    return found


def python_tol_kinds(text: str) -> dict[str, int]:
    body = _block(text, "_TOL_KIND = {", "}", "__init__.py")
    return _pairs(body, r'"(\w+)":\s*(\d+)', "__init__.py", len(TOL_KINDS))


def julia_tol_kinds(text: str) -> dict[str, int]:
    return _pairs(text, r"const TREEWEAVE_(\w+)\s*=\s*Cint\((\d+)\)", "Treeweave.jl", len(TOL_KINDS) + 2)


def fortran_tol_kinds(text: str) -> dict[str, int]:
    return _pairs(
        text,
        r"parameter\s*::\s*TREEWEAVE_(\w+)\s*=\s*(\d+)_c_int",
        "treeweave.f90",
        len(TOL_KINDS) + 2,
    )


def ts_tol_kinds(text: str) -> dict[str, int]:
    body = _block(text, "export const TOL_KIND = {", "}", "backend.ts")
    return _pairs(body, r"(\w+):\s*(\d+)", "backend.ts", len(TOL_KINDS))


def matlab_tol_kinds(text: str) -> dict[str, int]:
    """The MATLAB docstring states the range and the default, not every name."""
    m = re.search(r"'tol_kind'\s*(\d+)=\w+\.\.(\d+)=\w+ \(default: (\d+)=\w+\)", text)
    if m is None:
        raise Drift("treeweave.m: no 'tol_kind' range line in the docstring")
    return {
        TOL_KINDS[0]: int(m.group(1)),
        TOL_KINDS[-1]: int(m.group(2)),
        "DEFAULT": int(m.group(3)),
    }


def matlab_defaults(text: str) -> dict[str, int]:
    body = _block(text, "p = inputParser;", "parse(p,", "treeweave.m")
    out = {
        m.group(1): int(m.group(2))
        for m in re.finditer(r"addParameter\(p,\s*'(\w+)',\s*(-?\d+)\)", body)
    }
    missing = [k for k in c_defaults(_read("src/capi/treeweave.cpp")) if k not in out]
    if missing:
        raise Drift(f"treeweave.m: no addParameter default for {missing}")
    return out


def _read(rel: str) -> str:
    return (ROOT / rel).read_text()


# Each row: label, file, extractor, the owning fact, and the one-character text
# edit --self-test applies to prove the row can fail.
Mirror = tuple[str, str, Callable[[str], dict[str, int]], str, tuple[str, str]]

MIRRORS: tuple[Mirror, ...] = (
    (
        "C++ TolKind",
        "include/treeweave/detail/tol_kind.hpp",
        cpp_tol_kinds,
        "tol_kind",
        ("AbsoluteL2   = 5", "AbsoluteL2   = 9"),
    ),
    (
        "Python _TOL_KIND",
        "bindings/python/treeweave/__init__.py",
        python_tol_kinds,
        "tol_kind",
        ('"relative_max":  2', '"relative_max":  9'),
    ),
    (
        "Julia constants",
        "bindings/julia/Treeweave/src/Treeweave.jl",
        julia_tol_kinds,
        "tol_kind+dtype",
        ("TREEWEAVE_F32 = Cint(1)", "TREEWEAVE_F32 = Cint(9)"),
    ),
    (
        "Fortran parameters",
        "bindings/fortran/treeweave.f90",
        fortran_tol_kinds,
        "tol_kind+dtype",
        ("TREEWEAVE_RELATIVE_L2   = 4_c_int", "TREEWEAVE_RELATIVE_L2   = 9_c_int"),
    ),
    (
        "TypeScript TOL_KIND",
        "bindings/js/src/backend.ts",
        ts_tol_kinds,
        "tol_kind",
        ("absolute_tail: 1", "absolute_tail: 9"),
    ),
    (
        "MATLAB tol_kind docstring",
        "bindings/matlab/treeweave.m",
        matlab_tol_kinds,
        "tol_kind_range",
        ("(default: 2=REL_MAX)", "(default: 9=REL_MAX)"),
    ),
    (
        "MATLAB option defaults",
        "bindings/matlab/treeweave.m",
        matlab_defaults,
        "defaults",
        ("'max_depth',              50", "'max_depth',              99"),
    ),
)


def _reference(fact: str) -> dict[str, int]:
    header = _read("include/treeweave.h")
    if fact == "tol_kind":
        return c_tol_kinds(header)
    if fact == "tol_kind+dtype":
        return c_tol_kinds(header) | c_dtypes(header)
    if fact == "tol_kind_range":
        kinds = c_tol_kinds(header)
        defaults = c_defaults(_read("src/capi/treeweave.cpp"))
        return {
            TOL_KINDS[0]: kinds[TOL_KINDS[0]],
            TOL_KINDS[-1]: kinds[TOL_KINDS[-1]],
            "DEFAULT": defaults["tol_kind"],
        }
    if fact == "defaults":
        return c_defaults(_read("src/capi/treeweave.cpp"))
    raise AssertionError(fact)


def _check_mirror(label: str, text: str, extract: Callable[[str], dict[str, int]], fact: str) -> list[str]:
    want = _reference(fact)
    got = extract(text)
    return [
        f"{label}: {name} is {got[name]}, {fact} says {value}"
        for name, value in want.items()
        if name in got and got[name] != value
    ] + [f"{label}: no entry for {name}" for name in want if name not in got]


def check_mirrors() -> list[str]:
    errors: list[str] = []
    for label, rel, extract, fact, _ in MIRRORS:
        try:
            errors += _check_mirror(label, _read(rel), extract, fact)
        except Drift as exc:
            errors.append(str(exc))
    return errors


# ---- versions ----------------------------------------------------------------


def _version(rel: str, pattern: str) -> str:
    m = re.search(pattern, _read(rel), flags=re.MULTILINE)
    if not m:
        raise Drift(f"{rel}: no version match (pattern {pattern!r})")
    return m.group(1)


def check_versions(expected: str | None) -> tuple[list[str], str]:
    canonical = _read("VERSION").strip()
    if not re.fullmatch(r"\d+\.\d+\.\d+", canonical):
        return [f"VERSION must be MAJOR.MINOR.PATCH, got {canonical!r}"], canonical
    found = {
        "VERSION": canonical,
        "bindings/julia/Treeweave/Project.toml": _version(
            "bindings/julia/Treeweave/Project.toml", r'^version = "(\d+\.\d+\.\d+)"'
        ),
        "bindings/js/package.json": _version(
            "bindings/js/package.json", r'"version":\s*"(\d+\.\d+\.\d+)"'
        ),
    }
    errors = [f"{name}: version {value}, VERSION says {canonical}" for name, value in found.items() if value != canonical]
    if expected is not None and canonical != expected:
        errors.append(f"version is {canonical}, expected {expected}")
    return errors, canonical


def check_unpublished(version: str) -> list[str]:
    """Refuse a version that already has a tag or a PyPI release."""
    errors = []
    tag = subprocess.run(
        ["git", "rev-parse", f"v{version}"], cwd=ROOT, capture_output=True, check=False
    )
    if tag.returncode == 0:
        errors.append(f"tag v{version} already exists")
    url = f"https://pypi.org/pypi/treeweave/{version}/json"
    try:
        with urllib.request.urlopen(url, timeout=30) as response:  # noqa: S310
            errors.append(f"version {version} is already on PyPI (HTTP {response.status})")
    except urllib.error.HTTPError as exc:
        if exc.code != 404:
            # Anything but a clean 404 leaves the question unanswered, and an
            # unanswered question is not a pass.
            errors.append(f"PyPI answered HTTP {exc.code} for {url}; cannot prove {version} is unpublished")
    except urllib.error.URLError as exc:
        errors.append(f"PyPI unreachable ({exc.reason}); cannot prove {version} is unpublished")
    return errors


# ---- self-test ---------------------------------------------------------------


def self_test() -> int:
    """Every check must report a copy with one value changed."""
    failures = 0
    for label, rel, extract, fact, (old, new) in MIRRORS:
        text = _read(rel)
        if old not in text:
            print(f"  BROKEN CONTROL {label}: {old!r} is not in {rel}")
            failures += 1
            continue
        try:
            errors = _check_mirror(label, text.replace(old, new, 1), extract, fact)
        except Drift as exc:
            errors = [str(exc)]
        if errors:
            print(f"  proved: {label} reports a drifted copy ({errors[0]})")
        else:
            print(f"  MISSED: {label} accepted {old!r} -> {new!r}")
            failures += 1

    # The version check, same treatment.
    canonical = _read("VERSION").strip()
    errors, _ = check_versions(f"{canonical}-not-this")
    if errors:
        print("  proved: the version check reports a version that is not the expected one")
    else:
        print("  MISSED: the version check accepted a wrong expected version")
        failures += 1

    if failures:
        print(f"self-test FAILED: {failures} check(s) cannot fail", file=sys.stderr)
        return 1
    print("self-test passed")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("version", nargs="?", help="the version every version file must equal")
    parser.add_argument(
        "--unpublished",
        metavar="X.Y.Z",
        help="also refuse the version if a tag or a PyPI release already exists",
    )
    parser.add_argument("--self-test", action="store_true", help="prove each check fires")
    args = parser.parse_args()

    if args.self_test:
        return self_test()

    # --unpublished guards only the publication state: the bump workflow runs it
    # before the version files have been rewritten. The positional argument is
    # what demands they already equal a given version.
    expected = (args.version or "").lstrip("v") or None
    errors, canonical = check_versions(expected)
    errors += check_mirrors()
    if args.unpublished:
        errors += check_unpublished(args.unpublished.lstrip("v"))

    if errors:
        print("error: hand-synced copies disagree:", file=sys.stderr)
        for line in errors:
            print(f"  {line}", file=sys.stderr)
        return 1

    published = ", no tag and no PyPI release" if args.unpublished else ""
    print(
        f"sync OK: version {canonical}, {len(MIRRORS)} enum and default mirrors match "
        f"include/treeweave.h and treeweave_default_opts{published}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
