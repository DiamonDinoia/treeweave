#!/usr/bin/env python3
"""Check that the code shown in the docs is code that exists.

Five gates:

1. Every ``literalinclude`` resolves to a file, its ``:start-after:`` /
   ``:end-before:`` anchors are present in that file, and it names no
   ``:lines:`` range. A line range survives an edit above the range silently,
   so an anchor is the only stable way to embed part of a file.
2. Every C declaration typed inline in a ``code-block:: c`` matches a
   declaration in ``include/treeweave.h``, character for character once
   comments, the export macro and whitespace are normalized away.
3. No other source-language ``code-block`` exists. Program text and shell
   recipes in the docs are embedded from a file CI compiles or executes, so a
   page cannot drift from the library. A recipe CI cannot run without a
   contrivance carries the reason on the line before the directive::

       .. not-run-in-ci: <why, and which workflow covers what instead>

4. No orphan recipe. Every ``# BEGIN DOCS_*`` region in
   ``tools/ci/docs-recipes.sh`` lies inside a ``recipe_<name>`` function, at
   least one docs page embeds it, and at least one workflow runs the recipe. A
   recipe CI forgot to call is indistinguishable from one that runs.
5. Every release asset a ``DOCS_DOWNLOAD_*`` region names resolves: each
   platform matches the ``_build-c-abi.yml`` build matrix, and each archive
   name matches one a release publishes. The published set is derived, not
   guessed: the ``pkg=`` template in ``_build-c-abi.yml`` expanded over the
   matrix, plus the unversioned duplicates ``release.yml`` copies for the
   floating ``releases/latest`` URLs. A region writing ``${PLATFORM}`` is
   expanded over the platforms it lists, so the check reaches the floating URL
   the C guide prints. The download lines themselves can only run after a
   release exists, so this is the one failure they can be checked for in
   advance.

Sphinx reports a missing include as a warning, and ``docs/conf.py`` suppresses the
``docutils`` category to silence Exhale's generated-API noise, so even
``sphinx-build -W`` stays green when a docs page stops embedding real code. This
script is the gate that does fire.

The ``docs`` target runs it before ``sphinx-build``. It needs no build: the
figures under ``docs/_generated/`` are tracked. Run it directly with::

    python scripts/check_docs_code.py
    python scripts/check_docs_code.py --self-test   # positive controls
"""

from __future__ import annotations

import re
import sys
import tempfile
from pathlib import Path

DIRECTIVE = re.compile(r"^(\s*)\.\.\s+literalinclude::\s*(\S+)\s*$")
OPTION = re.compile(r"^(\s*):([a-z-]+):\s*(.*)$")
DOC_SUFFIXES = (".rst", ".src", ".md")

RECIPE_SCRIPT = "tools/ci/docs-recipes.sh"
WORKFLOW_DIR = ".github/workflows"
C_ABI_WORKFLOW = ".github/workflows/_build-c-abi.yml"
RELEASE_WORKFLOW = ".github/workflows/release.yml"


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


def documents(docs: Path):
    return sorted(p for p in docs.rglob("*") if p.suffix in DOC_SUFFIXES)


def check_c_blocks(docs, root, header) -> list[str]:
    problems = []
    if not header.is_file():
        return [
            f"{header} is missing, so the C declarations in the docs cannot be checked"
        ]
    reference = normalize_c(header.read_text(encoding="utf-8"))
    for doc in documents(docs):
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


# The languages whose snippets have to come from a file CI compiles or executes.
# The shell and CMake recipes are regions of tools/ci/docs-recipes.sh and of the
# consumer projects under examples/quickstart/; the program snippets come from
# the examples themselves.
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
    "bash",
    "sh",
    "shell",
    "console",
    "cmake",
}
ANY_CODE_BLOCK = re.compile(r"^(\s*)\.\.\s+code-block::\s*(\S+)\s*$")
EXEMPT = re.compile(r"^\s*\.\.\s+not-run-in-ci:\s*\S")


