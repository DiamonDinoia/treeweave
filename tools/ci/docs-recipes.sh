#!/usr/bin/env bash
# Every shell recipe the docs print, as a region CI executes.
#
# Each recipe is a `# BEGIN DOCS_<NAME>` / `# END DOCS_<NAME>` region inside a
# recipe_<name> function. The region text is exactly what a docs page prints
# with `literalinclude`, so the printed command and the executed command cannot
# drift. CI-only setup — a scratch directory, copying the example program in,
# `cd` into a build tree — stays outside the markers.
#
#   tools/ci/docs-recipes.sh --list
#   tools/ci/docs-recipes.sh dev-build dev-test
#
# scripts/check_docs_code.py enforces the invariants: every region lies inside a
# recipe_ function, at least one docs page includes it, and at least one
# workflow runs the recipe. A recipe nothing calls is a recipe nothing proves.
#
# TREEWEAVE_DOCS_RECIPE_DIR    scratch tree (default <root>/_docs-recipes)
# TREEWEAVE_DOCS_C_TARBALL     a C-ABI tarball to consume (release.yml hands in
#                              the one the run just built, release-install.yml
#                              the published asset). Unset: pack one here.
# TREEWEAVE_DOCS_CXX_HEADERS   same, for the C++ header bundle.
# TREEWEAVE_DOCS_PLATFORM      platform name in the tarball recipes
#                              (default linux-x86_64).
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
work="${TREEWEAVE_DOCS_RECIPE_DIR:-$root/_docs-recipes}"
PLATFORM="${TREEWEAVE_DOCS_PLATFORM:-linux-x86_64}"
shared_prefix="$work/prefix"

# Recipes run from a scratch directory, so a caller's relative path has to be
# resolved before the first cd.
abspath() { (cd "$(dirname "$1")" && printf '%s/%s\n' "$(pwd)" "$(basename "$1")"); }
c_tarball_in="${TREEWEAVE_DOCS_C_TARBALL:-}"
cxx_headers_in="${TREEWEAVE_DOCS_CXX_HEADERS:-}"
if [ -n "$c_tarball_in" ]; then c_tarball_in="$(abspath "$c_tarball_in")"; fi
if [ -n "$cxx_headers_in" ]; then cxx_headers_in="$(abspath "$cxx_headers_in")"; fi

# A clean directory per recipe, so one recipe never inherits another's leftovers.
scratch() {
    rm -rf "${work:?}/$1"
    mkdir -p "$work/$1"
    cd "$work/$1"
}

# What the recipe established, on stdout: a run whose only evidence is an exit
# status cannot be told from a run that was skipped.
proved() { printf '    proved: %s\n' "$1"; }

# One install tree, shared by the recipes that consume a prefix or a tarball.
ensure_prefix() {
    if [ -d "$shared_prefix/include" ]; then return 0; fi
    cmake -S "$root" -B "$work/prefix-build" -DCMAKE_BUILD_TYPE=Release \
        -DTREEWEAVE_BUILD_TESTS=OFF -DTREEWEAVE_BUILD_EXAMPLES=OFF
    cmake --build "$work/prefix-build" -j
    cmake --install "$work/prefix-build" --prefix "$shared_prefix"
}

# The C-ABI tarball, in the current directory, under the unversioned name the
# release attaches (release.yml's github-release job duplicates every platform
# archive under that name, so the floating download URL keeps working).
ensure_c_tarball() {
    if [ -n "$c_tarball_in" ]; then
        cp "$c_tarball_in" "treeweave-${PLATFORM}.tar.gz"
        echo "consuming the handed-in tarball $c_tarball_in"
        return 0
    fi
    ensure_prefix
    tar -C "$shared_prefix" -czf "$PWD/treeweave-${PLATFORM}.tar.gz" .
    echo "consuming a tarball packed from this checkout"
}

# The arch-independent header bundle, packed from the consolidated include tree
# exactly as _build-c-abi.yml packs the release asset.
ensure_cxx_headers() {
    if [ -n "$cxx_headers_in" ]; then
        cp "$cxx_headers_in" treeweave-cxx-headers.tar.gz
        echo "consuming the handed-in bundle $cxx_headers_in"
        return 0
    fi
    cmake -S "$root" -B "$work/hdrs" -DCMAKE_BUILD_TYPE=Release \
        -DTREEWEAVE_BUILD_TESTS=OFF -DTREEWEAVE_BUILD_EXAMPLES=OFF
    cmake --build "$work/hdrs" -j
    tar -C "$work/hdrs" -czf "$PWD/treeweave-cxx-headers.tar.gz" include
    echo "consuming a bundle packed from this checkout"
}

