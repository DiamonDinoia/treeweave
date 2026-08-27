#!/usr/bin/env bash
#
# Give every ISA rung of the multi-arch C ABI its own copy of the code it
# shares with the other rungs, by renaming the rung's defined external symbols.
#
# COFF has no visibility control. Both cl.exe and clang-cl emit every inline
# function, template instantiation, RTTI record and float-pool entry as an
# external COMDAT, so the same header compiled at four /arch: levels defines
# the same symbol four times with four different instruction streams. The
# linker keeps one arbitrary copy: either a high rung runs baseline code, or
# the baseline runs AVX-512 and faults with an illegal instruction. Renaming
# the rung's copies removes the choice -- each rung calls its own code.
#
# ELF needs none of this: the rungs build with hidden visibility, which already
# makes these symbols local. Do not reach for --localize-symbol to get the same
# effect on ELF; that leaves the section in its COMDAT group, the linker still
# discards the group, and the local reference points into a discarded section.
# Renaming keeps the symbol external, so no group is ever merged away.
#
# usage: rename_rung_symbols.sh <tag> <keep-pattern> <object>...
#
#   tag           rung name, used as the symbol prefix (e.g. avx2)
#   keep-pattern  grep -E pattern for the symbols that must stay callable from
#                 the baseline TUs (the kernel-table factory)
#
# Renaming is idempotent: a symbol already carrying the prefix is left alone,
# so the step is safe to re-run on an object that did not get rebuilt.

set -euo pipefail

if [[ $# -lt 3 ]]; then
    echo "usage: $0 <tag> <keep-pattern> <object>..." >&2
    exit 2
fi

tag=$1
keep=$2
shift 2

NM=${LLVM_NM:-llvm-nm}
OBJCOPY=${LLVM_OBJCOPY:-llvm-objcopy}

# A missing tool must fail the build, never skip the rename: an unrenamed rung
# links without complaint and the collision is invisible until it faults.
for tool in "$NM" "$OBJCOPY"; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "rename_rung_symbols: '$tool' not found; the multi-arch COFF build needs" >&2
        echo "  llvm-nm and llvm-objcopy (LLVM_NM / LLVM_OBJCOPY override the names)." >&2
        exit 1
    fi
done

prefix="tw\$${tag}\$"
renamed_total=0

# Defined external names, one per line. llvm-nm prints "<addr> <type> <name>";
# MSVC names contain no spaces, so dropping the first two columns is exact.
# Every caller below goes through this, so no two of them can disagree on the
# format -- the keep pattern is anchored against a bare name.
defined_names() {
    "$NM" --defined-only --extern-only "$1" | sed 's/^[0-9a-fA-F]* [A-Za-z] //'
}

for obj in "$@"; do
    if [[ ! -f $obj ]]; then
        echo "rename_rung_symbols: no such object: $obj" >&2
        exit 1
    fi

    all=$(mktemp)
    map=$(mktemp)
    # Split from the filtering below so a failure of nm itself aborts here,
    # instead of looking like "no symbols to rename".
    defined_names "$obj" >"$all"
    # One awk, not a grep chain: under pipefail a grep that selects nothing
    # exits 1 and would abort the script on an already-renamed object.
    awk -v keep="$keep" -v p="$prefix" '
        $0 ~ keep            { next }
        index($0, p) == 1    { next }
        NF                   { printf "%s %s%s\n", $0, p, $0 }
    ' "$all" >"$map"
    rm -f "$all"

    n=$(awk 'END { print NR }' "$map")
    if [[ $n -gt 0 ]]; then
        "$OBJCOPY" --redefine-syms="$map" "$obj" "$obj"
        renamed_total=$((renamed_total + n))
    fi
    rm -f "$map"

    # The factory must survive, or the baseline TUs lose their entry point and
    # the failure surfaces as a link error with no explanation. Counted, not
    # `grep -q`: -q closes the pipe, nm dies on SIGPIPE and pipefail then
    # reports the whole pipeline as failed even when the symbol is there.
    kept=$(defined_names "$obj" | grep -Ec "$keep" || true)
    if [[ $kept -eq 0 ]]; then
        echo "rename_rung_symbols: $obj defines nothing matching '$keep' after the" >&2
        echo "  rename; the rung would export no kernel factory." >&2
        exit 1
    fi
done

echo "rename_rung_symbols: $tag: renamed $renamed_total symbols across $# object(s)"
