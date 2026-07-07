#ifndef TREEWEAVE_DETAIL_POLYTREE_HPP
#define TREEWEAVE_DETAIL_POLYTREE_HPP

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <queue>
#include <type_traits>
#include <utility>
#include <vector>

#include <poet/poet.hpp>
#include <polyfit/polyfit.hpp>
#include <xsimd/xsimd.hpp>

#include <treeweave/detail/compiler_macros.hpp>
#include <treeweave/detail/errors.hpp>
#include <treeweave/detail/eval_policy.hpp>
#include <treeweave/detail/node.hpp>
#include <treeweave/detail/numerics.hpp>
#include <treeweave/detail/tol_kind.hpp>
#include <treeweave/detail/value.hpp>

namespace treeweave::detail {

/// One BFS-built subtree: a flat node array plus an optional
/// quantize→leaf table for shallow trees. The eval-time hot path
/// (`get_node_index`, `find_leaf_id_with_ood`) lives here.
template <std::size_t Degree, class Func, EvalPolicy Policy = EvalPolicy::Balanced>
struct PolyTree {
    using input_type     = std::remove_cvref_t<poly_eval::fitInput_t<Func>>;
    using value_type     = poly_eval::detail::value_type_or_t<input_type>;
    using poly_eval_type = poly_eval_type_for<Func, Degree, Policy>;
    using output_type    = poly_eval::fitOutput_t<Func>;

    static constexpr std::size_t output_dim = value_dim_v<output_type>;
    static constexpr std::size_t input_dim  = value_dim_v<input_type>;
    static constexpr std::size_t n_child    = std::size_t{1} << input_dim;

    using node_t      = Node<Func, Degree, Policy>;
    using box_t       = Box<value_type, input_dim>;
    using dim_array_t = Value<value_type, input_dim>;