# ctest exits 0 when its -R filter matches nothing, so a recipe whose suite was
# never registered would print a `proved:` line having run no test at all. Count
# the matches before the region wherever the build tree already exists, and
# straight after it where the region itself is what configures that tree.
require_tests() {
    local n
    n=$(ctest --test-dir "$1" -R "$2" -N | sed -n 's/^Total Tests: //p')
    if [ "${n:-0}" -lt 1 ]; then
        echo "ERROR: no test in $1 matches '$2'; the suite the docs promise is not registered" >&2
        return 1
    fi
    printf '    %s test(s) match %s in %s\n' "$n" "$2" "$1"
}

# Copy one quick-start route into the scratch tree next to the program it
# compiles, so the literal user commands run against the committed project.
stage_quickstart() {
    cp -R "$root/examples/quickstart/$1" route
    cp "$root/examples/quickstart/$2" .
    cd route
}

# --- recipes: source build ---------------------------------------------------

recipe_clone() {
    scratch clone
    # BEGIN DOCS_CLONE
    git clone https://github.com/DiamonDinoia/treeweave.git
    cd treeweave
    # END DOCS_CLONE
    test -f CMakeLists.txt
    proved "the clone URL resolves and the checkout has a CMakeLists.txt"
}

recipe_dev_build() {
    cd "$root"
    # BEGIN DOCS_DEV_BUILD
    cmake --preset dev-release
    cmake --build build/dev-release -j
    # END DOCS_DEV_BUILD
    proved "the dev-release preset configures and builds this checkout"
}

recipe_dev_test() {
    cd "$root"
    require_tests build/dev-release .
    # BEGIN DOCS_DEV_TEST
    ctest --test-dir build/dev-release --output-on-failure
    # END DOCS_DEV_TEST
    proved "the dev-release test suite passes"
}

recipe_c_abi_build() {
    cd "$root"
    # BEGIN DOCS_C_ABI_BUILD
    cmake --preset dev-release
    cmake --build build/dev-release --target treeweave_c -j
    # END DOCS_C_ABI_BUILD
    ls build/dev-release/libtreeweave_c.* >/dev/null
    proved "the treeweave_c target builds a shared C library"
}

recipe_install_prefix() {
    cd "$root"
    # PREFIX would default into the checkout; keep the scratch tree clean.
    # quickstart-prefix exercises the default.
    export PREFIX="$work/install-prefix/prefix"
    rm -rf "$PREFIX"
    # BEGIN DOCS_INSTALL_PREFIX
    PREFIX="${PREFIX:-$PWD/_prefix}"
    cmake --preset dev-release
    cmake --build build/dev-release --parallel
    cmake --install build/dev-release --prefix "$PREFIX"
    # END DOCS_INSTALL_PREFIX
    test -f "$PREFIX/include/treeweave.h"
    proved "the install prefix carries treeweave.h and the CMake package"
}

recipe_oneflag_cxx() {
    cd "$root"
    trap 'rm -f "$root/simple1d"' EXIT
    # BEGIN DOCS_ONEFLAG_CXX
    cmake --preset dev-release
    cmake --build build/dev-release
    g++ -std=c++20 -O3 -march=native examples/c++/simple1d.cpp -Ibuild/dev-release/include -o simple1d
    ./simple1d
    # END DOCS_ONEFLAG_CXX
    proved "one -I against the consolidated header tree compiles and runs an example"
}

recipe_multiarch() {
    cd "$root"
    # BEGIN DOCS_MULTIARCH
    cmake -B build -DTREEWEAVE_C_MULTIARCH=ON -DTREEWEAVE_ARCH=x86-64
    # END DOCS_MULTIARCH
    grep -q '^TREEWEAVE_C_MULTIARCH:BOOL=ON$' build/CMakeCache.txt
    proved "the multiarch flags configure and land in the cache"
}

