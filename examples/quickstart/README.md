# Quick-start consumer projects

One directory per install route, each a real, standalone CMake project. The
docs embed these files verbatim, so a recipe on the website is a recipe CI
builds and runs.

| Directory | Route | Docs |
|---|---|---|
| `cpp-fetchcontent/` | `FetchContent` (recommended for C++) | install, C++ guide |
| `cpp-cpm/` | `CPMAddPackage` | CMake guide |
| `cpp-find_package/` | `find_package` against an install prefix | CMake guide |
| `c-find_package/` | `find_package` against the release tarball | install, C guide |

`main.cpp` and `main.c` sit one level up because every route compiles the same
program; only the way it finds treeweave differs.

The shell commands the docs print for these projects are regions of
`tools/ci/docs-recipes.sh`, which runs each route in a scratch tree against the
real GitHub fetch. `scripts/check_docs_code.py` refuses a region no page embeds
or no workflow runs.

`tools/ci/install-test.sh` runs every route, or one route named on its command
line. It substitutes the local checkout for the GitHub fetch through the
mechanisms CMake and CPM already provide (`FETCHCONTENT_SOURCE_DIR_TREEWEAVE`,
`CPM_treeweave_SOURCE`), so the files below stay the recipe a user actually
copies.
