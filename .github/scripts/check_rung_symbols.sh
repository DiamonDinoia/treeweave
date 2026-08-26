#!/usr/bin/env bash
# Fail if two ISA rungs of the multi-arch C ABI define the same external text
# symbol with different code.
#
# TREEWEAVE_C_MULTIARCH compiles kernels_arch.cpp once per ISA rung. A symbol
# a header instantiates at global scope is emitted weak by every rung, and the
# linker keeps one arbitrary copy; a kept copy from a higher rung executes on
# every CPU that loads the artifact, including CPUs that trap its
# instructions. The rung TUs keep everything internal except the Arch-mangled
# make_kernels_for instantiations (kernels_arch.cpp); this gate proves the
# property holds for any residue, with no ISA regex and
# no name pattern: every symbol that more than one object set defines must
# disassemble identically in all of them (offsets, hex immediates and
# relocation targets normalized), and must reference no TU-local symbol.
# The local-ref rule closes the stub hole: a shared symbol whose body is a
# jump into a rung-local clone disassembles identically in every rung, yet
# the kept copy executes its own rung's clone — identical bytes prove
# nothing when the ISA hides behind a local relocation. Data symbols carry
# no instructions and are exempt.
#
# Runs on the OBJECT files, which keep full symbol tables on ELF, Mach-O and
# COFF alike. A linked Windows DLL keeps none, so check_isa_leak.sh (the
# second gate, on the linked artifact) cannot cover Windows; this gate can.
#
# usage: check_rung_symbols.sh <rung>=<obj;obj;...> [<rung>=<obj;...> ...]
set -uo pipefail

nm=$(command -v llvm-nm || command -v llvm-nm-23 || command -v nm)
objdump=$(command -v llvm-objdump || command -v llvm-objdump-23 || command -v objdump)
# A missing tool must fail, never skip: a silent pass here is the exact hole
# this gate exists to close.
[[ -n "${nm:-}" && -n "${objdump:-}" ]] || { echo "check_rung_symbols: FAIL: no nm/objdump on PATH"; exit 2; }
(($# >= 2)) || { echo "check_rung_symbols: FAIL: need at least two <rung>=<objects> sets"; exit 2; }

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

rungs=()
for arg in "$@"; do
    rung=${arg%%=*}
    rungs+=("$rung")
    IFS=';' read -r -a objs <<<"${arg#*=}"
    printf '%s\n' "${objs[@]}" >"$work/$rung.objs"
    # External defined text symbols: T (strong) and W (weak, untyped/text).
    # V (weak object: vtables, typeinfo) holds no instructions.
    "$nm" --defined-only --extern-only "${objs[@]}" 2>/dev/null |
        awk '$2 ~ /^[TW]$/ {print $3}' | sort -u >"$work/$rung.syms"
    [[ -s "$work/$rung.syms" ]] ||
        { echo "check_rung_symbols: FAIL: rung '$rung' yields no external text symbols (bad paths or tool)"; exit 2; }
    # Locals (lowercase nm type), for the local-ref rule below.
    "$nm" --defined-only "${objs[@]}" 2>/dev/null |
        awk '$2 ~ /^[a-z]/ {print $3}' | sort -u >"$work/$rung.locals"
done

cat "$work"/*.syms | sort | uniq -d >"$work/shared"
n_shared=$(wc -l <"$work/shared")
if ((n_shared == 0)); then
    echo "check_rung_symbols: PASS: no symbol is defined by more than one object set"
    exit 0
fi

# Per rung: one normalized-disassembly stream per shared symbol, first
# occurrence only (later TUs of the same rung repeat the identical copy).
for rung in "${rungs[@]}"; do
    mapfile -t objs <"$work/$rung.objs"
    "$objdump" -d -r --no-show-raw-insn "${objs[@]}" 2>/dev/null |
        awk -v shared="$work/shared" -v refs="$work/$rung.refs" '
            BEGIN { while ((getline s < shared) > 0) keep[s] = 1 }
            /^[0-9a-f]+ <.*>:[ \t]*$/ {
                sym = $0
                sub(/^[0-9a-f]+ </, "", sym); sub(/>:[ \t]*$/, "", sym)
                cur = (sym in keep && !(sym in seen)) ? sym : ""
                if (cur != "") seen[sym] = 1
                next
            }
            # Relocation entry (-r interleaves them): record the target both
            # in the stream (name differences fail the byte compare) and in
            # the refs file (the local-ref rule checks it against nm).
            /^[ \t]+[0-9a-f]+:[ \t]+[A-Z][A-Z0-9_]*[ \t]/ && $2 ~ /^(R_|IMAGE_REL_|[A-Z0-9]+_RELOC_)/ {
                if (cur == "") next
                tgt = $3
                sub(/[+-]0x[0-9a-fA-F]+$/, "", tgt)
                print cur "\trel " tgt
                print cur "\t" tgt > refs
                next
            }
            /^[ \t]+[0-9a-f]+:/ {
                if (cur == "") next
                line = $0
                sub(/^[ \t]+[0-9a-f]+:[ \t]*/, "", line)  # instruction offset
                sub(/[ \t]*(#|\/\/).*$/, "", line)        # objdump comments
                gsub(/<[^>]*>/, "<>", line)               # relocation targets
                gsub(/0x[0-9a-fA-F]+/, "0x_", line)       # addresses, immediates
                gsub(/[ \t]+/, " ", line)
                print cur "\t" line
                next
            }
            { cur = "" }  # section/file headers and padding end the block
        ' >"$work/$rung.norm"
    : >>"$work/$rung.refs"
done

fail=0
n_compared=0
while IFS= read -r sym; do
    ref_rung=""
    for rung in "${rungs[@]}"; do
        grep -qFx -- "$sym" "$work/$rung.syms" || continue
        awk -F'\t' -v s="$sym" '$1 == s { print substr($0, length(s) + 2) }' "$work/$rung.norm" >"$work/cur"
        if [[ -z "$ref_rung" ]]; then
            mv "$work/cur" "$work/ref"
            ref_rung=$rung
            [[ -s "$work/ref" ]] && ((n_compared += 1))
        elif ! cmp -s "$work/ref" "$work/cur"; then
            echo "DIFFERS: $sym ($ref_rung vs $rung)"
            diff "$work/ref" "$work/cur" | head -8
            fail=1
        fi
    done
done <"$work/shared"

# Local-ref rule: a shared symbol that relocates against a TU-local symbol
# (or a section, always local) executes rung-private code or data through
# whichever copy the linker keeps — byte-identical bodies prove nothing.
for rung in "${rungs[@]}"; do
    while IFS=$'\t' read -r sym tgt; do
        [[ -n "$tgt" ]] || continue
        if [[ "$tgt" == .* ]] || grep -qFx -- "$tgt" "$work/$rung.locals"; then
            echo "LOCAL-REF: $sym references TU-local '$tgt' (rung $rung); make it internal or pin it to one TU"
            fail=1
        fi
    done < <(sort -u "$work/$rung.refs")
done

# All-empty streams mean the disassembly matched nothing: the procedure broke
# (format change, alias-only labels). Refuse the vacuous pass.
((n_compared > 0)) ||
    { echo "check_rung_symbols: FAIL: none of the $n_shared shared symbols disassembled"; exit 2; }

if ((fail)); then
    echo "check_rung_symbols: FAIL: shared symbols differ or reference TU-locals (see DIFFERS/LOCAL-REF above)"
    exit 1
fi
echo "check_rung_symbols: PASS: $n_shared shared symbols, all byte-identical across sets ($n_compared with code)"
