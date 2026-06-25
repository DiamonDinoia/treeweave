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
                bool  successful_fit = node.fit(input, func, current_box.center, current_box.half_length, {}, polyfits);

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

        // For shallow subtrees, build a quantize-to-leaf table so
        // eval-time descent collapses to a single load. Table is
        // uint32, 4 B per entry, size 1 << (input_dim * max_depth_).
        // Capped at 64 K entries (256 KiB) per subtree — L2-resident
        // on modern x86 cores (SPR has 2 MiB L2/core, Zen4 1 MiB) and
        // worth the L1d eviction tradeoff because the descent path
        // it replaces costs one `vucomisd + ja` per level (IPC ~1.9,
        // branch-miss 6-10%) vs one `vcvttsd2usi + load` for the
        // table (measured ~3x worse at depth 15-16 on bench_pack_scatter).
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

    /// Combined leaf-id lookup: table if available, else descent.
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

    /// Compute the leaf id for a single point via the leaf-table fast
    /// path. 1D uses one scalar quantize + unsigned wrap OOD; ND iterates
    /// per axis and bails on the first out-of-range axis. Caller must
    /// have verified `has_leaf_table()` — this is the shared kernel
    /// used by `find_leaf_id_with_ood` (scalar API) and the per-lane
    /// body of `find_leaf_ids_batch` (1D SIMD batch path), as well as
    /// the scatter recompute in `Function::eval_batch_tile`.
    ///
    /// The signed `vcvttsd2si` + unsigned compare folds the OOD test
    /// into the table-index quantize: an out-of-range double yields
    /// INT64_MIN (x86-64 indefinite-integer), which compares above
    /// `mask` as uint64. Saves one cmp per axis vs. an explicit
    /// in-domain pre-check. The early `return ood_id;` inside the
    /// per-axis loop keeps the OOD path off the hot fall-through —
    /// flag-and-post-check formulations regressed 1D batch by 4–7 %
    /// (paired-median, n=24).
    ///
    /// Out-of-domain gate: the positive-logic test `!(x >= lower_ && x <=
    /// upper_)` returns `ood_id` for everything outside `[lower_, upper_]`
    /// in a single per-axis check. It matches `Function::operator()` exactly,
    /// so the batch and scalar APIs agree point-for-point: OOD-low, finite
    /// `x > upper_`, NaN and ±Inf all map to `ood_id` (NaN/±Inf fail both
    /// comparisons — every NaN compare is false). This replaces the old
    /// `isfinite` + unsigned-wrap pair, and because the gate proves `x`
    /// finite and in range before the cast, `static_cast<int64_t>` is never
    /// applied to a non-finite value (no UB, no aarch64 `fcvtzs(NaN) = 0`).
    ///
    /// Closed upper endpoint: `x == upper_` passes the gate and quantizes to
    /// `mask + 1`; the `min(q, mask)` clamp routes it to the last leaf (the
    /// boundary value, finite). Finite `x > upper_` is rejected by the gate
    /// *before* the clamp, so it returns `ood_id` rather than extrapolating
    /// — the one case the batch path used to disagree with the scalar API on.
    /// `std::floor` matches the packed/sweep/tail truncation in
    /// `for_each_leaf_id_batch` so a point classifies the same whichever runs.
    [[nodiscard]] TREEWEAVE_ALWAYS_INLINE auto quantize_one(const input_type &x, std::uint32_t ood_id) const noexcept
        -> std::uint32_t {
        const std::size_t bits   = leaf_table_depth_;
        const auto        mask_i = static_cast<std::int64_t>((std::size_t{1} << bits) - 1);
        if constexpr (!poly_eval::detail::hasTupleSize_v<input_type>) {
            // Single positive-logic domain gate (see docstring): rejects
            // OOD-low, finite OOD-high, NaN and ±Inf in one test, and proves
            // `x` finite and in range so the cast below is UB-free.
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

    /// 1D batch leaf-id stream — invokes `on_id(i, id)` for every point in
    /// `[xp, xp+n)`, amortising the quantize across
    /// `xsimd::batch<value_type>::size` lanes per iteration. Caller picks
    /// the per-point side effect: `Function::eval_batch_tile` uses this
    /// twice, first to bump `counts[id]` (the histogram), then to scatter
    /// `xp_packed[counts[id]++] = xp[i]` (no `leaf_ids[]` materialisation
    /// in between — see FINUFFT bin-sort recon, spread.hpp:421).
    ///
    /// Pipeline per SIMD chunk:
    ///   1. SIMD compute: `q = (x - lo) * inv_span_bins` for `simd_size`
    ///      lanes (one `load_unaligned`, one sub, one mul). Truncating
    ///      conversion to int produces INT64_MIN on x86 for OOD doubles,
    ///      which compares above `mask` as uint64 — same single-cmp
    ///      OOD trick used by the scalar variant.
    ///   2. Scalar lane sweep: per-lane table lookup `leaf_table_[q]` (or
    ///      `ood_id` on wrap) and the user-supplied `on_id(i+j, id)`
    ///      callback. Vectorised gather/scatter to the histogram would
    ///      lose to bank-conflict serialization on shared counters
    ///      (FINUFFT spread.hpp:454-457 documents the same finding).
    ///
    /// The trailing `n % simd_size` points are dispatched through the
    /// scalar quantize for clarity; the loop is short enough that the
    /// branch-predictor handles it without measurable cost.
    template <class OnId>
    TREEWEAVE_ALWAYS_INLINE auto for_each_leaf_id_batch(const value_type *xp, std::uint32_t ood_id, std::size_t n,
                                                        OnId on_id) const -> void
        requires(input_dim == 1)
    {
        using batch_t                 = xsimd::batch<value_type>;
        constexpr std::size_t lanes   = batch_t::size;
        constexpr std::size_t aligned = batch_t::arch_type::alignment();

        // Truncating-conversion strategy, by value type (leaf ids are <= 2^16,
        // so 32 bits is always ample for the index):
        //
        //   * float  -> int32 via `vcvttps2dq` (xsimd `fast_cast(float,int32)`,
        //     lane-matched on SSE2/AVX/AVX-512 — xsimd_sse2.hpp:739 etc.). This
        //     is the *only* lane-matched packed float->int truncate, and it
        //     replaces the per-lane `vcvttss2si` sweep that made the 16-wide
        //     AVX-512 float batch the costliest quantize cell measured
        //     (bench/binsort_phase0.md). OOD lanes are decided by the float
        //     domain mask below, so the packed cast only has to land in-range
        //     for the *kept* lanes — the clamp in `resolve` guarantees that.
        //
        //   * double -> int64 via `vcvttpd2qq`, but ONLY on AVX-512DQ (the one
        //     x86 packed double->int64 truncate, xsimd_avx512dq.hpp:259).
        //     Off that ISA `xsimd::batch_cast<int64_t>` emulates per-lane
        //     (slower than the per-lane sweep below), so AVX2/SSE double stays
        //     on the per-lane `vcvttsd2si` sweep. Phase 0 measured that sweep at
        //     ~1.5 cyc/pt, on par with the AVX-512DQ fast path, so narrowing
        //     double to int32 buys nothing — and double->int32 is not
        //     lane-matched in xsimd, so it would need a per-arch intrinsic for
        //     no gain. Left as-is deliberately.
#ifdef __AVX512DQ__
        constexpr bool kFastInt64 = std::is_same_v<value_type, double>;
#else
        constexpr bool kFastInt64 = false;
#endif
        constexpr bool kFastInt32 = std::is_same_v<value_type, float>;

        const std::size_t mask  = (std::size_t{1} << leaf_table_depth_) - 1;
        const auto        lo_v  = batch_t::broadcast(lower_[0]);
        const auto        hi_v  = batch_t::broadcast(upper_[0]);
        const auto        inv_v = batch_t::broadcast(inv_span_bins_[0]);

        // Per-lane resolve: the caller passes the truncated quantize `qi` and
        // the per-lane out-of-domain bit `lane_ood`, computed in SIMD from the
        // same positive-logic domain test `quantize_one` uses. `lane_ood` alone
        // decides OOD — it already covers OOD-low, finite OOD-high, NaN and
        // ±Inf — so this is a branchless select that matches the scalar API
        // point-for-point. `qi` is clamped to `[0, mask]` only to keep the
        // discarded lanes' table read in bounds (an OOD-low `qi < 0`, or aarch64
        // `fcvtzs(NaN) = 0` / `fcvtzs(+Inf) = INT_MAX`); the same clamp routes
        // the closed upper endpoint `x == upper_` (quantizing to `mask + 1`) to
        // the last leaf.
        auto resolve = [&](auto qi, bool lane_ood) -> std::uint32_t {
            using U = std::make_unsigned_t<decltype(qi)>;
            // One unsigned min keeps the index in [0, mask] for every lane: a
            // negative `qi` (OOD-low, or the x86 indefinite INT_MIN) wraps to a
            // huge unsigned and clamps to `mask`. OOD lanes are discarded by the
            // `lane_ood` select, so this index only has to stay in bounds, not
            // be meaningful — hence a bare clamp with no sign test suffices.
            const auto q = std::min(static_cast<U>(qi), static_cast<U>(mask));
            return lane_ood ? ood_id : leaf_table_[static_cast<std::size_t>(q)];
        };

        // Round n down to a whole number of lanes. `lanes` is a power of two
        // (xsimd batch size), so this is a single mask — same codegen as the
        // div+mul, just explicit about the assumption.
        static_assert((lanes & (lanes - 1)) == 0, "lanes must be a power of two");
        const std::size_t n_simd = n & -lanes;

        // Lane-matched signed-integer lane type for `value_type`:
        // `as_integer_t<float> == int32_t` (-> vcvttps2dq),
        // `as_integer_t<double> == int64_t` (-> vcvttpd2qq). Same width used by
        // the packed fast path, the per-lane sweep, and the scalar tail so a
        // point classifies identically whichever code path handles it.
        using int_t = xsimd::as_integer_t<value_type>;

        if constexpr (kFastInt32) {
            // f32: gather the leaf table for W lanes in one shot, replacing the W
            // dependent scalar `leaf_table_[q]` loads the audit measured as the
            // bottleneck of the scalar resolve sweep. Lowers to `vpgatherdd` on
            // AVX2+; xsimd emulates with a scalar load loop below that, which
            // still beats the old sweep (measured ~1.7x at SSE2, ~2-4x with a
            // hardware gather) — so no ISA gate, let xsimd pick. Only the consumer
            // stays scalar: `on_id` may carry a cross-lane RMW (e.g. the histogram
            // `++counts[id]`) a vector scatter can't serialize. `gather_leaf_ids`
            // does the floor/clamp/OOD-select in SIMD, matching `quantize_one`.
            const auto mask_v = xsimd::batch<std::uint32_t>::broadcast(static_cast<std::uint32_t>(mask));
            const auto ood_v  = xsimd::batch<std::uint32_t>::broadcast(ood_id);
            alignas(aligned) std::array<std::uint32_t, lanes> id_arr{};
            for (std::size_t i = 0; i < n_simd; i += lanes) {
                gather_leaf_ids(batch_t::load_unaligned(xp + i), lo_v, hi_v, inv_v, mask_v, ood_v)
                    .store_aligned(id_arr.data());
                for (std::size_t j = 0; j < lanes; ++j) on_id(i + j, id_arr[j]);
            }
        } else if constexpr (kFastInt64) {
            // double on AVX-512DQ: one packed `vcvttpd2qq` per W lanes in place of
            // W scalar conversions (xsimd::batch_cast selects it via `fast_cast`
            // ADL). f32 goes through the gather path above; this is double-only.
            using ibatch_t = xsimd::batch<int_t, typename batch_t::arch_type>;
            static_assert(ibatch_t::size == lanes, "integer batch must be lane-matched to the value batch");
            alignas(aligned) std::array<int_t, lanes> q_arr{};
            alignas(aligned) std::array<bool, lanes>  ood_arr{};
            for (std::size_t i = 0; i < n_simd; i += lanes) {
                const auto x_v  = batch_t::load_unaligned(xp + i);
                const auto fq_v = xsimd::floor((x_v - lo_v) * inv_v);
                // Positive-logic domain mask, identical to `quantize_one`'s gate:
                // a lane is OOD unless `lo <= x <= hi`. This subsumes the old
                // non-finite guard (NaN and ±Inf fail both compares) and adds the
                // finite OOD-high case, so finite `x > upper_` now maps to `ood_id`
                // instead of extrapolating from the last cell. Computed on `x_v`
                // (not `fq_v`) so the closed upper endpoint `x == upper_` stays
                // in-domain. The packed cast runs on every lane (well-defined on
                // SIMD, no per-lane UB); `resolve`'s clamp keeps the discarded
                // OOD lanes' table index in bounds.
                (~((x_v >= lo_v) & (x_v <= hi_v))).store_aligned(ood_arr.data());
                xsimd::batch_cast<int_t>(fq_v).store_aligned(q_arr.data());
                for (std::size_t j = 0; j < lanes; ++j)
                    on_id(i + j, resolve(q_arr[j], ood_arr[j]));
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
                for (std::size_t j = 0; j < lanes; ++j) {
                    const auto qd = ood_arr[j] ? int_t{0} : static_cast<int_t>(q_arr[j]);
                    on_id(i + j, resolve(qd, ood_arr[j]));
                }
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

    /// f32 gather kernel shared by `for_each_leaf_id_batch` and `leaf_ids_batch`:
    /// floor -> truncate -> clamp (`vpminud`) -> gather (`vpgatherdd` on AVX2+,
    /// xsimd scalar-loop emulation below that) -> OOD-select, matching
    /// `quantize_one` lane-for-lane. Index and value are both 32-bit so the
    /// gather lanes match. `*_v` are loop-invariant broadcasts hoisted by the
    /// caller. Only instantiated for `float`.
    TREEWEAVE_ALWAYS_INLINE auto gather_leaf_ids(xsimd::batch<value_type> x_v, xsimd::batch<value_type> lo_v,
                                                 xsimd::batch<value_type> hi_v, xsimd::batch<value_type> inv_v,
                                                 xsimd::batch<std::uint32_t> mask_v,
                                                 xsimd::batch<std::uint32_t> ood_v) const
        -> xsimd::batch<std::uint32_t>
        requires(std::is_same_v<value_type, float> && input_dim == 1)
    {
        const auto fq  = xsimd::floor((x_v - lo_v) * inv_v);
        const auto qi  = xsimd::min(xsimd::batch_cast<std::uint32_t>(xsimd::batch_cast<std::int32_t>(fq)), mask_v);
        const auto ids = xsimd::batch<std::uint32_t>::gather(leaf_table_.data(), xsimd::bitwise_cast<std::int32_t>(qi));
        const auto ood = ~((x_v >= lo_v) & (x_v <= hi_v));
        return xsimd::select(xsimd::batch_bool_cast<std::uint32_t>(ood), ood_v, ids);
    }

    /// Vectorized leaf-id stream into `out[i]` (dependency-free consumer, unlike
    /// the histogram/scatter callbacks). On the f32 path this is the fully packed
    /// gather pipeline with a vector store — no per-lane callback, the audit's
    /// ~4x cell. The double path falls back to the generic callback.
    auto leaf_ids_batch(const value_type *xp, std::uint32_t *out, std::uint32_t ood_id, std::size_t n) const -> void
        requires(input_dim == 1)
    {
        if constexpr (std::is_same_v<value_type, float>) {
            using batch_t               = xsimd::batch<value_type>;
            constexpr std::size_t lanes = batch_t::size;
            const std::size_t     mask  = (std::size_t{1} << leaf_table_depth_) - 1;
            const auto            lo_v  = batch_t::broadcast(lower_[0]);
            const auto            hi_v  = batch_t::broadcast(upper_[0]);
            const auto            inv_v = batch_t::broadcast(inv_span_bins_[0]);
            const auto mask_v = xsimd::batch<std::uint32_t>::broadcast(static_cast<std::uint32_t>(mask));
            const auto ood_v  = xsimd::batch<std::uint32_t>::broadcast(ood_id);

            const std::size_t n_simd = n & -lanes;
            for (std::size_t i = 0; i < n_simd; i += lanes)
                gather_leaf_ids(batch_t::load_unaligned(xp + i), lo_v, hi_v, inv_v, mask_v, ood_v)
                    .store_unaligned(out + i);
            // Tail (< lanes): reuse the generic path's scalar quantize.
            if (n_simd < n)
                for_each_leaf_id_batch(xp + n_simd, ood_id, n - n_simd,
                                       [&](std::size_t i, std::uint32_t id) { out[n_simd + i] = id; });
            return;
        }
        for_each_leaf_id_batch(xp, ood_id, n, [&](std::size_t i, std::uint32_t id) { out[i] = id; });
    }

    [[nodiscard]] TREEWEAVE_ALWAYS_INLINE auto find_leaf_id_with_ood(const input_type &x, std::uint32_t ood_id) const
        -> std::uint32_t {
        return quantize_one(x, ood_id);
    }

    /// Descent hot loop. The per-subtree (lo, hi) bounds live in
    /// registers and `mid = 0.5 * (lo + hi)` is recomputed each level
    /// — the node carries no `center`, so descent is not load-bound.
    /// `input_dim` is constexpr so the ND per-axis compare unrolls.
    ///
    /// `mid` is computed differently from the fit-time
    /// `box.center` (chained halving), so bit-exactness at boundary
    /// points is not guaranteed; tests assert relative tolerance.
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

    [[nodiscard]] auto memory_usage() const -> std::size_t {
        std::size_t total = sizeof(*this);
        for (const auto &node : nodes_)
            total += node.memory_usage();
        return total;
    }

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
