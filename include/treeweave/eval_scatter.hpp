#ifndef TREEWEAVE_EVAL_SCATTER_HPP
#define TREEWEAVE_EVAL_SCATTER_HPP

/// \file treeweave/eval_scatter.hpp
/// \brief Cross-Function batched scalar evaluation.
///
/// `eval_scatter_sorted` takes parallel arrays of (fit-index, point)
/// pairs and writes per-pair outputs. Pairs are grouped by fit-id so
/// each Function sees a contiguous run, then dispatched via scalar
/// `operator()` (TREEWEAVE_ALWAYS_INLINE — no per-pair callq).
///
/// Two overloads, both counting-sort:
///   * scratch-span: caller supplies pre-allocated `counts` (zero-init)
///     and `perm`. No allocation inside.
///   * convenience: same, but allocates `counts`/`perm` per call.
///
/// Numbers: see `bench/baseline_pack_scatter_nb.txt`. The hand-rolled
/// `for (i) ys[i] = (*fits[fit_ids[i]])(xs[i])` (scatter_naive) still
/// wins on raw throughput at small `n / n_fits` ratios; counting-sort
/// pays off when grouping unlocks the per-fit SIMD batch path (~1024
/// evals per fit) or when fits spill L2 and per-fit locality matters.

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <span>
#include <vector>

#include <treeweave/treeweave.hpp>

namespace treeweave {

/// Caller-scratch counting-sort overload. Caller provides `counts`
/// (size `n_fits`, **zero-initialised on entry**) and `perm`
/// (size `n`, contents ignored). No allocation here.
///
/// The pre-zeroed-counts contract avoids a redundant clear pass when
/// the caller knows the buffer is fresh. Re-using a dirty buffer? Run
/// `std::ranges::fill(counts.first(n_fits), 0u)` first.
///
/// Algorithm: FINUFFT `bin_sort_singlethread_impl` pattern (finufft
/// spread.hpp:421) — `uint32_t` counts halves cache footprint vs
/// `BIGINT`, `std::exclusive_scan` for offsets in place (Reinecke's
/// trick), scalar histogram/placement (SIMD scatter to `counts` loses
/// to duplicate-bin serialisation; clang dead-store-eliminates any
/// xsimd round-trip wrapper anyway).
template <std::size_t Degree, class Func>
auto eval_scatter_sorted(std::span<const Function<Degree, Func> *const> fits, std::span<const std::uint32_t> fit_ids,
                         std::span<const double> xs, std::span<double> ys, std::uint32_t n_fits,
                         std::span<std::uint32_t> counts, std::span<std::uint32_t> perm) -> void {
    using fn_t = Function<Degree, Func>;
    static_assert(fn_t::input_dim == 1 && fn_t::output_dim == 1, "eval_scatter_sorted: 1D scalar fits only");

    const std::size_t n = fit_ids.size();
    assert(xs.size() == n && ys.size() == n);
    assert(counts.size() >= n_fits && "counts span too small");
    assert(perm.size() >= n && "perm span too small");
    if (n == 0)
        return;

    for (std::size_t i = 0; i < n; ++i) {
        assert(fit_ids[i] < n_fits && "fit_ids[i] >= n_fits");
        ++counts[fit_ids[i]];
    }

    // Reinecke's trick: in-place exclusive scan turns counts into start
    // offsets. The placement loop below bumps each entry, so after
    // placement counts[k] holds the END of bin k.
    std::exclusive_scan(counts.begin(), counts.begin() + n_fits, counts.begin(), std::uint32_t{0});

    // Placement: write only the original-order index at the bin cursor.
    // The dispatch loop reads xs[perm[k]] directly, so no fit-grouped
    // `xs_sorted` scratch is needed — trades sequential xs_sorted reads
    // for scattered reads of xs, but dispatch already writes ys[perm[k]]
    // scattered, so the cache pattern is the same gather/scatter pair.
    for (std::size_t i = 0; i < n; ++i) {
        const std::uint32_t fid = fit_ids[i];
        perm[counts[fid]++]     = static_cast<std::uint32_t>(i);
    }

    // Dispatch each non-empty fit-id run. fn(x) is TREEWEAVE_ALWAYS_INLINE,
    // so the per-pair body is an inlined Hybrid FMA chain with no callq.
    std::uint32_t run_start = 0;
    for (std::uint32_t fid = 0; fid < n_fits; ++fid) {
        const std::uint32_t run_end = counts[fid];
        const auto         &fn      = *fits[fid];
        for (std::uint32_t k = run_start; k < run_end; ++k) {
            const std::uint32_t idx = perm[k];
            ys[idx]                 = fn(xs[idx]);
        }
        run_start = run_end;
    }
}

/// Convenience overload — allocates `counts` and `perm` per call. Use
/// the scratch-span overload above when calling repeatedly to amortise
/// the two allocations (~50–100 ns flat on glibc).
template <std::size_t Degree, class Func>
auto eval_scatter_sorted(std::span<const Function<Degree, Func> *const> fits, std::span<const std::uint32_t> fit_ids,
                         std::span<const double> xs, std::span<double> ys, std::uint32_t n_fits) -> void {
    std::vector<std::uint32_t> counts(n_fits);
    std::vector<std::uint32_t> perm(fit_ids.size());
    eval_scatter_sorted<Degree, Func>(fits, fit_ids, xs, ys, n_fits, std::span<std::uint32_t>(counts),
                                      std::span<std::uint32_t>(perm));
}

} // namespace treeweave

#endif // TREEWEAVE_EVAL_SCATTER_HPP
