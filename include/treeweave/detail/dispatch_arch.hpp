#ifndef TREEWEAVE_DETAIL_DISPATCH_ARCH_HPP
#define TREEWEAVE_DETAIL_DISPATCH_ARCH_HPP

// dispatch_arch.hpp — the family-selected xsimd arch_list the C-ABI runtime
// dispatcher walks. Mirrors simdrng's include/random/dispatch_arch.hpp.
// Compiled at each family's baseline -march; xsimd::dispatch + available_architectures().has
// picks the widest host-supported variant at runtime.

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
// neon64 derives from neon — assert so a future xsimd rearrangement breaks loudly.
static_assert(std::is_base_of_v<xsimd::neon, xsimd::neon64>,
              "dispatch_arch: expected xsimd::neon64 to derive from xsimd::neon");

// Fixed-128-bit RVV, matching the dispatch TU's -mrvv-vector-bits=zvl flag.
using rvv128 = xsimd::detail::rvv<128>;

// x86 ladder: MSVC ABI compilers use avx instead of sse4_2 (no /arch:SSE4.2;
// they jump SSE2→AVX). Keep this list in lockstep with treeweave_c_dispatch.cmake.
// Each entry must equal a variant TU's xsimd::best_arch (see treeweave_c_dispatch.cmake).
#ifdef _MSC_VER
using x86_dispatch_list = xsimd::arch_list<xsimd::avx512bw, xsimd::fma3<xsimd::avx2>, xsimd::avx, xsimd::sse2>;
#else
using x86_dispatch_list = xsimd::arch_list<xsimd::avx512bw, xsimd::fma3<xsimd::avx2>, xsimd::sse4_2, xsimd::sse2>;
#endif

using dispatch_arch_list = std::conditional_t<
    dispatch_is_x86, x86_dispatch_list,
    std::conditional_t<dispatch_is_aarch64, xsimd::arch_list<xsimd::neon64>, xsimd::arch_list<rvv128>>>;

} // namespace treeweave::capi

#endif
