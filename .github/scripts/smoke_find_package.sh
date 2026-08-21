#!/usr/bin/env bash
# Smoke-test an installed/extracted treeweave C-ABI tree: a fresh consumer project
# must find_package(treeweave), link treeweave::treeweave_c, and fit+eval at runtime.
# Usage: smoke_find_package.sh <install-prefix>
set -euo pipefail

set -x
prefix="${1:?usage: smoke_find_package.sh <install-prefix>}"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

cat >"$work/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.20)
project(treeweave_smoke C)
find_package(treeweave REQUIRED)
add_executable(smoke smoke.c)
target_link_libraries(smoke PRIVATE treeweave::treeweave_c)
CMAKE

cat >"$work/smoke.c" <<'C'
/* Deliberately libm-free: this checks find_package + treeweave_c linkage, not
   the consumer's own math deps. f(x) = x^2 needs no <math.h>. */
#include <treeweave.h>
#include <stdio.h>
static void f(const double *x, double *y, void *d) { (void)d; y[0] = x[0] * x[0]; }
int main(void) {
    double a = 0.0, b = 2.0, tol = 1e-8;
    treeweave_t h = treeweave_fit(f, 1, 1, &a, &b, tol, NULL, NULL);
    if (!h) { fprintf(stderr, "fit failed: %s\n", treeweave_last_error()); return 1; }
    double x = 1.5, y = 0.0;
    treeweave_eval(h, &x, &y);
    treeweave_free(h);
    double err = y - 2.25;
    if (err < 0) err = -err;
    if (err > 1e-6) { fprintf(stderr, "bad eval: %g\n", y); return 1; }
    printf("smoke OK: treeweave(x^2)(1.5) = %g\n", y);
    return 0;
}
C

# Git Bash hands CMake POSIX paths it cannot use, and the default VS generator
# is multi-config, which moves the binary into a per-config subdirectory.
gen=()
if command -v cygpath >/dev/null 2>&1; then
    gen=(-G Ninja -DCMAKE_BUILD_TYPE=Release)
    src="$(cygpath -m "$work")"
    prefix="$(cygpath -m "$prefix")"
else
    src="$work"
fi

cmake -S "$src" -B "$src/build" "${gen[@]}" -DCMAKE_PREFIX_PATH="$prefix"
cmake --build "$src/build"

# The exe lands in build/ with Ninja and build/<config>/ with a multi-config
# generator, so locate it instead of assuming either layout.
bin="$(find "$work/build" -maxdepth 2 -name 'smoke' -o -maxdepth 2 -name 'smoke.exe' | head -1)"
if [ -z "$bin" ]; then
    echo "no smoke binary under $work/build" >&2
    find "$work/build" -maxdepth 2 >&2
    exit 1
fi

# Windows has no RPATH: treeweave_c.dll installs to bin/ and the loader searches
# the exe directory and PATH, so a consumer must put bin/ on PATH.
if command -v cygpath >/dev/null 2>&1; then
    PATH="$(cygpath -u "$prefix")/bin:$PATH"
    export PATH
fi
"$bin"

# Confirm libtreeweave_c is the treeweave one (vendored/installed), not a system stray.
echo "--- linkage ---"
if command -v ldd >/dev/null 2>&1; then
    ldd "$bin" | grep -i treeweave || echo "(static link, no treeweave_c shared dep)"
elif command -v otool >/dev/null 2>&1; then
    otool -L "$bin" | grep -i treeweave || echo "(static link, no treeweave_c shared dep)"
elif command -v dumpbin >/dev/null 2>&1; then
    dumpbin //dependents "$bin"
fi
echo "smoke_find_package: PASS"