recipe_force_arch() {
    # The table needs the multiarch build DOCS_MULTIARCH configures, so this
    # runs that recipe rather than repeating its command; its `proved:` line
    # appears first as a result.
    recipe_multiarch
    cmake --build build --target test_c_abi -j
    cd build
    # BEGIN DOCS_FORCE_ARCH
    TREEWEAVE_FORCE_ARCH=sse2       ./test_c_abi   # force the x86 baseline
    TREEWEAVE_FORCE_ARCH=sse4.2     ./test_c_abi   # force the GCC/Clang middle rung
    TREEWEAVE_FORCE_ARCH=fma3+avx2  ./test_c_abi   # force AVX2
    TREEWEAVE_FORCE_ARCH=avx512bw   ./test_c_abi   # force AVX-512 where the host has it
    # END DOCS_FORCE_ARCH
    proved "every x86 rung name the table lists passes the C-ABI test"
}

# --- recipes: install from a release artifact --------------------------------

recipe_cxx_headers_download() {
    scratch cxx-headers-download
    # BEGIN DOCS_DOWNLOAD_CXX_HEADERS
    curl -fLO https://github.com/DiamonDinoia/treeweave/releases/latest/download/treeweave-cxx-headers.tar.gz
    # END DOCS_DOWNLOAD_CXX_HEADERS
    tar tzf treeweave-cxx-headers.tar.gz | grep -q 'include/treeweave/treeweave.hpp'
    proved "the floating header-bundle URL resolves to a bundle holding treeweave.hpp"
}

recipe_cxx_headers() {
    scratch cxx-headers
    cp "$root/examples/quickstart/main.cpp" .
    ensure_cxx_headers
    # BEGIN DOCS_CXX_HEADERS
    tar xzf treeweave-cxx-headers.tar.gz          # -> ./include/
    c++ -std=c++20 -O3 -march=native main.cpp -Iinclude -o app && ./app
    # END DOCS_CXX_HEADERS
    proved "the header bundle compiles the quick-start program with one -I"
}

recipe_c_tarball_download() {
    scratch c-tarball-download
    # BEGIN DOCS_DOWNLOAD_C_TARBALL
    PLATFORM=linux-x86_64      # or linux-aarch64, macos-arm64, macos-x86_64
    URL="https://github.com/DiamonDinoia/treeweave/releases/latest/download/treeweave-${PLATFORM}.tar.gz"
    curl -fLO "$URL"
    # END DOCS_DOWNLOAD_C_TARBALL
    tar tzf "treeweave-${PLATFORM}.tar.gz" | grep -q 'include/treeweave.h'
    proved "the floating C-ABI URL resolves to a tarball holding treeweave.h"
}

recipe_c_tarball() {
    scratch c-tarball
    cp "$root/examples/quickstart/main.c" .
    ensure_c_tarball
    # BEGIN DOCS_C_TARBALL
    tar xzf "treeweave-${PLATFORM}.tar.gz"   # extracts include/ and lib/ into ./
    cc main.c -Iinclude -Llib -ltreeweave_c -lm -o app
    LD_LIBRARY_PATH=lib ./app
    # END DOCS_C_TARBALL
    proved "the C-ABI tarball compiles, links and runs a C program"
}

recipe_quickstart_build() {
    scratch quickstart-build
    stage_quickstart cpp-fetchcontent main.cpp
    # BEGIN DOCS_QUICKSTART_BUILD
    cmake -S . -B build && cmake --build build && ./build/app
    # END DOCS_QUICKSTART_BUILD
    proved "FetchContent of the stable branch builds and runs the quick-start program"
}

recipe_quickstart_prefix() {
    scratch quickstart-prefix
    stage_quickstart c-find_package main.c
    ensure_c_tarball
    mkdir -p _prefix
    tar -C _prefix -xzf "treeweave-${PLATFORM}.tar.gz"
    # BEGIN DOCS_QUICKSTART_PREFIX
    PREFIX="${PREFIX:-$PWD/_prefix}"   # where the tarball was extracted
    cmake -S . -B build -DCMAKE_PREFIX_PATH="$PREFIX"
    cmake --build build && ./build/app
    # END DOCS_QUICKSTART_PREFIX
    proved "find_package against an extracted tarball builds and runs the C program"
}

# --- recipes: registries ----------------------------------------------------