    PolyTree(const TreeInput &input, const Box<value_type, input_dim> &root_box, std::vector<poly_eval_type> &polyfits,
             const Func &func)
        : lower_(root_box.center - root_box.half_length), upper_(root_box.center + root_box.half_length) {
        std::queue<box_t> q;
        dim_array_t       half_width = root_box.half_length * value_type{0.5};
        q.push(root_box);

        index_t curr_child_idx = 1;

        while (!q.empty()) {
            const std::size_t n_next     = q.size();
            const std::size_t node_index = nodes_.size();
            // True when this is the last allowed level: any node that fails
            // its tolerance check here cannot be subdivided further.
            const bool at_max_depth = std::cmp_equal(max_depth_, input.max_depth);
            // Force uniform refinement to the requested depth before
            // accepting any leaf, so the per-subtree quantize→leaf table
            // (built at the bottom of this BFS) covers the whole domain
            // at a known minimum depth. Useful when the caller wants the
            // SIMD-quantize fast path active on functions where tol-based
            // refinement would otherwise leave a ragged tree.
            const bool force_subdivide = std::cmp_less(max_depth_, input.min_uniform_depth) && !at_max_depth;
            for (std::size_t i = 0; i < n_next; ++i) {
                box_t const current_box = q.front();
                q.pop();

                nodes_.emplace_back();

                auto &node           = nodes_[i + node_index];
                bool  successful_fit = node.fit(input, func, current_box.center, current_box.half_length, polyfits);

                // Force-uniform: roll back the just-accepted polynomial and
                // treat the node as failing convergence, so the subdivide
                // branch below adds children. Keeps the leaf-table fast
                // path live at the depth the user asked for.
                if (successful_fit && force_subdivide) {
                    polyfits.pop_back();
                    node.set_poly_eval_id(Node<Func, Degree, Policy>::kLeafSentinel);
                    successful_fit = false;
                }

                if (successful_fit) {
                    assert(polyfits.size() > 0);
                    assert(node.poly_eval_id() == polyfits.size() - 1);
                } else if (at_max_depth) {
                    // Record the failed panel and force-accept the polynomial
                    // as a best-effort leaf. The decision to throw or accept
                    // is made at the Function level after all subtrees finish,
                    // so a multi-subtree fit can report panels from every
                    // failing subtree rather than just the first one to throw.
                    const auto a_arr = (current_box.center - current_box.half_length).as_array();
                    const auto b_arr = (current_box.center + current_box.half_length).as_array();
                    non_converged_panels_.push_back(
                        NonConvergedPanel{.a     = std::vector<double>(a_arr.begin(), a_arr.end()),
                                          .b     = std::vector<double>(b_arr.begin(), b_arr.end()),
                                          .depth = max_depth_});
                    node.force_fit_as_leaf(func, current_box.center, current_box.half_length, polyfits);
                } else {
                    node.set_first_child_idx(static_cast<std::uint32_t>(curr_child_idx));
                    curr_child_idx += n_child;

                    const dim_array_t &node_center = current_box.center;
                    for (index_t child = 0; child < n_child; ++child) {
                        dim_array_t center_offset;

                        // Extract sign of each offset component from the bits of child.
                        for (std::size_t j = 0; j < input_dim; ++j) {
                            const std::array<value_type, 2> signed_hw{-half_width[j], half_width[j]};
                            center_offset[j] = node_center[j] + signed_hw[(child >> j) & index_t{1}];
                        }

                        q.push(box_t(center_offset, half_width));
                    }
                }
            }

            // At max_depth, all failing panels were force-accepted as leaves;
            // q stays empty for those, so the BFS terminates cleanly. The
            // throw/accept decision happens at the Function level once all
            // subtrees have finished, in `gather_non_converged_panels()`.

            if (!q.empty())
                ++max_depth_;
            if (input.max_memory_mib > 0 && !q.empty()) {
                const std::size_t budget =
                    static_cast<std::size_t>(input.max_memory_mib) * std::size_t{1024} * std::size_t{1024};
                const std::size_t used = polyfits.size() * sizeof(poly_eval_type) + nodes_.size() * sizeof(node_t);
                if (used > budget) {
                    const auto &offender = q.front();
                    throw MemoryBudgetExceeded(used, budget, offender.center.as_array(),
                                               offender.half_length.as_array());
                }
            }

            half_width = half_width * value_type{0.5};
        }

        // Build a quantize-to-leaf table for shallow subtrees (cap: 64 K uint32
        // entries, 256 KiB). Replaces per-level descent (~3x slower at depth 15-16).
        // (see devel/agents/perf-notes.md)
        constexpr std::size_t kTableMaxEntries = std::size_t{1} << 16; // 256 KiB / 4 B
        if (max_depth_ > 0) {
            const std::size_t total_bits = input_dim * max_depth_;
            if (total_bits <= 16) {
                const std::size_t n = std::size_t{1} << total_bits;
                if (n <= kTableMaxEntries) {
                    leaf_table_.assign(n, std::uint32_t{0});
                    leaf_table_depth_ = max_depth_;
                    for (std::size_t d = 0; d < input_dim; ++d) {
                        const value_type span = upper_[d] - lower_[d];
                        inv_span_bins_[d]     = static_cast<value_type>(std::size_t{1} << max_depth_) / span;
                    }
                    const auto bins = static_cast<value_type>(std::size_t{1} << max_depth_);
                    for (std::size_t i = 0; i < n; ++i) {
                        // Decode i into per-axis quantize indices q[d].
                        // NOLINTNEXTLINE(misc-const-correctness) — mutated by `r >>= bits` in the ND branch below.
                        std::size_t       r    = i;
                        const std::size_t bits = max_depth_;
                        const std::size_t mask = (std::size_t{1} << bits) - 1;
                        // Compute cell-center x and descend the tree. The
                        // dispatch is on the *shape* of `input_type`, not on
                        // `input_dim`: a scalar-input 1D fit needs a plain
                        // `value_type` xc to match `get_node_index`'s
                        // signature, while `array<T,1>` and ND inputs share
                        // the indexed path below.
                        if constexpr (!poly_eval::detail::hasTupleSize_v<input_type>) {
                            const std::size_t q0   = r & mask;
                            const value_type  span = upper_[0] - lower_[0];
                            const value_type  cell = span / bins;
                            const value_type  xc   = lower_[0] + (static_cast<value_type>(q0) + value_type{0.5}) * cell;
                            leaf_table_[i]         = nodes_[get_node_index(xc)].poly_eval_id();
                        } else {
                            input_type xc;
                            for (std::size_t d = 0; d < input_dim; ++d) {
                                const std::size_t qd = r & mask;
                                r >>= bits;
                                const value_type span = upper_[d] - lower_[d];
                                const value_type cell = span / bins;
                                xc[d] = lower_[d] + (static_cast<value_type>(qd) + value_type{0.5}) * cell;
                            }
                            leaf_table_[i] = nodes_[get_node_index(xc)].poly_eval_id();
                        }
                    }
                }
            }
        }
    }

