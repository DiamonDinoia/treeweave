#!/usr/bin/env python3
"""Check that the code shown in the docs is code that exists.

Three gates:

1. Every ``literalinclude`` resolves to a file, and its ``:start-after:`` /
   ``:end-before:`` anchors are present in that file.
2. Every C declaration typed inline in a ``code-block:: c`` matches a
   declaration in ``include/treeweave.h``, character for character once
   comments, the export macro and whitespace are normalized away.
3. No other source-language ``code-block`` exists. Source code in the docs is
   embedded from an example that CI compiles and runs, so a page cannot drift
   from the library. An install recipe that CI cannot run (it fetches a
   published release) carries the reason on the line before the directive::

       .. not-run-in-ci: <why, and which workflow covers it instead>

Sphinx reports a missing include as a warning, and ``docs/conf.py`` suppresses the
``docutils`` category to silence Exhale's generated-API noise, so even
``sphinx-build -W`` stays green when a docs page stops embedding real code. This
script is the gate that does fire: it resolves every path and, when the directive
carries ``:start-after:`` / ``:end-before:``, checks the anchors are present too.

Run it after the docs target, so the figures captured under ``docs/_generated/``
already exist:

    python scripts/check_docs_code.py
    python scripts/check_docs_code.py --self-test   # positive control
"""

from __future__ import annotations

import re
import sys
import tempfile
from pathlib import Path

DIRECTIVE = re.compile(r"^(\s*)\.\.\s+literalinclude::\s*(\S+)\s*$")
OPTION = re.compile(r"^(\s*):([a-z-]+):\s*(.*)$")
DOC_SUFFIXES = (".rst", ".src", ".md")


def parse(text: str):
    """Yield (line_no, path, options) for each literalinclude in one document."""
    lines = text.splitlines()
    for i, line in enumerate(lines):
        m = DIRECTIVE.match(line)
        if not m:
            continue
        body_indent, target = m.group(1), m.group(2)
        options = {}
        for follow in lines[i + 1 :]:
            if not follow.strip():
                break
            opt = OPTION.match(follow)
            if not opt or len(opt.group(1)) <= len(body_indent):
                break
            options[opt.group(2)] = opt.group(3).strip()
        yield i + 1, target, options


C_HEADER = "include/treeweave.h"
CODE_BLOCK = re.compile(r"^(\s*)\.\.\s+code-block::\s*c\s*$")


def strip_c_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    return re.sub(r"//[^\n]*", " ", text)


def normalize_c(text: str) -> str:
    """Collapse a C declaration to one canonical line."""
    text = strip_c_comments(text).replace("TREEWEAVE_EXPORT", " ")
    text = re.sub(r"\s*([(),;*])\s*", r"\1", text)
    return re.sub(r"\s+", " ", text).strip()


def c_declarations(body: str):
    """Yield the declarations in one C snippet, skipping statements and calls."""
    for raw in re.split(r";", strip_c_comments(body)):
        decl = raw.strip()
        if not decl or "=" not in decl and "(" not in decl:
            continue
        if "=" in decl or "treeweave" not in decl:
            continue
        # A declaration names a type before the identifier; a bare call does not.
        if not re.match(r"^[A-Za-z_][\w\s*]*[\s*]\(?\*?\s*\w+\s*\)?\s*\(", decl):
            continue
        yield decl + ";"


def check_c_blocks(docs, root, header) -> list[str]:
    problems = []
    if not header.is_file():
        return [
            f"{header} is missing, so the C declarations in the docs cannot be checked"
        ]
    reference = normalize_c(header.read_text(encoding="utf-8"))
    for doc in sorted(p for p in docs.rglob("*") if p.suffix in DOC_SUFFIXES):
        lines = doc.read_text(encoding="utf-8").splitlines()
        for i, line in enumerate(lines):
            m = CODE_BLOCK.match(line)
            if not m:
                continue
            indent = len(m.group(1))
            body = []
            for follow in lines[i + 1 :]:
                if follow.strip() and len(follow) - len(follow.lstrip()) <= indent:
                    break
                body.append(follow)
            for decl in c_declarations("\n".join(body)):
                if normalize_c(decl) not in reference:
                    problems.append(
                        f"{doc.relative_to(root)}:{i + 1}: no such declaration in {C_HEADER}: {decl}"
                    )
    return problems


# The languages whose snippets have to come from a compiled, executed example.
# bash, cmake, text and console are recipes, checked by running them elsewhere
# (tools/ci/install-test.sh) rather than by this gate.
SOURCE_LANGUAGES = {
    "c",
    "cpp",
    "c++",
    "python",
    "julia",
    "matlab",
    "octave",
    "fortran",
    "js",
    "javascript",
    "ts",
    "typescript",
}
ANY_CODE_BLOCK = re.compile(r"^(\s*)\.\.\s+code-block::\s*(\S+)\s*$")
EXEMPT = re.compile(r"^\s*\.\.\s+not-run-in-ci:\s*\S")


