#!/usr/bin/env bash
# Fail if a symbol whose mangled name carries no ISA tag holds an instruction
# above the family baseline.
#
# TREEWEAVE_C_MULTIARCH compiles the same sources once per ISA rung. Symbols the
# fan-out controls encode the rung in their mangled name, so the rungs never
# collide. Symbols instantiated from headers at global scope do not: every rung
# emits the same weak name with different code, and the linker keeps one
# arbitrary copy. A kept copy from a higher rung executes on every CPU that
# loads the artifact, including CPUs that trap the instruction.
#
# usage: check_isa_leak.sh <artifact> [more artifacts...]
set -uo pipefail

objdump=$(command -v llvm-objdump || command -v llvm-objdump-23 || command -v objdump)
test -n "$objdump" || { echo "no objdump found"; exit 2; }

tmp=$(mktemp)
trap 'rm -f "$tmp"' EXIT

rc=0
for art in "$@"; do
    test -f "$art" || { echo "not a file: $art"; exit 2; }

    # AArch64 and RISC-V build a single rung, so no two copies can differ.
    fmt=$("$objdump" -f "$art" 2>/dev/null | head -20)
    test -n "$fmt" || { echo "$art: objdump read nothing"; exit 2; }
    if ! grep -qiE 'x86[-_]?64|i386' <<<"$fmt"; then
        echo "$art: not x86, single-rung, skipped"
        continue
    fi

    # A local symbol is reachable only from the object that defines it, so a rung's
    # private copy is correct by construction. Only a symbol the linker shares
    # between rungs can carry a higher rung's code into a lower rung's path.
    nm=$(command -v llvm-nm || command -v nm)
    "$nm" "$art" 2>/dev/null | awk '$2 ~ /^[a-z]$/ {print $3}' | sort -u > "$tmp"

    leaks=$("$objdump" -d --no-show-raw-insn "$art" 2>/dev/null | awk -v locals="$tmp" '
        BEGIN { while ((getline l < locals) > 0) local[l] = 1 }
        /^[0-9a-f]+ [<(].*[>)]:$/ {
            sym = $0
            sub(/^[0-9a-f]+ [<(]/, "", sym)
            sub(/[>)]:$/, "", sym)
            tagged = (sym ~ /avx|sse|fma|neon|sve|rvv/) || (sym in local)
            next
        }
        tagged { next }
        # EVEX: 512-bit registers or an opmask operand.
        /%zmm|%k[1-7]/ { bad[sym] = "AVX-512"; next }
        # VEX: a v-prefixed mnemonic on a vector register is AVX or higher.
        /\yv[a-z0-9]+[[:space:]]+.*%[xyz]mm/ { if (!(sym in bad)) bad[sym] = "AVX" }
        END { for (s in bad) printf "%-8s %s\n", bad[s], s }
    ' | sort)

    if [ -n "$leaks" ]; then
        echo "$art: ISA leak, untagged symbols carrying above-baseline instructions"
        echo "$leaks"
        rc=1
    else
        echo "$art: no ISA leak"
    fi
done
exit $rc