recipe_pip_install() {
    scratch pip-install
    # BEGIN DOCS_PIP_PYPI
    pip install treeweave
    # END DOCS_PIP_PYPI
    python "$root/tests/smoke/pip_smoke.py"
    proved "the published wheel installs from PyPI and fits and evaluates"
}

recipe_pip_testpypi() {
    scratch pip-testpypi
    # BEGIN DOCS_PIP_TESTPYPI
    pip install --index-url https://test.pypi.org/simple/ \
                --extra-index-url https://pypi.org/simple/ treeweave
    # END DOCS_PIP_TESTPYPI
    python "$root/tests/smoke/pip_smoke.py"
    proved "the staging wheel installs from TestPyPI with its deps from PyPI"
}

recipe_npm_install() {
    scratch npm-install
    cp "$root/tests/smoke/npm_smoke.mjs" .
    # BEGIN DOCS_NPM
    npm install @flatironinstitute/treeweave
    # END DOCS_NPM
    node npm_smoke.mjs
    proved "the published package installs from the npm registry and evaluates"
}

# --- recipes: bindings from source ------------------------------------------

recipe_python_dev() {
    cd "$root"
    # BEGIN DOCS_PYTHON_DEV
    cmake --preset bindings-python
    cmake --build build/bindings-python -j
    ctest --test-dir build/bindings-python -R python_treeweave --output-on-failure
    # END DOCS_PYTHON_DEV
    require_tests build/bindings-python python_treeweave   # after: the region configures the tree
    proved "the Python binding builds from source and its suite passes"
}

recipe_julia_dev() {
    cd "$root"
    # BEGIN DOCS_JULIA_DEV
    cmake --preset bindings-julia
    cmake --build build/bindings-julia -j --target treeweave_c
    # END DOCS_JULIA_DEV
    proved "the sibling C-ABI build the Julia package resolves against exists"
}

recipe_julia_test() {
    cd "$root"
    require_tests build/bindings-julia julia_treeweave
    # BEGIN DOCS_JULIA_TEST
    ctest --test-dir build/bindings-julia -R julia_treeweave --output-on-failure
    # END DOCS_JULIA_TEST
    proved "the Julia suite passes against the sibling build"
}

recipe_fortran_dev() {
    cd "$root"
    # BEGIN DOCS_FORTRAN_DEV
    cmake --preset bindings-fortran
    cmake --build build/bindings-fortran -j
    # END DOCS_FORTRAN_DEV
    proved "the Fortran binding builds from source"
}

recipe_fortran_test() {
    cd "$root"
    require_tests build/bindings-fortran fortran_treeweave
    # BEGIN DOCS_FORTRAN_TEST
    ctest --test-dir build/bindings-fortran -R fortran_treeweave --output-on-failure
    # END DOCS_FORTRAN_TEST
    proved "the Fortran suite passes"
}

recipe_js_dev() {
    cd "$root"
    # BEGIN DOCS_JS_DEV
    cmake --preset bindings-js
    cmake --build build/bindings-js -j
    # END DOCS_JS_DEV
    proved "the native N-API addon and the TypeScript layer build from source"
}

recipe_octave_dev() {
    cd "$root"
    # BEGIN DOCS_OCTAVE_DEV
    cmake --preset bindings-octave      # or bindings-matlab to build against MATLAB
    cmake --build build/bindings-octave -j
    ctest --test-dir build/bindings-octave -R matlab_treeweave --output-on-failure
    # END DOCS_OCTAVE_DEV
    require_tests build/bindings-octave matlab_treeweave   # after: the region configures the tree
    proved "the Octave MEX builds from source and its suite passes"
}

# --- entry point ------------------------------------------------------------

all_recipes() { declare -F | sed -n 's/^declare -f recipe_//p' | tr '_' '-' | sort; }

if [ "${1:-}" = "--list" ]; then
    all_recipes
    exit 0
fi
if [ $# -eq 0 ]; then
    echo "usage: ${0##*/} [--list] <recipe>..." >&2
    all_recipes | sed 's/^/  /' >&2
    exit 2
fi

mkdir -p "$work"
for name in "$@"; do
    fn="recipe_${name//-/_}"
    if ! declare -F "$fn" >/dev/null; then
        echo "ERROR: unknown recipe '$name'; try --list" >&2
        exit 1
    fi
    echo
    echo "=== recipe: $name"
    ( "$fn" )
done

echo
echo "every requested docs recipe ran: $*"