def check_snippets(docs: Path, root: Path) -> list[str]:
    """Every source-language snippet is embedded from a CI-run file, or exempted."""
    problems = []
    for doc in sorted(p for p in docs.rglob("*") if p.suffix in DOC_SUFFIXES):
        lines = doc.read_text(encoding="utf-8").splitlines()
        for i, line in enumerate(lines):
            m = ANY_CODE_BLOCK.match(line)
            if not m:
                continue
            lang = m.group(2).lower()
            # `c` blocks list the ABI declarations; check_c_blocks holds those
            # to include/treeweave.h, which is stronger than running them.
            if lang not in SOURCE_LANGUAGES or lang == "c":
                continue
            previous = next((p for p in reversed(lines[:i]) if p.strip()), "")
            if EXEMPT.match(previous):
                continue
            problems.append(
                f"{doc.relative_to(root)}:{i + 1}: inline {lang} snippet. Embed it with "
                "literalinclude from an example CI runs, or mark it "
                "'.. not-run-in-ci: <reason>'"
            )
    return problems


def check_tree(docs: Path, root: Path) -> list[str]:
    problems = []
    for doc in sorted(p for p in docs.rglob("*") if p.suffix in DOC_SUFFIXES):
        for line_no, target, options in parse(doc.read_text(encoding="utf-8")):
            # Sphinx resolves a leading "/" against the source dir, everything
            # else against the directory of the including document.
            base = docs if target.startswith("/") else doc.parent
            src = (base / target.lstrip("/")).resolve()
            where = f"{doc.relative_to(root)}:{line_no}"
            if not src.is_file():
                problems.append(
                    f"{where}: literalinclude target does not exist: {target}"
                )
                continue
            body = src.read_text(encoding="utf-8", errors="replace")
            for opt in ("start-after", "end-before"):
                anchor = options.get(opt)
                if anchor and anchor not in body:
                    problems.append(
                        f"{where}: :{opt}: anchor {anchor!r} not found in {target}"
                    )
    return problems


def self_test() -> int:
    """Positive control: every gate must fire on each way docs code can rot."""
    failures = []
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        docs = root / "docs"
        docs.mkdir()
        (docs / "real.cpp").write_text("int main() { return 0; }\n")
        (docs / "page.rst").write_text(".. literalinclude:: real.cpp\n")
        if check_tree(docs, root):
            failures.append("clean tree reported a problem")
        for name, text in {
            "missing file": ".. literalinclude:: nope.cpp\n",
            "missing anchor": ".. literalinclude:: real.cpp\n   :start-after: NOT_THERE\n",
        }.items():
            (docs / "page.rst").write_text(text)
            if not check_tree(docs, root):
                failures.append(f"{name}: not detected")

        for name, text, fires in [
            ("julia snippet", ".. code-block:: julia\n\n   using Pkg\n", True),
            (
                "exempted snippet",
                ".. not-run-in-ci: fetches a release.\n\n.. code-block:: julia\n\n   using Pkg\n",
                False,
            ),
            (
                "reason-less exemption",
                ".. not-run-in-ci:\n\n.. code-block:: julia\n\n   using Pkg\n",
                True,
            ),
            ("bash recipe", ".. code-block:: bash\n\n   make\n", False),
        ]:
            (docs / "page.rst").write_text(text)
            fired = bool(check_snippets(docs, root))
            if fired != fires:
                failures.append(
                    f"snippet {name}: {'fired' if fired else 'did not fire'}, expected the opposite"
                )

        header = root / "treeweave.h"
        header.write_text(
            "TREEWEAVE_EXPORT double treeweave_eval_1d(treeweave_t f, double x0);\n"
        )
        block = ".. code-block:: c\n\n   {decl}\n"
        for name, decl, fires in [
            (
                "declaration as published",
                "double treeweave_eval_1d(treeweave_t f, double x0);",
                False,
            ),
            (
                "renamed function",
                "double treeweave_eval_one(treeweave_t f, double x0);",
                True,
            ),
            (
                "changed parameter",
                "double treeweave_eval_1d(treeweave_t f, float x0);",
                True,
            ),
            ("dropped parameter", "double treeweave_eval_1d(treeweave_t f);", True),
            ("a call, not a declaration", "treeweave_eval_1d(f, 3.5);", False),
        ]:
            (docs / "page.rst").write_text(block.format(decl=decl))
            fired = bool(check_c_blocks(docs, root, header))
            if fired != fires:
                failures.append(
                    f"C {name}: {'fired' if fired else 'did not fire'}, expected the opposite"
                )

    for f in failures:
        print(f"FAIL: {f}")
    print(
        "self-test passed"
        if not failures
        else f"{len(failures)} self-test case(s) failed"
    )
    return 1 if failures else 0


def main(argv: list[str]) -> int:
    if "--self-test" in argv:
        return self_test()
    root = Path(__file__).resolve().parent.parent
    problems = check_tree(root / "docs", root)
    problems += check_c_blocks(root / "docs", root, root / C_HEADER)
    problems += check_snippets(root / "docs", root)
    for p in problems:
        print(f"FAIL: {p}")
    if problems:
        print(f"\n{len(problems)} problem(s) with the code shown in the docs")
        return 1
    print(
        "every literalinclude resolves, every inline C declaration matches the header, "
        "and every other snippet comes from a CI-run file"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
