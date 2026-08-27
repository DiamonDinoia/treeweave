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
# Run this on a LINKED artifact (shared library, DLL, MEX), never on an object
# file. Before the link every rung holds its own copy by design, so an object
# file always looks like a leak. Only the link decides which copy survives.
#
# usage: check_isa_leak.sh <artifact> [more artifacts...]
set -uo pipefail

tmp=$(mktemp)
trap 'rm -f "$tmp"' EXIT

objdump=$(command -v llvm-objdump || command -v llvm-objdump-23 || command -v objdump)
test -n "$objdump" || {
    echo "no objdump found"
    exit 2
}

rc=0
for art in "$@"; do
    test -f "$art" || {
        echo "not a file: $art"
        exit 2
    }

    # AArch64 and RISC-V build a single rung, so no two copies can differ.
    fmt=$("$objdump" -f "$art" 2>/dev/null | head -20)
    test -n "$fmt" || {
        echo "$art: objdump read nothing"
        exit 2
    }
    if ! grep -qiE 'x86[-_]?64|i386' <<<"$fmt"; then
        echo "$art: not x86, single-rung, skipped"
        continue
    fi

    # Two kinds of symbol are per-rung by construction and carry a higher rung's
    # code legitimately: the fan-out's own, which mangle the rung into the name,
    # and anything in the variant TU's anonymous namespace, which mangles as
    # _GLOBAL__N_1 and is reachable only from the rung that defines it. Binding
    # cannot tell them apart from a leak: the fan-out builds with hidden
    # visibility, so the linker makes every leaked symbol local too.
    # Two properties come from the symbol table, both per name.
    #
    # Only code counts. Disassembling a data symbol yields bytes that decode as
    # plausible instructions: the Windows DLL reported a VEX encoding inside
    # treeweave_version_string. nm's type letter separates code from data.
    #
    # The copy count decides ownership. A leak is one symbol the linker chose
    # among several levels' copies, so it appears exactly once. A symbol GCC
    # emits privately per level, such as an .isra / .constprop clone, appears
    # once per level, and an ELF-local reference cannot bind outside its own
    # object, so each level reaches its own copy and no CPU meets another
    # level's code.
    nm=$(command -v llvm-nm || command -v nm)
    "$nm" "$art" 2>/dev/null | awk 'tolower($2) == "t" {print $3}' |
        sort | uniq -c | awk '{print $2, $1}' >"$tmp"

    # Every symbol the fan-out could leak has local linkage: the rungs build with
    # hidden visibility and the variant TU's names live in an anonymous namespace.
    # With the local names stripped, objdump labels each internal function with
    # whichever export precedes it, so an export is charged instructions it does
    # not own and no verdict is possible either way. A linked PE carries an empty
    # COFF table, and delocate / auditwheel strip the wheels down to exports.
    # check_rung_symbols.sh (TREEWEAVE_VERIFY_RUNGS) gates those trees instead: it
    # reads symbol tables that survive on every format.
    if [ "$("$nm" "$art" 2>/dev/null | awk '$2 == "t"' | wc -l)" -eq 0 ]; then
        echo "$art: no local text symbols, cannot attribute instructions, skipped"
        continue
    fi

    report=$("$objdump" -d --no-show-raw-insn "$art" 2>/dev/null | awk -v textsyms="$tmp" '
        BEGIN { while ((getline l < textsyms) > 0) { split(l, f, " "); text[f[1]] = f[2] } }
        /^[0-9a-f]+ [<(].*[>)]:$/ {
            sym = $0
            sub(/^[0-9a-f]+ [<(]/, "", sym)
            sub(/[>)]:$/, "", sym)
            tagged = (sym ~ /avx|sse|fma|neon|sve|rvv/) || (sym ~ /_GLOBAL__N_1/) \
                     || !(sym in text) || text[sym] > 1
            next
        }
        tagged { next }
        # EVEX: 512-bit registers or an opmask operand.
        /%zmm|%k[1-7]/ { bad[sym] = "AVX-512"; next }
        # VEX: a v-prefixed mnemonic on a vector register is AVX or higher.
        /\yv[a-z0-9]+[[:space:]]+.*%[xyz]mm/ { if (!(sym in bad)) bad[sym] = "AVX" }
        END { for (s in bad) printf "%-8s %s\n", bad[s], s }
    ')
    leaks=$(sort <<<"$report")

    if [ -n "$leaks" ]; then
        echo "$art: ISA leak, untagged symbols carrying above-baseline instructions"
        echo "$leaks"
        rc=1
    else
        echo "$art: no ISA leak"
    fi
done
exit $rc