def check_snippets(docs: Path, root: Path) -> list[str]:
    """Every source-language snippet is embedded from a CI-run file, or exempted."""
    problems = []
    for doc in documents(docs):
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
    for doc in documents(docs):
        for line_no, target, options in parse(doc.read_text(encoding="utf-8")):
            # Sphinx resolves a leading "/" against the source dir, everything
            # else against the directory of the including document.
            base = docs if target.startswith("/") else doc.parent
            src = (base / target.lstrip("/")).resolve()
            where = f"{doc.relative_to(root)}:{line_no}"
            if "lines" in options:
                problems.append(
                    f"{where}: :lines: {options['lines']} on {target}. A line range "
                    "goes stale silently when the file is edited above it; use "
                    ":start-after: / :end-before: markers"
                )
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


RECIPE_FN = re.compile(r"^recipe_([a-z0-9_]+)\(\)\s*\{\s*$")
REGION_BEGIN = re.compile(r"^\s*# BEGIN (DOCS_[A-Z0-9_]+)\s*$")
REGION_END = re.compile(r"^\s*# END (DOCS_[A-Z0-9_]+)\s*$")
PLATFORM_NAME = re.compile(r"^(linux|macos|windows)[\w.+-]*$")
REGION_ARCHIVE = re.compile(r"treeweave-[\w.+${}-]*?\.(?:tar\.gz|zip)")
# release.yml copies each versioned platform archive to an unversioned name, so
# the floating releases/latest URLs the docs print keep resolving.
UNVERSIONED_DUP = re.compile(r'cp -- "\$f" "\$\{f/-\$ver-/-\}"')


def parse_recipes(text: str):
    """Return (regions, recipes, problems).

    ``regions`` maps a region name to (owning recipe or None, body lines).
    ``recipes`` is the set of recipe names, hyphenated as the CLI spells them.
    """
    regions: dict[str, tuple[str | None, list[str]]] = {}
    recipes: set[str] = set()
    problems: list[str] = []
    current_fn: str | None = None
    open_region: str | None = None
    body: list[str] = []
    for i, line in enumerate(text.splitlines(), 1):
        fn = RECIPE_FN.match(line)
        if fn:
            current_fn = fn.group(1).replace("_", "-")
            recipes.add(current_fn)
            continue
        if line == "}":
            if open_region:
                problems.append(
                    f"{RECIPE_SCRIPT}:{i}: region {open_region} is not closed inside recipe_"
                    f"{(current_fn or '').replace('-', '_')}"
                )
                open_region = None
            current_fn = None
            continue
        begin = REGION_BEGIN.match(line)
        if begin:
            if open_region:
                problems.append(
                    f"{RECIPE_SCRIPT}:{i}: region {begin.group(1)} opens inside {open_region}; "
                    "regions do not nest"
                )
            open_region, body = begin.group(1), []
            if begin.group(1) in regions:
                problems.append(
                    f"{RECIPE_SCRIPT}:{i}: region {begin.group(1)} is defined twice"
                )
            continue
        end = REGION_END.match(line)
        if end:
            if end.group(1) != open_region:
                problems.append(
                    f"{RECIPE_SCRIPT}:{i}: END {end.group(1)} does not close "
                    f"{open_region or 'anything'}"
                )
            else:
                regions[open_region] = (current_fn, body)
                if current_fn is None:
                    problems.append(
                        f"{RECIPE_SCRIPT}:{i}: region {open_region} is outside every "
                        "recipe_ function, so no recipe runs it"
                    )
            open_region = None
            continue
        if open_region:
            body.append(line)
    if open_region:
        problems.append(f"{RECIPE_SCRIPT}: region {open_region} is never closed")
    return regions, recipes, problems


def workflow_recipe_calls(workflows: Path) -> set[str]:
    """Every recipe name a workflow passes to docs-recipes.sh."""
    called: set[str] = set()
    for wf in sorted(workflows.glob("*.yml")):
        # A `run:` block wraps a long invocation with a trailing backslash;
        # join those first so the arguments stay on the calling line.
        text = re.sub(r"\\\n\s*", " ", wf.read_text(encoding="utf-8"))
        for line in text.splitlines():
            if "docs-recipes.sh" not in line:
                continue
            # A YAML comment naming the script calls nothing. Counting its prose
            # as recipe names would clear a future recipe called `download` or
            # `print` without any workflow running it.
            if line.lstrip().startswith("#"):
                continue
            _, _, tail = line.partition("docs-recipes.sh")
            # The argument list ends at the first shell metacharacter, so
            # `... pip-testpypi; then break; fi` contributes one name, not four.
            tail = re.split(r"[;&|#()<>]", tail, maxsplit=1)[0]
            for token in tail.split():
                if re.fullmatch(r"[a-z0-9]+(-[a-z0-9]+)*", token):
                    called.add(token)
    return called