    [[nodiscard]] auto find_node(const input_type &x) const -> const node_t & { return nodes_[get_node_index(x)]; }

    /// Leaf-id lookup: table fast path when available, else tree descent.
    [[nodiscard]] auto find_leaf_id(const input_type &x) const -> std::uint32_t {
        if (!leaf_table_.empty()) {
            const std::size_t bits = leaf_table_depth_;
            const std::size_t mask = (std::size_t{1} << bits) - 1;
            std::size_t       idx  = 0;
            poet::static_for<input_dim>([&](auto D) -> void {
                constexpr std::size_t d  = D;
                const value_type      xd = [&]() -> value_type {
                    if constexpr (poly_eval::detail::hasTupleSize_v<input_type>)
                        return x[d];
                    else
                        return x;
                }();
                auto q = static_cast<std::size_t>((xd - lower_[d]) * inv_span_bins_[d]);
                q      = std::min(q, mask);
                idx |= q << (bits * d);
            });
            return leaf_table_[idx];
        }
        return nodes_[get_node_index(x)].poly_eval_id();
    }

    [[nodiscard]] constexpr auto has_leaf_table() const noexcept -> bool { return !leaf_table_.empty(); }

    /// Entry count of the leaf-id quantize table; 0 when not built.
    /// Used by `Function::print_stats` to report fast-path memory.
    [[nodiscard]] constexpr auto leaf_table_size() const noexcept -> std::size_t { return leaf_table_.size(); }

    /// Quantize `x` to its leaf-table index. Positive-logic domain gate
    /// `!(x >= lo && x <= hi)` rejects NaN/±Inf/OOD without UB (no non-finite
    /// cast); closed upper endpoint clamped to last leaf. (see devel/agents/perf-notes.md)
    [[nodiscard]] TREEWEAVE_ALWAYS_INLINE auto quantize_one(const input_type &x, std::uint32_t ood_id) const noexcept
        -> std::uint32_t {
        const std::size_t bits   = leaf_table_depth_;
        const auto        mask_i = static_cast<std::int64_t>((std::size_t{1} << bits) - 1);
        if constexpr (!poly_eval::detail::hasTupleSize_v<input_type>) {
            // Positive-logic gate: rejects OOD-low, finite OOD-high, NaN and ±Inf;
            // proves `x` finite and in range so the cast is UB-free. (see devel/agents/perf-notes.md)
            if (!(x >= lower_[0] && x <= upper_[0])) [[unlikely]]
                return ood_id;
            const auto fq0 = std::floor((x - lower_[0]) * inv_span_bins_[0]);
            const auto q0  = std::min(static_cast<std::int64_t>(fq0), mask_i);
            return leaf_table_[static_cast<std::size_t>(q0)];
        } else {
            std::size_t idx = 0;
            for (std::size_t d = 0; d < input_dim; ++d) {
                // Per-axis domain gate, identical in spirit to the 1D branch.
                if (!(x[d] >= lower_[d] && x[d] <= upper_[d])) [[unlikely]]
                    return ood_id;
                const auto fqd = std::floor((x[d] - lower_[d]) * inv_span_bins_[d]);
                const auto qd  = std::min(static_cast<std::int64_t>(fqd), mask_i);
                idx |= static_cast<std::size_t>(qd) << (bits * d);
            }
            return leaf_table_[idx];
        }
    }

