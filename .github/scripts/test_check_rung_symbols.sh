#!/usr/bin/env bash
# Positive controls for check_rung_symbols.sh: prove the gate fires on known-bad
# object sets before a clean run on the real rungs is believed.
#
# A gate that has only ever passed is indistinguishable from a gate that cannot
# fail. Each case below builds its own fixture objects with the project's own C
# compiler, so the check runs against whatever object format that compiler
# emits (ELF, Mach-O or COFF).
#
# usage: test_check_rung_symbols.sh <cc> <msvc-style:0|1>
set -uo pipefail

gate=$(dirname "${BASH_SOURCE[0]}")/check_rung_symbols.sh
cc=${1:?usage: test_check_rung_symbols.sh <cc> <msvc-style:0|1>}
msvc=${2:?usage: test_check_rung_symbols.sh <cc> <msvc-style:0|1>}

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# -O0 keeps the static helper a real call, so the LOCAL-REF case has a
# relocation to inspect instead of an inlined body.
compile() { # compile <src> <obj>
    if ((msvc)); then
        "$cc" /nologo /c /Od "$1" "/Fo$2" >/dev/null 2>&1
    else
        "$cc" -c -O0 "$1" -o "$2" >/dev/null 2>&1
    fi
}

fail=0
check() { # check <name> <expected-rc> <args...>
    local name=$1 want=$2
    shift 2
    local out
    out=$(bash "$gate" "$@" 2>&1)
    local got=$?
    if ((got == want)); then
        printf 'ok   %-28s rc=%d\n' "$name" "$got"
    else
        printf 'FAIL %-28s rc=%d, wanted %d\n%s\n' "$name" "$got" "$want" "$out"
        fail=1
    fi
}

printf 'int other_fn(int x) { return x - 1; }\n' >"$work/other.c"
printf 'int shared_fn(int x) { return x + 1; }\n' >"$work/plus.c"
printf 'int shared_fn(int x) { return x * 3; }\n' >"$work/times.c"
# Identical shared_fn in both rungs, but each calls its own TU-local helper.
printf 'static int helper(int x) { return x + 1; }\nint shared_fn(int x) { return helper(x); }\n' >"$work/loc_a.c"
printf 'static int helper(int x) { return x * 3; }\nint shared_fn(int x) { return helper(x); }\n' >"$work/loc_b.c"

for f in other plus times loc_a loc_b; do
    compile "$work/$f.c" "$work/$f.o" || {
        echo "test_check_rung_symbols: FAIL: cannot compile fixture $f.c with $cc"
        exit 2
    }
done

# The controls that must FIRE. Without these the gate proves nothing.
check "differing bodies" 1 "a=$work/plus.o" "b=$work/times.o"
check "local-ref stub" 1 "a=$work/loc_a.o" "b=$work/loc_b.o"

# The controls that must NOT fire, so a firing gate is not merely stuck at 1.
check "identical bodies" 0 "a=$work/plus.o" "b=$work/plus.o"
check "no shared symbol" 0 "a=$work/plus.o" "b=$work/other.o"

# Misuse must be loud, never a silent pass.
check "one rung only" 2 "a=$work/plus.o"
check "unreadable object" 2 "a=$work/nope.o" "b=$work/plus.o"

((fail)) && {
    echo "test_check_rung_symbols: FAIL"
    exit 1
}
echo "test_check_rung_symbols: PASS: the gate fires on both bad cases and stays quiet on both good ones"