def release_archives(root: Path) -> tuple[set[str], set[str]]:
    """The platform names, and every archive name a release publishes.

    Derived rather than read literally: `_build-c-abi.yml` names the platform
    archives through a `pkg=` template and a `${{ steps.ver.outputs.pkg }}`
    reference, so a regex over the file text finds only the hard-coded header
    bundle. Expand the template over the matrix, then add the unversioned
    duplicates `release.yml` makes for the floating `releases/latest` URLs.
    """
    path = root / C_ABI_WORKFLOW
    if not path.is_file():
        return set(), set()
    text = path.read_text(encoding="utf-8")
    platforms = {
        m for m in re.findall(r"^\s*- name: (\S+)$", text, re.M) if PLATFORM_NAME.match(m)
    }
    archives = set(re.findall(r"treeweave-[a-z0-9_.+-]+\.(?:tar\.gz|zip)", text))
    # Up to the closing quote, not to whitespace: the template holds
    # `${{ inputs.version }}` expressions with spaces inside the braces.
    template = re.search(r'pkg=(treeweave-[^"\n]+)', text)
    # Each packing step names its own suffix and carries the `if:` that says
    # which legs run it: the Ninja legs tar, the MSVC leg zips, so a `.tar.gz`
    # URL for windows-x64 resolves to nothing. Read the steps, not the file.
    for step in re.split(r"\n\s+- name:", text):
        # `run:`, so the step that creates the archive counts and the upload
        # step that lists both suffixes under `path:` does not.
        packs = re.search(
            r"run:[^\n]*steps\.ver\.outputs\.pkg\s*\}\}(\.tar\.gz|\.zip)", step
        )
        if not (packs and template):
            continue
        cond = re.search(r"if:\s*matrix\.name\s*(!=|==)\s*'(\S+?)'", step)
        if cond is None:
            legs = set(platforms)
        elif cond.group(1) == "==":
            legs = {cond.group(2)}
        else:
            legs = platforms - {cond.group(2)}
        for platform in legs:
            name = re.sub(r"\$\{\{\s*matrix\.name\s*\}\}", platform, template.group(1))
            name = re.sub(r"-\$\{\{\s*inputs\.version\s*\}\}", "", name)
            archives.add(name + packs.group(1))
    release = root / RELEASE_WORKFLOW
    if not UNVERSIONED_DUP.search(
        release.read_text(encoding="utf-8") if release.is_file() else ""
    ):
        # Without that copy step the floating unversioned names stop existing;
        # the header bundle is unversioned upstream, so it survives.
        archives = {a for a in archives if "cxx-headers" in a}
    return platforms, archives


