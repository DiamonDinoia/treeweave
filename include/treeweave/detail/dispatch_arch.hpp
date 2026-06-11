#ifndef TREEWEAVE_DETAIL_DISPATCH_ARCH_HPP
#define TREEWEAVE_DETAIL_DISPATCH_ARCH_HPP

// dispatch_arch.hpp — the family-selected xsimd arch_list the C-ABI runtime
// dispatcher walks. Mirrors simdrng's include/random/dispatch_arch.hpp.
//
// arch_dispatch.cpp is compiled at each family's *baseline* `-march`
// (x86-64 → best_arch=sse2, armv8-a → neon64, rv64gc → rvv), so the
// std::conditional_t ladder below resolves to that family's list. The
// dispatcher (xsimd::dispatch over this list + available_architectures().has)
// then picks the widest host-supported variant at runtime.
//
// Per-family lists:
//   - x86      → the full ladder {avx512bw, fma3<avx2>, sse4_2, sse2}, matching
//                the four `-march` variant TUs CMake fans out (treeweave keeps a
//                richer list than simdrng's avx512f/avx2/sse2).
//   - aarch64  → {neon64} only. NEON64 is mandatory on ARMv8-A so it always
//                dispatches. SVE is deliberately excluded: xsimd's sve<N> bakes
//                the vector width in at compile time, but the runtime has(sve<N>)
//                probe only checks the SVE *presence* HWCAP bit, not the width —
//                a fixed-width SVE variant would falsely match mismatched-width
//                SVE hardware and mis-run. simdrng makes the same choice.
//   - riscv64  → {rvv128}. Best-effort / untested: no RISC-V CI runner. rvv128
//                fixes the vector length to match the dispatch TU's
//                -mrvv-vector-bits=zvl compile flag.

#include <type_traits>

#include <xsimd/xsimd.hpp>

namespace treeweave::capi {

// xsimd's x86 hierarchy has three independent roots that all derive from
// `common` (sse2, avx, avx512f), so "is this an x86 arch?" needs all three
// base checks — there is no single x86 base class to test against.
inline constexpr bool dispatch_is_x86 = std::is_base_of_v<xsimd::sse2, xsimd::best_arch> ||
                                        std::is_base_of_v<xsimd::avx, xsimd::best_arch> ||
                                        std::is_base_of_v<xsimd::avx512f, xsimd::best_arch>;

inline constexpr bool dispatch_is_aarch64 = std::is_base_of_v<xsimd::neon, xsimd::best_arch>;
// neon64 derives from neon: the aarch64 detection above, and the arch_list
// below that uses neon64, depend on this hierarchy. Guard it explicitly.
static_assert(std::is_base_of_v<xsimd::neon, xsimd::neon64>,
              "dispatch_arch: expected xsimd::neon64 to derive from xsimd::neon");

// Fixed-128-bit RVV, matching the dispatch TU's -mrvv-vector-bits=zvl flag.
using rvv128 = xsimd::detail::rvv<128>;

using dispatch_arch_list = std::conditional_t<
    dispatch_is_x86, xsimd::arch_list<xsimd::avx512bw, xsimd::fma3<xsimd::avx2>, xsimd::sse4_2, xsimd::sse2>,
    std::conditional_t<dispatch_is_aarch64, xsimd::arch_list<xsimd::neon64>, xsimd::arch_list<rvv128>>>;

} // namespace treeweave::capi

#endif
