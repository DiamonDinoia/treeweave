#!/usr/bin/env bash
# Smoke-test an installed/extracted treeweave C-ABI tree: a fresh consumer project
# must find_package(treeweave), link treeweave::treeweave_c, and fit+eval at runtime.
# Usage: smoke_find_package.sh <install-prefix>
set -euo pipefail

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

cmake -S "$work" -B "$work/build" -DCMAKE_PREFIX_PATH="$prefix" >/dev/null
cmake --build "$work/build" >/dev/null
"$work/build/smoke"

# Confirm libtreeweave_c is the treeweave one (vendored/installed), not a system stray.
bin="$work/build/smoke"
echo "--- linkage ---"
if command -v ldd >/dev/null 2>&1; then
	ldd "$bin" | grep -i treeweave || echo "(static link — no treeweave_c shared dep)"
elif command -v otool >/dev/null 2>&1; then
	otool -L "$bin" | grep -i treeweave || echo "(static link — no treeweave_c shared dep)"
fi
echo "smoke_find_package: PASS"