def check_recipes(docs: Path, root: Path) -> list[str]:
    """No orphan recipe, and every named release asset resolves."""
    script = root / RECIPE_SCRIPT
    if not script.is_file():
        return [f"{RECIPE_SCRIPT} is missing, so the docs recipes cannot be checked"]
    regions, recipes, problems = parse_recipes(script.read_text(encoding="utf-8"))

    # Sphinx's :start-after: is a substring search, so one marker must never be
    # a prefix of another: the include would stop at the wrong line.
    for a in regions:
        for b in regions:
            if a != b and f"# BEGIN {a}" in f"# BEGIN {b}":
                problems.append(
                    f"{RECIPE_SCRIPT}: region {a} is a prefix of {b}; :start-after: "
                    "matches by substring and would resolve to the wrong region"
                )

    embedded = set()
    for doc in documents(docs):
        embedded |= set(
            re.findall(r"# BEGIN (DOCS_[A-Z0-9_]+)", doc.read_text(encoding="utf-8"))
        )
    for name in sorted(set(regions) - embedded):
        problems.append(
            f"{RECIPE_SCRIPT}: region {name} is embedded by no docs page, so nothing "
            "shows what it proves"
        )

    called = workflow_recipe_calls(root / WORKFLOW_DIR)
    for name in sorted(recipes - called):
        problems.append(
            f"{RECIPE_SCRIPT}: recipe {name} is called by no workflow under "
            f"{WORKFLOW_DIR}/, so CI never runs it"
        )

    platforms, archives = release_archives(root)
    downloads = {k: v for k, v in regions.items() if k.startswith("DOCS_DOWNLOAD_")}
    if downloads and not platforms:
        problems.append(
            f"{C_ABI_WORKFLOW}: no platform names found in the build matrix, so the "
            "assets the download recipes name cannot be checked"
        )
    for name, (_, body) in sorted(downloads.items()):
        text = "\n".join(body)
        named = set(re.findall(r"^\s*PLATFORM=(\S+)", text, re.M))
        # The comment beside PLATFORM= lists the alternatives a reader may pick.
        for comment in re.findall(r"^\s*PLATFORM=\S+\s*#\s*(?:or\s+)?(.*)$", text, re.M):
            named |= {p.strip() for p in comment.split(",") if p.strip()}
        for platform in sorted(named):
            if platform not in platforms:
                problems.append(
                    f"{RECIPE_SCRIPT}: region {name} names platform {platform!r}, which "
                    f"the {C_ABI_WORKFLOW} matrix does not build"
                )
        for pattern in sorted(set(REGION_ARCHIVE.findall(text))):
            # `treeweave-${PLATFORM}.tar.gz` is one printed line standing for
            # every platform the comment beside it lists; check them all.
            candidates = (
                sorted(pattern.replace("${PLATFORM}", p) for p in named)
                if "${PLATFORM}" in pattern
                else [pattern]
            )
            for archive in candidates:
                if archive not in archives:
                    problems.append(
                        f"{RECIPE_SCRIPT}: region {name} downloads {archive}, which no "
                        "release publishes"
                    )
    return problems


CLEAN_SCRIPT = """#!/usr/bin/env bash
recipe_dev_build() {
    # BEGIN DOCS_DEV_BUILD
    cmake --preset dev-release
    # END DOCS_DEV_BUILD
}

recipe_c_tarball_download() {
    # BEGIN DOCS_DOWNLOAD_C_TARBALL
    PLATFORM=linux-x86_64      # or macos-arm64
    curl -fLO "https://example.invalid/treeweave-${PLATFORM}.tar.gz"
    # END DOCS_DOWNLOAD_C_TARBALL
}
"""
CLEAN_PAGE = """.. literalinclude:: ../tools/ci/docs-recipes.sh
   :start-after: # BEGIN DOCS_DEV_BUILD
   :end-before: # END DOCS_DEV_BUILD

.. literalinclude:: ../tools/ci/docs-recipes.sh
   :start-after: # BEGIN DOCS_DOWNLOAD_C_TARBALL
   :end-before: # END DOCS_DOWNLOAD_C_TARBALL
"""
CLEAN_WORKFLOW = """jobs:
  x:
    steps:
      - run: tools/ci/docs-recipes.sh dev-build c-tarball-download
"""
CLEAN_C_ABI = """jobs:
  build:
    strategy:
      matrix:
        include:
          - name: linux-x86_64
          - name: macos-arm64
          - name: windows-x64
    steps:
      - name: Package name
        id: ver
        run: echo "pkg=treeweave-${{ inputs.version }}-${{ matrix.name }}" >> "$GITHUB_OUTPUT"
      - name: Package (tar.gz)
        if: matrix.name != 'windows-x64'
        run: tar -czf "${{ steps.ver.outputs.pkg }}.tar.gz" .
      - name: Package (zip)
        if: matrix.name == 'windows-x64'
        run: Compress-Archive -Path dist/* -DestinationPath "${{ steps.ver.outputs.pkg }}.zip"
      - name: Upload artifact
        with:
          path: |
            ${{ steps.ver.outputs.pkg }}.tar.gz
            ${{ steps.ver.outputs.pkg }}.zip
      - name: Package C++ headers
        run: tar -czf treeweave-cxx-headers.tar.gz include
"""
CLEAN_RELEASE = """jobs:
  github-release:
    steps:
      - name: Duplicate platform archives under unversioned names
        run: |
          for f in artifacts/treeweave-"$ver"-*; do
            cp -- "$f" "${f/-$ver-/-}"
          done
"""
# One more recipe, called through shell syntax the parser has to stop at.
THEN_RECIPE = """
recipe_then() {
    # BEGIN DOCS_THEN
    cmake --build build
    # END DOCS_THEN
}
"""
THEN_PAGE = """
.. literalinclude:: ../tools/ci/docs-recipes.sh
   :start-after: # BEGIN DOCS_THEN
   :end-before: # END DOCS_THEN
"""