    /// double→int32: vcvttpd2dq (7c lat/1CPI on SKX, half-width ymm) via direct
    /// intrinsic — xsimd has no lane-matched double→int32 cast. Stub declared on
    /// non-AVX2 so discarded if-constexpr arms still compile. (see devel/agents/perf-notes.md)
    [[nodiscard]] TREEWEAVE_ALWAYS_INLINE static auto
    narrow_trunc_to_u32([[maybe_unused]] xsimd::batch<double> fq) noexcept {
#ifdef __AVX512F__
        return xsimd::batch<std::uint32_t, xsimd::avx2>(_mm512_cvttpd_epi32(fq.data)); // 8 doubles -> 8 i32 (ymm)
#elif defined(__AVX2__)
        return xsimd::batch<std::uint32_t, xsimd::sse2>(_mm256_cvttpd_epi32(fq.data)); // 4 doubles -> 4 i32 (xmm)
#else
        return xsimd::batch<std::uint32_t>{}; // unreachable: f64 fast path is x86 AVX2+ only
#endif
    }

    /// SIMD quantize over batch lanes + scalar `on_id` callback per lane.
    /// Gather/scatter to shared counters serialises (bank conflicts, FINUFFT
    /// spread.hpp:454); hence scalar callback, not vector scatter. (see devel/agents/perf-notes.md)
    template <class OnId>
    TREEWEAVE_ALWAYS_INLINE auto for_each_leaf_id_batch(const value_type *xp, std::uint32_t ood_id, std::size_t n,
                                                        OnId on_id) const -> void
        requires(input_dim == 1)
    {
        using batch_t                 = xsimd::batch<value_type>;
        constexpr std::size_t lanes   = batch_t::size;
        constexpr std::size_t aligned = batch_t::arch_type::alignment();

        // Conversion strategy: f32→vcvttps2dq (lane-matched, no ISA gate, xsimd
        // emulates below SSE2); f64→vcvttpd2dq+scalar loads on AVX2/AVX-512 (narrowing
        // lat 1c vs 4c for int64; vpgatherdd rejected >256 leaves, see perf-notes.md).
#if defined(__AVX512F__) || defined(__AVX2__)
        constexpr bool kFastGatherF64 = std::is_same_v<value_type, double>;
#else
        constexpr bool kFastGatherF64 = false;
#endif
        constexpr bool kFastInt32 = std::is_same_v<value_type, float>;

        const std::size_t mask  = (std::size_t{1} << leaf_table_depth_) - 1;
        const auto        lo_v  = batch_t::broadcast(lower_[0]);
        const auto        hi_v  = batch_t::broadcast(upper_[0]);
        const auto        inv_v = batch_t::broadcast(inv_span_bins_[0]);

        // Branchless OOD select: `lane_ood` (from positive-logic SIMD mask) decides;
        // `qi` clamped unsigned so OOD/aarch64 saturating lanes stay in table bounds.
        // (see devel/agents/perf-notes.md)
        auto resolve = [&](auto qi, bool lane_ood) -> std::uint32_t {
            using U      = std::make_unsigned_t<decltype(qi)>;
            const auto q = std::min(static_cast<U>(qi), static_cast<U>(mask));
            return lane_ood ? ood_id : leaf_table_[static_cast<std::size_t>(q)];
        };

        // Round n down to a whole number of lanes. `lanes` is a power of two
        // (xsimd batch size), so this is a single mask — same codegen as the
        // div+mul, just explicit about the assumption.
        static_assert((lanes & (lanes - 1)) == 0, "lanes must be a power of two");
        const std::size_t n_simd = n & ~(lanes - 1);

        // Lane-matched signed-integer lane type for `value_type`:
        // `as_integer_t<float> == int32_t` (-> vcvttps2dq),
        // `as_integer_t<double> == int64_t` (-> vcvttpd2qq). Same width used by
        // the packed fast path, the per-lane sweep, and the scalar tail so a
        // point classifies identically whichever code path handles it.
        using int_t = xsimd::as_integer_t<value_type>;

        if constexpr (kFastInt32) {
            // f32: vpgatherdd of W ids (no ISA gate; xsimd scalar-loop below AVX2
            // still beats per-lane sweep ~1.7x). Consumer stays scalar — cross-lane
            // RMW can't scatter. (see devel/agents/perf-notes.md)
            const auto mask_v = xsimd::batch<std::uint32_t>::broadcast(static_cast<std::uint32_t>(mask));
            const auto ood_v  = xsimd::batch<std::uint32_t>::broadcast(ood_id);
            alignas(aligned) std::array<std::uint32_t, lanes> id_arr{};
            for (std::size_t i = 0; i < n_simd; i += lanes) {
                gather_leaf_ids(batch_t::load_unaligned(xp + i), lo_v, hi_v, inv_v, mask_v, ood_v)
                    .store_aligned(id_arr.data());
                poet::static_for<static_cast<std::ptrdiff_t>(lanes)>([&](auto J) -> void { on_id(i + J, id_arr[J]); });
            }
        } else if constexpr (kFastGatherF64) {
            // f64 AVX2/AVX-512: vcvttpd2dq (lat 1c) → scalar table loads. No
            // vpgatherdd: measured -9% at d8 to -24% at d16 vs scalar (table
            // spills L1 at high leaf counts). (see devel/agents/perf-notes.md)
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
                // compares; finite `x > upper_` maps to `ood_id`). On `x_v` not
                // `fq_v` so the closed upper endpoint `x == upper_` stays in.
                (~((x_v >= lo_v) & (x_v <= hi_v))).store_aligned(ood_arr.data());
                // Clamp the narrowed index to [0, mask] (vpminud) so the scalar
                // table load stays in bounds; OOD lanes are remapped below.
                xsimd::min(narrow_trunc_to_u32(fq_v), mask_v).store_aligned(q_arr.data());
                poet::static_for<static_cast<std::ptrdiff_t>(lanes)>(
                    [&](auto J) -> void { on_id(i + J, ood_arr[J] ? ood_id : leaf_table_[q_arr[J]]); });
            }
        } else {
            // AVX2/SSE double: per-lane `vcvttsd2si` sweep (no packed
            // double->int64 truncate off AVX-512DQ). `floor` first so the
            // OOD-low sliver classifies identically to the fast path.
            // Non-finite guard mirrors the fast path: see comment above.
            alignas(aligned) std::array<value_type, lanes> q_arr{};
            alignas(aligned) std::array<bool, lanes>       ood_arr{};
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
            const value_type xi       = xp[i];
            const bool       lane_ood = !(xi >= lower_[0] && xi <= upper_[0]);
            const auto       fq       = std::floor((xi - lower_[0]) * inv_span_bins_[0]);
            const auto       qi       = lane_ood ? int_t{0} : static_cast<int_t>(fq);
            on_id(i, resolve(qi, lane_ood));
        }
    }

