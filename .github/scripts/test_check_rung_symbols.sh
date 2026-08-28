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

gate=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/check_rung_symbols.sh
cc=${1:?usage: test_check_rung_symbols.sh <cc> <msvc-style:0|1>}
msvc=${2:?usage: test_check_rung_symbols.sh <cc> <msvc-style:0|1>}

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
# Every fixture path stays relative to $work: MSYS bash rewrites a POSIX path
# before a native Windows tool sees it, and the rewrite is not reversible.
cd "$work" || exit 2

# -O0 keeps the static helper a real call, so the LOCAL-REF case has a
# relocation to inspect instead of an inlined body.
compile() { # compile <src> <obj>
    if ((msvc)); then
        # cl.exe takes its options with either prefix, and the "-" form is the
        # one that survives MSYS bash, which rewrites any argument starting "/".
        "$cc" -nologo -c -Od "$1" "-Fo$2"
    else
        "$cc" -c -O0 "$1" -o "$2"
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

printf 'int other_fn(int x) { return x - 1; }\n' >other.c
printf 'int shared_fn(int x) { return x + 1; }\n' >plus.c
printf 'int shared_fn(int x) { return x * 3; }\n' >times.c
# Identical shared_fn in both rungs, but each calls its own TU-local helper.
printf 'static int helper(int x) { return x + 1; }\nint shared_fn(int x) { return helper(x); }\n' >loc_a.c
printf 'static int helper(int x) { return x * 3; }\nint shared_fn(int x) { return helper(x); }\n' >loc_b.c

for f in other plus times loc_a loc_b; do
    if ! out=$(compile "$f.c" "$f.o" 2>&1); then
        printf 'test_check_rung_symbols: FAIL: cannot compile fixture %s.c with %s\n%s\n' \
            "$f" "$cc" "$out"
        exit 2
    fi
done

# The controls that must FIRE. Without these the gate proves nothing.
check "differing bodies" 1 "a=plus.o" "b=times.o"
check "local-ref stub" 1 "a=loc_a.o" "b=loc_b.o"

# The controls that must NOT fire, so a firing gate is not merely stuck at 1.
check "identical bodies" 0 "a=plus.o" "b=plus.o"
check "no shared symbol" 0 "a=plus.o" "b=other.o"

# Misuse must be loud, never a silent pass.
check "one rung only" 2 "a=plus.o"
check "unreadable object" 2 "a=nope.o" "b=plus.o"

((fail)) && {
    echo "test_check_rung_symbols: FAIL"
    exit 1
}
echo "test_check_rung_symbols: PASS: the gate fires on both bad cases and stays quiet on both good ones"
