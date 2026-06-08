#!/usr/bin/env bash
# run_parity.sh — cross-language parity check.
#
# Fits the same 2D -> 3D kernel in C (the reference), Python, and Julia, and
# verifies that Python and Julia reproduce the C reference's evaluations.
#
# Environment overrides:
#   TREEWEAVE_BUILD  build dir holding libtreeweave_c.{so,a}  (default: ../../build)
#   PYTHON        python interpreter with `treeweave` installed (default: python3)
#   JULIA         julia executable                       (default: julia)
#   TOL           max allowed abs difference vs C         (default: 1e-9)
#
# A language whose runtime / package is unavailable is reported and skipped;
# the C reference is mandatory. Exit status is nonzero if any compared language
# disagrees with C beyond TOL.
set -u

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
build="${TREEWEAVE_BUILD:-$repo/build}"
PYTHON="${PYTHON:-python3}"
JULIA="${JULIA:-julia}"
TOL="${TOL:-1e-9}"

lib_so="$build/libtreeweave_c.so"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

# --- C reference (mandatory) ---------------------------------------------
echo "== C reference =="
cc "$here/reference.c" -I"$repo/include" -L"$build" -ltreeweave_c -lm \
	-Wl,-rpath,"$build" -o "$tmp/reference" || {
	echo "FATAL: cannot build reference.c"
	exit 2
}
"$tmp/reference" >"$tmp/ref.csv" || {
	echo "FATAL: reference run failed"
	exit 2
}
cat "$tmp/ref.csv"

status=0

compare() { # name csv
	"$PYTHON" - "$tmp/ref.csv" "$2" "$TOL" "$1" <<'PYEOF'
import sys
ref, got, tol, name = sys.argv[1], sys.argv[2], float(sys.argv[3]), sys.argv[4]
R = [list(map(float, l.split(","))) for l in open(ref) if l.strip()]
G = [list(map(float, l.split(","))) for l in open(got) if l.strip()]
if len(R) != len(G):
    print(f"FAIL {name}: row count {len(G)} != reference {len(R)}"); sys.exit(1)
worst = 0.0
for r, g in zip(R, G):
    for a, b in zip(r, g):
        worst = max(worst, abs(a - b))
ok = worst <= tol
print(f"{'PASS' if ok else 'FAIL'} {name}: max |diff vs C| = {worst:.3e} (tol {tol:.0e})")
sys.exit(0 if ok else 1)
PYEOF
}

# --- Python --------------------------------------------------------------
echo "== Python =="
if "$PYTHON" -c "import treeweave" 2>/dev/null; then
	if "$PYTHON" "$here/parity.py" >"$tmp/py.csv" 2>"$tmp/py.err"; then
		compare Python "$tmp/py.csv" || status=1
	else
		echo "FAIL Python: parity.py errored"
		cat "$tmp/py.err"
		status=1
	fi
else
	echo "SKIP Python: 'treeweave' not importable in $PYTHON (pip install ./bindings/python)"
fi

# --- Julia ---------------------------------------------------------------
echo "== Julia =="
if command -v "$JULIA" >/dev/null 2>&1; then
	if LIBTREEWEAVE_C="$lib_so" "$JULIA" --project="$repo/bindings/julia/Treeweave" \
		"$here/parity.jl" >"$tmp/jl.csv" 2>"$tmp/jl.err"; then
		compare Julia "$tmp/jl.csv" || status=1
	else
		echo "FAIL Julia: parity.jl errored"
		cat "$tmp/jl.err"
		status=1
	fi
else
	echo "SKIP Julia: '$JULIA' not on PATH"
fi

echo
[ "$status" -eq 0 ] && echo "cross-language parity: OK" || echo "cross-language parity: MISMATCH"
exit "$status"