    /// f32: floor→vcvttps2dq→vpminud→vpgatherdd→OOD-select. Matches `quantize_one`
    /// lane-for-lane; broadcasts hoisted by caller. Only instantiated for `float`.
    [[nodiscard]] TREEWEAVE_ALWAYS_INLINE auto
    gather_leaf_ids(xsimd::batch<value_type> x_v, xsimd::batch<value_type> lo_v, xsimd::batch<value_type> hi_v,
                    xsimd::batch<value_type> inv_v, xsimd::batch<std::uint32_t> mask_v,
                    xsimd::batch<std::uint32_t> ood_v) const -> xsimd::batch<std::uint32_t>
        requires(std::is_same_v<value_type, float> && input_dim == 1)
    {
        const auto fq  = xsimd::floor((x_v - lo_v) * inv_v);
        const auto qi  = xsimd::min(xsimd::batch_cast<std::uint32_t>(xsimd::batch_cast<std::int32_t>(fq)), mask_v);
        const auto ids = xsimd::batch<std::uint32_t>::gather(leaf_table_.data(), xsimd::bitwise_cast<std::int32_t>(qi));
        const auto ood = ~((x_v >= lo_v) & (x_v <= hi_v));
        return xsimd::select(xsimd::batch_bool_cast<std::uint32_t>(ood), ood_v, ids);
    }