def self_test() -> int:
    """Positive controls: every gate must fire on each way docs code can rot."""
    failures = []

    def case(name, fired, expected, claim):
        if fired != expected:
            failures.append(
                f"{name}: {'fired' if fired else 'did not fire'}, expected the opposite"
            )
        else:
            print(f"  proved: {claim}")

    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        docs = root / "docs"
        docs.mkdir()
        (docs / "real.cpp").write_text("int main() { return 0; }\n")
        (docs / "page.rst").write_text(".. literalinclude:: real.cpp\n")
        case(
            "clean tree",
            bool(check_tree(docs, root)),
            False,
            "a literalinclude that resolves, with no options, passes",
        )
        for name, text, claim in [
            (
                "missing file",
                ".. literalinclude:: nope.cpp\n",
                "a literalinclude of a file that does not exist fails",
            ),
            (
                "missing anchor",
                ".. literalinclude:: real.cpp\n   :start-after: NOT_THERE\n",
                "a :start-after: anchor absent from the file fails",
            ),
            (
                "line range",
                ".. literalinclude:: real.cpp\n   :lines: 2-\n",
                "a :lines: range fails, because an edit above it goes unnoticed",
            ),
        ]:
            (docs / "page.rst").write_text(text)
            case(name, bool(check_tree(docs, root)), True, claim)

        for name, text, fires, claim in [
            (
                "julia snippet",
                ".. code-block:: julia\n\n   using Pkg\n",
                True,
                "an inline julia snippet fails",
            ),
            (
                "exempted snippet",
                ".. not-run-in-ci: fetches a release.\n\n.. code-block:: julia\n\n   using Pkg\n",
                False,
                "an inline snippet with a not-run-in-ci reason passes",
            ),
            (
                "reason-less exemption",
                ".. not-run-in-ci:\n\n.. code-block:: julia\n\n   using Pkg\n",
                True,
                "a not-run-in-ci marker with no reason fails",
            ),
            (
                "bash recipe",
                ".. code-block:: bash\n\n   make\n",
                True,
                "an inline bash recipe fails",
            ),
            (
                "cmake snippet",
                ".. code-block:: cmake\n\n   find_package(treeweave)\n",
                True,
                "an inline cmake snippet fails",
            ),
        ]:
            (docs / "page.rst").write_text(text)
            case(name, bool(check_snippets(docs, root)), fires, claim)

        header = root / "treeweave.h"
        header.write_text(
            "TREEWEAVE_EXPORT double treeweave_eval_1d(treeweave_t f, double x0);\n"
        )
        block = ".. code-block:: c\n\n   {decl}\n"
        for name, decl, fires, claim in [
            (
                "declaration as published",
                "double treeweave_eval_1d(treeweave_t f, double x0);",
                False,
                "a C declaration matching the header passes",
            ),
            (
                "renamed function",
                "double treeweave_eval_one(treeweave_t f, double x0);",
                True,
                "a renamed C function fails",
            ),
            (
                "changed parameter",
                "double treeweave_eval_1d(treeweave_t f, float x0);",
                True,
                "a changed C parameter type fails",
            ),
            (
                "dropped parameter",
                "double treeweave_eval_1d(treeweave_t f);",
                True,
                "a dropped C parameter fails",
            ),
            (
                "a call, not a declaration",
                "treeweave_eval_1d(f, 3.5);",
                False,
                "a call in a C snippet is not read as a declaration",
            ),
        ]:
            (docs / "page.rst").write_text(block.format(decl=decl))
            case(name, bool(check_c_blocks(docs, root, header)), fires, claim)

    # The recipe gate needs a whole tree: script, page, workflows.
    def recipe_case(name, expected, claim, script=None, page=None, workflow=None,
                    release=None):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "docs").mkdir()
            (root / "tools/ci").mkdir(parents=True)
            (root / WORKFLOW_DIR).mkdir(parents=True)
            (root / RECIPE_SCRIPT).write_text(script or CLEAN_SCRIPT)
            (root / "docs/page.rst").write_text(page or CLEAN_PAGE)
            (root / WORKFLOW_DIR / "run.yml").write_text(workflow or CLEAN_WORKFLOW)
            (root / C_ABI_WORKFLOW).write_text(CLEAN_C_ABI)
            (root / RELEASE_WORKFLOW).write_text(release or CLEAN_RELEASE)
            fired = bool(check_recipes(root / "docs", root))
            case(name, fired, expected, claim)

    recipe_case(
        "clean recipes",
        False,
        "a region inside a recipe, embedded by a page and called by a workflow, passes",
    )
    recipe_case(
        "region outside a recipe",
        True,
        "a region outside every recipe_ function fails",
        script=CLEAN_SCRIPT.replace("recipe_dev_build() {", "not_a_recipe() {"),
    )
    recipe_case(
        "uncalled recipe",
        True,
        "a recipe no workflow calls fails",
        workflow=CLEAN_WORKFLOW.replace("dev-build ", ""),
    )
    recipe_case(
        "unembedded region",
        True,
        "a region no docs page embeds fails",
        page=CLEAN_PAGE.split("\n\n")[1] + "\n",
    )
    recipe_case(
        "renamed region",
        True,
        "renaming a region in the script alone fails, because the page no longer embeds it",
        script=CLEAN_SCRIPT.replace("DOCS_DEV_BUILD", "DOCS_DEV_CONFIGURE"),
    )
    recipe_case(
        "unknown platform",
        True,
        "a download region naming a platform the release matrix does not build fails",
        script=CLEAN_SCRIPT.replace("macos-arm64", "solaris-sparc"),
    )
    recipe_case(
        "unpacked archive",
        True,
        "a download region naming a hard-coded archive no packing step produces fails",
        script=CLEAN_SCRIPT.replace("treeweave-${PLATFORM}.tar.gz", "treeweave-headers.tar.gz"),
    )
    recipe_case(
        "call inside a YAML comment",
        True,
        "a recipe named only by a workflow comment fails, because no step runs it",
        workflow="jobs:\n  x:\n    steps:\n"
        "      # tools/ci/docs-recipes.sh dev-build c-tarball-download, once #123 lands\n"
        "      - run: echo nothing\n",
    )
    recipe_case(
        "shell syntax after the call",
        True,
        "shell keywords following the invocation are not read as recipe names",
        script=CLEAN_SCRIPT + THEN_RECIPE,
        page=CLEAN_PAGE + THEN_PAGE,
        workflow=CLEAN_WORKFLOW.replace(
            "dev-build c-tarball-download",
            "dev-build c-tarball-download; then break; fi",
        ),
    )
    recipe_case(
        "platform that ships a zip",
        True,
        "a .tar.gz download region listing a platform the release zips fails",
        script=CLEAN_SCRIPT.replace("macos-arm64", "windows-x64"),
    )
    recipe_case(
        "floating names withdrawn",
        True,
        "dropping the unversioned copy step fails, because the floating URLs stop resolving",
        release=CLEAN_RELEASE.replace('cp -- "$f" "${f/-$ver-/-}"', "true"),
    )
    recipe_case(
        "renamed platform archive",
        True,
        "a download region naming an archive the release does not publish fails",
        script=CLEAN_SCRIPT.replace("treeweave-${PLATFORM}", "treeweave-c-${PLATFORM}"),
    )
    recipe_case(
        "prefix-colliding regions",
        True,
        "a region name that is a prefix of another fails, because :start-after: matches by substring",
        script=CLEAN_SCRIPT.replace("DOCS_DEV_BUILD", "DOCS_DOWNLOAD_C_TARBALL_X"),
        page=CLEAN_PAGE.replace("DOCS_DEV_BUILD", "DOCS_DOWNLOAD_C_TARBALL_X"),
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
    problems += check_recipes(root / "docs", root)
    for p in problems:
        print(f"FAIL: {p}")
    if problems:
        print(f"\n{len(problems)} problem(s) with the code shown in the docs")
        return 1
    print(
        "every literalinclude resolves with anchors and no line ranges, every inline C "
        "declaration matches the header, every other snippet comes from a file CI runs, "
        "and every docs recipe is embedded by a page and called by a workflow"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
