#ifndef TREEWEAVE_DETAIL_QUANTIZE_HPP
#define TREEWEAVE_DETAIL_QUANTIZE_HPP

// SIMD leaf-id quantize kernels over a POD view of one PolyTree's leaf table.
// Header-only and arch-generic: a TU compiles these at its own -march, so the
// same code serves the header-only inline path and the per-ISA kernel TUs of
// the multi-arch C ABI (see kernels.hpp). `PolyTree` forwards here.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

#include <poet/poet.hpp>
#include <xsimd/xsimd.hpp>

#include <treeweave/detail/compiler_macros.hpp>

namespace treeweave::detail {

/// POD view of one PolyTree's quantize state: the leaf-id table plus the
/// per-axis affine map into it. Valid while the owning tree is alive.
template <class T, std::size_t IN>
struct QuantizeView {
    const std::uint32_t *table = nullptr;
    std::size_t          bits  = 0; // table depth: 2^bits bins per axis
    std::array<T, IN>    lower{}, upper{}, inv_span_bins{};
};

/// Quantize `x` (pointer to IN coords) to its leaf-table index. Positive-logic
/// domain gate `!(x >= lo && x <= hi)` rejects NaN/±Inf/OOD without UB (no
/// non-finite cast); closed upper endpoint clamped to last leaf.
template <class T, std::size_t IN>
[[nodiscard]] TREEWEAVE_ALWAYS_INLINE auto quantize_one(const QuantizeView<T, IN> &v, const T *x,
                                                        std::uint32_t ood_id) noexcept -> std::uint32_t {
    const std::size_t bits   = v.bits;
    const auto        mask_i = static_cast<std::int64_t>((std::size_t{1} << bits) - 1);
    if constexpr (IN == 1) {
        // Positive-logic gate: rejects OOD-low, finite OOD-high, NaN and ±Inf;
        // proves `x` finite and in range so the cast is UB-free.
        if (!(x[0] >= v.lower[0] && x[0] <= v.upper[0])) [[unlikely]]
            return ood_id;
        const auto fq0 = std::floor((x[0] - v.lower[0]) * v.inv_span_bins[0]);
        const auto q0  = std::min(static_cast<std::int64_t>(fq0), mask_i);
        return v.table[static_cast<std::size_t>(q0)];
    } else {
        std::size_t idx = 0;
        for (std::size_t d = 0; d < IN; ++d) {
            // Per-axis domain gate, identical in spirit to the 1D branch.
            if (!(x[d] >= v.lower[d] && x[d] <= v.upper[d])) [[unlikely]]
                return ood_id;
            const auto fqd = std::floor((x[d] - v.lower[d]) * v.inv_span_bins[d]);
            const auto qd  = std::min(static_cast<std::int64_t>(fqd), mask_i);
            idx |= static_cast<std::size_t>(qd) << (bits * d);
        }
        return v.table[idx];
    }
}

/// double→int32: vcvttpd2dq (7c lat/1CPI on SKX, half-width ymm) via direct
/// intrinsic — xsimd has no lane-matched double→int32 cast. Stub declared on
/// non-AVX2 so discarded if-constexpr arms still compile.
[[nodiscard]] TREEWEAVE_ALWAYS_INLINE auto
narrow_trunc_to_u32([[maybe_unused]] xsimd::batch<double> fq) noexcept {
#ifdef __AVX512F__
    return xsimd::batch<std::uint32_t, xsimd::avx2>(_mm512_cvttpd_epi32(fq.data)); // 8 doubles -> 8 i32 (ymm)
#elif defined(__AVX2__)
    return xsimd::batch<std::uint32_t, xsimd::sse2>(_mm256_cvttpd_epi32(fq.data)); // 4 doubles -> 4 i32 (xmm)
#else
    return xsimd::batch<std::uint32_t>{}; // unreachable: f64 fast path is x86 AVX2+ only
#endif
}

/// f32: floor→vcvttps2dq→vpminud→vpgatherdd→OOD-select. Matches `quantize_one`
/// lane-for-lane; broadcasts hoisted by caller. Only instantiated for `float`.
template <class T>
    requires(std::is_same_v<T, float>)
[[nodiscard]] TREEWEAVE_ALWAYS_INLINE auto
gather_leaf_ids(const std::uint32_t *table, xsimd::batch<T> x_v, xsimd::batch<T> lo_v, xsimd::batch<T> hi_v,
                xsimd::batch<T> inv_v, xsimd::batch<std::uint32_t> mask_v, xsimd::batch<std::uint32_t> ood_v) noexcept
    -> xsimd::batch<std::uint32_t> {
    const auto fq  = xsimd::floor((x_v - lo_v) * inv_v);
    const auto qi  = xsimd::min(xsimd::batch_cast<std::uint32_t>(xsimd::batch_cast<std::int32_t>(fq)), mask_v);
    const auto ids = xsimd::batch<std::uint32_t>::gather(table, xsimd::bitwise_cast<std::int32_t>(qi));
    const auto ood = ~((x_v >= lo_v) & (x_v <= hi_v));
    return xsimd::select(xsimd::batch_bool_cast<std::uint32_t>(ood), ood_v, ids);
}

/// SIMD quantize over batch lanes + scalar `on_id` callback per lane.
/// Gather/scatter to shared counters serialises (bank conflicts, FINUFFT
/// spread.hpp:454); hence scalar callback, not vector scatter.
template <class T, class OnId>
TREEWEAVE_ALWAYS_INLINE auto for_each_leaf_id_batch(const QuantizeView<T, 1> &v, const T *xp, std::uint32_t ood_id,
                                                    std::size_t n, OnId on_id) -> void {
    using batch_t                 = xsimd::batch<T>;
    constexpr std::size_t lanes   = batch_t::size;
    // cppcheck-suppress unreadVariable  ; read only through alignas below, which cppcheck does not count
    constexpr std::size_t aligned = batch_t::arch_type::alignment();

    // Conversion strategy: f32→vcvttps2dq (lane-matched, no ISA gate, xsimd
    // emulates below SSE2); f64→vcvttpd2dq+scalar loads on AVX2/AVX-512 (narrowing
    // lat 1c vs 4c for int64; vpgatherdd rejected >256 leaves).
#if defined(__AVX512F__) || defined(__AVX2__)
    constexpr bool kFastGatherF64 = std::is_same_v<T, double>;
#else
    constexpr bool kFastGatherF64 = false;
#endif
    constexpr bool kFastInt32 = std::is_same_v<T, float>;

    const std::size_t mask  = (std::size_t{1} << v.bits) - 1;
    const auto        lo_v  = batch_t::broadcast(v.lower[0]);
    const auto        hi_v  = batch_t::broadcast(v.upper[0]);
    const auto        inv_v = batch_t::broadcast(v.inv_span_bins[0]);

    // Branchless OOD select: `lane_ood` (from positive-logic SIMD mask) decides;
    // `qi` clamped unsigned so OOD/aarch64 saturating lanes stay in table bounds.
    auto resolve = [&](auto qi, bool lane_ood) -> std::uint32_t {
        using U      = std::make_unsigned_t<decltype(qi)>;
        const auto q = std::min(static_cast<U>(qi), static_cast<U>(mask));
        return lane_ood ? ood_id : v.table[static_cast<std::size_t>(q)];
    };

    // Round n down to a whole number of lanes. `lanes` is a power of two
    // (xsimd batch size), so this is a single mask — same codegen as the
    // div+mul, just explicit about the assumption.
    static_assert((lanes & (lanes - 1)) == 0, "lanes must be a power of two");
    const std::size_t n_simd = n & ~(lanes - 1);

    // Lane-matched signed-integer lane type for `T`:
    // `as_integer_t<float> == int32_t` (-> vcvttps2dq),
    // `as_integer_t<double> == int64_t` (-> vcvttpd2qq). Same width used by
    // the packed fast path, the per-lane sweep, and the scalar tail so a
    // point classifies identically whichever code path handles it.
    using int_t = xsimd::as_integer_t<T>;

    if constexpr (kFastInt32) {
        // f32: vpgatherdd of W ids (no ISA gate; xsimd scalar-loop below AVX2
        // still beats per-lane sweep ~1.7x). Consumer stays scalar — cross-lane
        // RMW can't scatter.
        const auto mask_v = xsimd::batch<std::uint32_t>::broadcast(static_cast<std::uint32_t>(mask));
        const auto ood_v  = xsimd::batch<std::uint32_t>::broadcast(ood_id);
        alignas(aligned) std::array<std::uint32_t, lanes> id_arr{};
        for (std::size_t i = 0; i < n_simd; i += lanes) {
            gather_leaf_ids(v.table, batch_t::load_unaligned(xp + i), lo_v, hi_v, inv_v, mask_v, ood_v)
                .store_aligned(id_arr.data());
            poet::static_for<static_cast<std::ptrdiff_t>(lanes)>([&](auto J) -> void { on_id(i + J, id_arr[J]); });
        }
    } else if constexpr (kFastGatherF64) {
        // f64 AVX2/AVX-512: vcvttpd2dq (lat 1c) → scalar table loads. No
        // vpgatherdd: the gathered table spills L1 at high leaf counts and
        // loses to scalar loads there.
        using idx_batch_t = decltype(narrow_trunc_to_u32(std::declval<batch_t>()));
        static_assert(idx_batch_t::size == lanes, "narrowed index batch must be lane-matched to the value batch");
        const auto mask_v = idx_batch_t::broadcast(static_cast<std::uint32_t>(mask));
        alignas(aligned) std::array<std::uint32_t, lanes> q_arr{};
        alignas(aligned) std::array<bool, lanes>          ood_arr{};
        for (std::size_t i = 0; i < n_simd; i += lanes) {
            const auto x_v  = batch_t::load_unaligned(xp + i);
            const auto fq_v = xsimd::floor((x_v - lo_v) * inv_v);
            // Positive-logic domain mask, identical to `quantize_one`'s gate:
            // a lane is OOD unless `lo <= x <= hi` (NaN/±Inf fail both
            // compares; finite `x > upper` maps to `ood_id`). On `x_v` not
            // `fq_v` so the closed upper endpoint `x == upper` stays in.
            (~((x_v >= lo_v) & (x_v <= hi_v))).store_aligned(ood_arr.data());
            // Clamp the narrowed index to [0, mask] (vpminud) so the scalar
            // table load stays in bounds; OOD lanes are remapped below.
            xsimd::min(narrow_trunc_to_u32(fq_v), mask_v).store_aligned(q_arr.data());
            poet::static_for<static_cast<std::ptrdiff_t>(lanes)>(
                [&](auto J) -> void { on_id(i + J, ood_arr[J] ? ood_id : v.table[q_arr[J]]); });
        }
    } else {
        // AVX2/SSE double: per-lane `vcvttsd2si` sweep (no packed
        // double->int64 truncate off AVX-512DQ). `floor` first so the
        // OOD-low sliver classifies identically to the fast path.
        // Non-finite guard mirrors the fast path: see comment above.
        alignas(aligned) std::array<T, lanes>    q_arr{};
        alignas(aligned) std::array<bool, lanes> ood_arr{};
        for (std::size_t i = 0; i < n_simd; i += lanes) {
            const auto x_v  = batch_t::load_unaligned(xp + i);
            const auto fq_v = xsimd::floor((x_v - lo_v) * inv_v);
            // Same positive-logic domain mask as the fast path above.
            (~((x_v >= lo_v) & (x_v <= hi_v))).store_aligned(ood_arr.data());
            fq_v.store_aligned(q_arr.data());
            // Per-lane `static_cast` (unlike the packed cast) is UB on a
            // non-finite value, so cast only the in-domain lanes; OOD lanes
            // pass a harmless 0 that `resolve` discards.
            poet::static_for<static_cast<std::ptrdiff_t>(lanes)>([&](auto J) -> void {
                const auto qd = ood_arr[J] ? int_t{0} : static_cast<int_t>(q_arr[J]);
                on_id(i + J, resolve(qd, ood_arr[J]));
            });
        }
    }
    // Scalar tail (the n < lanes leftover): same positive-logic domain gate
    // as the SIMD body. OOD-low/high and NaN/±Inf all fail it; the cast is
    // guarded because casting a non-finite value to int is UB (and aarch64
    // `fcvtzs` would silently fold NaN->0 / +Inf->INT_MAX).
    for (std::size_t i = n_simd; i < n; ++i) {
        const T    xi       = xp[i];
        const bool lane_ood = !(xi >= v.lower[0] && xi <= v.upper[0]);
        const auto fq       = std::floor((xi - v.lower[0]) * v.inv_span_bins[0]);
        const auto qi       = lane_ood ? int_t{0} : static_cast<int_t>(fq);
        on_id(i, resolve(qi, lane_ood));
    }
}

/// Write leaf ids for `n` points into `out`. f32: fully vectorized gather + store
/// (~4x vs scalar); f64: falls back to the generic callback.
template <class T>
auto leaf_ids_batch(const QuantizeView<T, 1> &v, const T *xp, std::uint32_t *out, std::uint32_t ood_id, std::size_t n)
    -> void {
    if constexpr (std::is_same_v<T, float>) {
        using batch_t                = xsimd::batch<T>;
        constexpr std::size_t lanes  = batch_t::size;
        const std::size_t     mask   = (std::size_t{1} << v.bits) - 1;
        const auto            lo_v   = batch_t::broadcast(v.lower[0]);
        const auto            hi_v   = batch_t::broadcast(v.upper[0]);
        const auto            inv_v  = batch_t::broadcast(v.inv_span_bins[0]);
        const auto            mask_v = xsimd::batch<std::uint32_t>::broadcast(static_cast<std::uint32_t>(mask));
        const auto            ood_v  = xsimd::batch<std::uint32_t>::broadcast(ood_id);

        const std::size_t n_simd = n & ~(lanes - 1);
        for (std::size_t i = 0; i < n_simd; i += lanes)
            gather_leaf_ids(v.table, batch_t::load_unaligned(xp + i), lo_v, hi_v, inv_v, mask_v, ood_v)
                .store_unaligned(out + i);
        // Tail (n % lanes leftover): for_each_leaf_id_batch with n=0 is a no-op.
        for_each_leaf_id_batch(v, xp + n_simd, ood_id, n - n_simd,
                               [&](std::size_t i, std::uint32_t id) -> void { out[n_simd + i] = id; });
        return;
    }
    for_each_leaf_id_batch(v, xp, ood_id, n, [&](std::size_t i, std::uint32_t id) -> void { out[i] = id; });
}

/// 1D leaf-table bin sort (stages 1-3 of the batch pipeline): histogram →
/// exclusive scan → scatter into `xp_packed` + `perm_inv`. Re-quantizes in the
/// scatter pass instead of materialising ids: the scatter is bound on the
/// counts[] RMW chain, so the re-quantize overlaps those stalls for free,
/// while a materialised id buffer adds L1 traffic that loses above a few
/// hundred leaves. On return `counts[k]` is the one-past-end of leaf k's
/// packed slice (Reinecke).
/// `static`: internal linkage, so the per-ISA kernel TUs that take this
/// function's address export no weak symbol another rung could collide with.
template <class T>
static auto partition_1d_table(const QuantizeView<T, 1> &v, const T *xp, std::size_t n_trg, std::uint32_t *counts,
                               std::uint32_t *perm_inv, T *xp_packed, std::uint32_t ood_id) -> void {
    const std::uint32_t n_leaves = ood_id; // sentinel id == n_leaves
    std::memset(counts, 0, (std::size_t{n_leaves} + 1) * sizeof(std::uint32_t));
    for_each_leaf_id_batch(v, xp, ood_id, n_trg,
                           [&](std::size_t /*i*/, std::uint32_t id) -> void { ++counts[id]; });
    // Exclusive scan, written out so no std algorithm symbol is emitted with
    // per-rung codegen.
    std::uint32_t acc = 0;
    for (std::uint32_t b = 0; b <= n_leaves; ++b) {
        const std::uint32_t c = counts[b];
        counts[b]             = acc;
        acc += c;
    }
    for_each_leaf_id_batch(v, xp, ood_id, n_trg, [&](std::size_t i, std::uint32_t id) -> void {
        const std::uint32_t dst = counts[id]++;
        perm_inv[i]             = dst;
        xp_packed[dst]          = xp[i];
    });
}

} // namespace treeweave::detail

#endif // TREEWEAVE_DETAIL_QUANTIZE_HPP