    /// Write leaf ids for `n` points into `out`. f32: fully vectorized gather + store
    /// (~4x vs scalar); f64: falls back to the generic callback.
    auto leaf_ids_batch(const value_type *xp, std::uint32_t *out, std::uint32_t ood_id, std::size_t n) const -> void
        requires(input_dim == 1)
    {
        if constexpr (std::is_same_v<value_type, float>) {
            using batch_t                = xsimd::batch<value_type>;
            constexpr std::size_t lanes  = batch_t::size;
            const std::size_t     mask   = (std::size_t{1} << leaf_table_depth_) - 1;
            const auto            lo_v   = batch_t::broadcast(lower_[0]);
            const auto            hi_v   = batch_t::broadcast(upper_[0]);
            const auto            inv_v  = batch_t::broadcast(inv_span_bins_[0]);
            const auto            mask_v = xsimd::batch<std::uint32_t>::broadcast(static_cast<std::uint32_t>(mask));
            const auto            ood_v  = xsimd::batch<std::uint32_t>::broadcast(ood_id);

            const std::size_t n_simd = n & ~(lanes - 1);
            for (std::size_t i = 0; i < n_simd; i += lanes)
                gather_leaf_ids(batch_t::load_unaligned(xp + i), lo_v, hi_v, inv_v, mask_v, ood_v)
                    .store_unaligned(out + i);
            // Tail (n % lanes leftover): for_each_leaf_id_batch with n=0 is a no-op.
            for_each_leaf_id_batch(xp + n_simd, ood_id, n - n_simd,
                                   [&](std::size_t i, std::uint32_t id) -> void { out[n_simd + i] = id; });
            return;
        }
        for_each_leaf_id_batch(xp, ood_id, n, [&](std::size_t i, std::uint32_t id) -> void { out[i] = id; });
    }

    [[nodiscard]] TREEWEAVE_ALWAYS_INLINE auto find_leaf_id_with_ood(const input_type &x, std::uint32_t ood_id) const
        -> std::uint32_t {
        return quantize_one(x, ood_id);
    }

    /// Tree descent: recomputes mid = 0.5*(lo+hi) each level (no per-node center
    /// field; not load-bound). Bit-exactness at boundaries not guaranteed vs fit-time
    /// chained halving — tests assert relative tolerance. (see devel/agents/perf-notes.md)
    [[nodiscard]] auto get_node_index(const input_type &x) const -> std::size_t {
        dim_array_t lo         = lower_;
        dim_array_t hi         = upper_;
        index_t     curr_index = 0;
        while (!nodes_[curr_index].is_leaf()) {
            index_t child_idx = 0;
            poet::static_for<input_dim>([&](auto D) -> void {
                constexpr std::size_t d     = D;
                const value_type      mid_d = (lo[d] + hi[d]) * value_type{0.5};
                const value_type      xd    = [&]() -> value_type {
                    if constexpr (poly_eval::detail::hasTupleSize_v<input_type>)
                        return x[d];
                    else
                        return x;
                }();
                const bool upper = (xd > mid_d);
                child_idx |= (static_cast<index_t>(upper) << d);
                (upper ? lo[d] : hi[d]) = mid_d;
            });
            curr_index = nodes_[curr_index].first_child_idx() + child_idx;
        }
        return curr_index;
    }

    [[nodiscard]] constexpr auto size() const -> std::size_t { return nodes_.size(); }
    [[nodiscard]] constexpr auto max_depth() const -> std::size_t { return max_depth_; }
    [[nodiscard]] auto           lower() const noexcept -> const dim_array_t           &{ return lower_; }
    [[nodiscard]] auto           upper() const noexcept -> const dim_array_t           &{ return upper_; }

    [[nodiscard]] auto memory_usage() const -> std::size_t { return sizeof(*this) + nodes_.size() * sizeof(node_t); }

    auto               get_nodes() -> auto               &{ return nodes_; }
    [[nodiscard]] auto get_nodes() const -> auto & { return nodes_; }

    [[nodiscard]] auto non_converged_panels() const -> const std::vector<NonConvergedPanel> & {
        return non_converged_panels_;
    }

  private:
    std::vector<node_t> nodes_;
    // Subtree bounding box, carried into descent so the Node carries
    // no per-axis `center` field.
    dim_array_t lower_{};
    dim_array_t upper_{};
    std::size_t max_depth_ = 0;
    // Quantize→leaf table for shallow subtrees; empty when not built.
    std::vector<std::uint32_t> leaf_table_;
    std::size_t                leaf_table_depth_ = 0;
    // Precomputed `(1.0 / span) * 2^depth` per axis so the per-point
    // quantize is a multiply (vmulsd, lat 3) instead of a divide
    // (vdivsd, lat 14).
    std::array<value_type, input_dim> inv_span_bins_{};
    std::vector<NonConvergedPanel>    non_converged_panels_;
};

} // namespace treeweave::detail

#endif // TREEWEAVE_DETAIL_POLYTREE_HPP
