#ifndef TREEWEAVE_GURU_HPP
#define TREEWEAVE_GURU_HPP

/// \file treeweave/guru.hpp
/// \brief The guru interface: the batch pipeline as public, caller-driven
///        stages — caller-owned scratch, caller-driven fusion, no per-call
///        allocation. Named after FFTW's guru interface, the established
///        precedent for an expert API exposing the planner/executor internals.
///
/// The public `Function` batch evaluators fuse the stages (classify ->
/// histogram -> scan -> scatter -> per-leaf SIMD eval -> gather back) into one
/// call. That composition is right for most uses, but a caller that fits
/// several Functions over sub-ranges of one domain (e.g. a hard function
/// partitioned into regimes — the standard treeweave methodology: split at the
/// singularities/asymptotes, subtract the singular part so each piece fits a
/// polynomial, evaluate the piecewise part per regime, then add the
/// analytically-known part back) re-pays the crossing costs: a range-partition
/// sweep plus per-range scatter, a separate post-processing sweep, fresh
/// allocations every call. The pieces here re-expose the stages so such
/// callers can:
///
///   * classify each point once into one combined key
///     (`key = range_base + f.leaf_id(x)` for the regime's fit — the
///     classification machinery is the public `Function` interface:
///     `leaf_id`/`leaf_ids`, the quantize view via `get_subtrees()`,
///     `has_fast_quantize()`, `num_leaves()`, `out_of_domain_id()`),
///   * run ONE counting sort over those keys, on caller-owned buffers that
///     persist across calls (`exclusive_scan` + `scatter` when the histogram
///     is fused into the classify sweep, otherwise the one-shot
///     `counting_sort`),
///   * evaluate each packed run straight through polyfit's SIMD kernel and
///     fuse their own elementwise post-processing while the run's data is
///     still in registers (`eval_leaf_aos` / `eval_leaf_soa` on the runs of
///     `for_each_run`, `fill_out_of_domain` on the out-of-domain bucket),
///     then `gather` back through `rank`,
///   * do the same run-level fusion on sorted input with zero scratch
///     (`for_each_sorted_run`: sorted input needs no counting sort at all).
///
/// Preconditions mirror the library's: ids and ranks are `std::uint32_t`,
/// so a tile stays below 2^32 points. Classification semantics are identical
/// to the public paths point-for-point. A point is out-of-domain (OOD) when
/// the fit cannot evaluate it — below `lower`, above `upper`, or NaN/±Inf;
/// classification maps it to the sentinel id `f.out_of_domain_id()`
/// (== `num_leaves()`), via the positive-logic gate that keeps the closed
/// upper endpoint in-domain.
///
/// Thread safety: everything here is reentrant and stateless; the buffers
/// are caller arguments. The usual rule applies — disjoint output slices and
/// disjoint scratch per thread.
///
/// Combined-key rules, in one place:
///
///   * Keys are fit-local ids with the fit's OWN sentinel FIRST folded, then
///     offset per regime: `key = (id == f.out_of_domain_id()) ? my_ood_bucket
///     : (range_base + id)`. The fold compares against the fit's own sentinel
///     — never against an offset number. Lay the per-regime spaces
///     back-to-back without holes (hank105's layout; the aggregated OOD
///     bucket comes last).
///   * After the sort, `rank[i]` holds the packed slot of point i — the
///     INVERSE permutation, which the writeback consumes directly:
///     `out[i] = packed_out[rank[i]]` per output component (`gather`),
///     unconditionally — every slot belongs to exactly one bin, so OOD
///     buckets too. (`eval_scatter_sorted` in `<treeweave/eval_scatter.hpp>`
///     records the opposite, FORWARD permutation: its `perm[k]` is the
///     original index of packed slot k.)
///   * Tile at what keeps the per-point scratch (keys 4 B + rank 4 B +
///     packed in and out streams) L2-resident; on a 2 MiB L2 host the
///     valley is 2^14 points for a 4-output fit (sweep
///     {16384, 32768, 65536} when the layout differs).
///   * Contract violations (key >= counts.size() into `scatter`, the OOD
///     sentinel into `eval_leaf_*`) are assert-guarded: they fail cleanly in
///     Debug and undefined in NDEBUG — build Debug/ASan when wiring a new
///     caller.
///   * Classification is SIMD when `f.has_fast_quantize()` (`leaf_id` /
///     `leaf_ids`, and any tables fused from the fit's quantize view) and
///     per-point tree descent otherwise; the ids are identical either way,
///     only the cost moves. ARM/NEON, wasm, and x86 below AVX2 run the
///     same semantics entirely through the public scalar machinery.

#include <algorithm>
#include <array>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>
#include <type_traits>

#include <treeweave/treeweave.hpp>

namespace treeweave::guru {

namespace gdetail {
/// Matching-element contiguous ranges: vector, array, span — anything whose
/// data() is a `T*` for the same T. Pointer callers wrap `std::span{p, n}`.
template <class R, class S>
concept same_element_ranges =
    std::ranges::contiguous_range<R> && std::ranges::contiguous_range<S> &&
    std::same_as<std::ranges::range_value_t<R>, std::ranges::range_value_t<S>>;
} // namespace gdetail

// ---------------------------------------------------------------------------
// Counting sort over caller-supplied u32 keys
// ---------------------------------------------------------------------------

/// Zero `counts`, then count keys: `counts[b]` becomes the number of keys
/// equal to b. First third of the counting sort; a caller whose classify
/// sweep already touches every key fuses the count there instead.
inline auto histogram(std::span<const std::uint32_t> keys, std::span<std::uint32_t> counts) -> void {
    std::ranges::fill(counts, 0u);
    for (const std::uint32_t key : keys) {
        assert(key < counts.size() && "guru::histogram: key out of range");
        ++counts[key];
    }
}

/// In-place exclusive scan (implicit init 0 — the only correct start offset):
/// `counts[b]` becomes the start offset of bin b (Reinecke). After a `scatter`
/// consumes the starts as cursors, `counts[b]` holds the one-past-end of bin b
/// — the array `for_each_run` reads.
inline auto exclusive_scan(std::span<std::uint32_t> counts) -> void {
    // Written out rather than std::exclusive_scan: matches the bin-sort
    // kernel in detail/quantize.hpp, and emits no std-algorithm symbol.
    std::uint32_t acc = 0;
    for (std::uint32_t &c : counts) {
        const std::uint32_t v = c;
        c                     = acc;
        acc += v;
    }
}

/// Place each `in[i]` at `packed[counts[keys[i]]++]` and record the packed
/// slot `rank[i]` (the INVERSE permutation — `gather` consumes it directly).
/// `counts` must hold bin start offsets (output of `exclusive_scan`) and is
/// consumed into ending offsets. Stable within a bin (the sweep walks i in
/// order), so per-bin runs keep input order. The name is the literature's
/// (the counting sort's placement pass); unlike `thrust::scatter`, the
/// destination map is computed from `keys` + `counts`, not caller-supplied.
template <std::ranges::contiguous_range In, std::ranges::contiguous_range Packed>
    requires gdetail::same_element_ranges<In, Packed>
auto scatter(std::span<const std::uint32_t> keys, In &&in, Packed &&packed, std::span<std::uint32_t> counts,
             std::span<std::uint32_t> rank) -> void {
    const std::size_t n = keys.size();
    assert(std::ranges::size(in) >= n && std::ranges::size(packed) >= n && rank.size() >= n);
    const auto *ip = std::ranges::data(in);
    auto       *pp = std::ranges::data(packed);
    for (std::size_t i = 0; i < n; ++i) {
        assert(keys[i] < counts.size() && "guru::scatter: key out of range");
        const std::uint32_t dst = counts[keys[i]]++;
        rank[i]                 = dst;
        pp[dst]                 = ip[i];
    }
}

/// One-shot convenience: `histogram` -> `exclusive_scan` -> `scatter`. On
/// return `counts[b]` is the ending offset of bin b (feed to `for_each_run`),
/// `packed` is the bin-ordered input, and `rank[i]` holds the packed slot of
/// point i.
template <std::ranges::contiguous_range In, std::ranges::contiguous_range Packed>
    requires gdetail::same_element_ranges<In, Packed>
auto counting_sort(std::span<const std::uint32_t> keys, In &&in, Packed &&packed, std::span<std::uint32_t> counts,
                   std::span<std::uint32_t> rank) -> void {
    histogram(keys, counts);
    exclusive_scan(counts);
    scatter(keys, std::forward<In>(in), std::forward<Packed>(packed), counts, rank);
}

/// `out[i] = packed[rank[i]]` — the writeback that restores caller order
/// (`thrust::gather`-exact semantics: `rank` is the map into `packed`). Owns
/// the software prefetch of `packed[rank[i + 32]]` (the batch pipeline's
/// `Function::kLookahead` distance), worthwhile on gathered reads. Call once
/// per output component on SoA layouts.
template <std::ranges::contiguous_range Packed, std::ranges::contiguous_range Out>
    requires gdetail::same_element_ranges<Packed, Out>
auto gather(std::span<const std::uint32_t> rank, Packed &&packed, Out &&out) -> void {
    const std::size_t n = rank.size();
    assert(std::ranges::size(out) >= n);
    constexpr std::size_t lookahead = detail::kLookahead;
    const auto *src = std::ranges::data(packed);
    auto       *dst = std::ranges::data(out);
    for (std::size_t i = 0; i < n; ++i) {
        assert(rank[i] < std::ranges::size(packed) && "guru::gather: rank value out of range");
        if (i + lookahead < n)
            detail::prefetch<0>(src + rank[i + lookahead]);
        dst[i] = src[rank[i]];
    }
}

/// `fn(id, begin, count)` once per non-empty bin, ascending id order —
/// the same callback shape as `for_each_sorted_run`. `ends[b]` is bin b's
/// one-past-end, i.e. a post-`scatter` `counts` array; `ends` must be
/// non-decreasing (a raw histogram that skipped `exclusive_scan` + `scatter`
/// is not a valid input).
template <class Fn>
auto for_each_run(std::span<const std::uint32_t> ends, Fn &&fn) -> void {
    std::uint32_t beg = 0;
    for (std::uint32_t b = 0; b < ends.size(); ++b) {
        const std::uint32_t end = ends[b];
        assert(end >= beg && "guru::for_each_run: ends must be non-decreasing (post-scatter counts)");
        if (end > beg)
            fn(b, static_cast<std::size_t>(beg), static_cast<std::size_t>(end - beg));
        beg = std::max(beg, end);
    }
}

// ---------------------------------------------------------------------------
// Packed-run leaf evaluation
// ---------------------------------------------------------------------------

/// Evaluate leaf `id` of `f` at `n` points into interleaved AoS output
/// (`out[q * output_dim + d]`). Same canonicalisation as the public tile
/// pipeline: tuple-spelled inputs reinterpret the scalar buffer as
/// `array<T,1>` (layout-compatible); scalar fits call the `Leaf1D` run.
/// `id` must be a real leaf — the `out_of_domain_id()` sentinel has no
/// coefficients (`fill_out_of_domain` owns that bucket).
template <class F, class K = default_kernel_policy>
auto eval_leaf_aos(const F &f, std::uint32_t id, const typename F::value_type *x, typename F::value_type *out,
                   std::size_t n, const K &k = {}) -> void {
    assert(id < f.num_leaves() && "guru::eval_leaf_aos: id must be a leaf, not the out-of-domain sentinel");
    if constexpr (poly_eval::detail::hasTupleSize_v<typename F::input_type>) {
        using CI = typename F::poly_eval_type::CanonicalInput;
        using CO = typename F::poly_eval_type::CanonicalOutput;
        k.run_aos(f.leaf_view_of(id), reinterpret_cast<const CI *>(x), reinterpret_cast<CO *>(out), n);
    } else {
        static_assert(F::output_dim == 1, "scalar-input fits have one output");
        k.run(f.leaf_view_of(id), x, out, n);
    }
}

/// SoA twin: component d of point q lands in `out[d][q]`.
template <class F, class K = default_kernel_policy>
auto eval_leaf_soa(const F &f, std::uint32_t id, const typename F::value_type *x,
                   std::array<typename F::value_type *, F::output_dim> out, std::size_t n, const K &k = {}) -> void
    requires(F::output_dim > 1)
{
    assert(id < f.num_leaves() && "guru::eval_leaf_soa: id must be a leaf, not the out-of-domain sentinel");
    static_assert(poly_eval::detail::hasTupleSize_v<typename F::input_type>,
                  "output_dim > 1 implies a tuple-spelled input");
    using CI = typename F::poly_eval_type::CanonicalInput;
    k.run_soa(f.leaf_view_of(id), reinterpret_cast<const CI *>(x), out, n);
}

/// The out-of-domain bucket's writeback: `n * output_dim` quiet NaNs into
/// interleaved AoS `out` — exactly what the public batch paths write for
/// points classified to `f.out_of_domain_id()`. `Function::sorted` runs on
/// this same definition.
template <class F>
auto fill_out_of_domain(const F &f, typename F::value_type *out, std::size_t n) -> void {
    detail::fill_out_of_domain(f, out, n);
}

/// SoA twin: `n` quiet NaNs into each of the `output_dim` component buffers.
template <class F>
auto fill_out_of_domain(const F &f, std::array<typename F::value_type *, F::output_dim> out, std::size_t n) -> void
    requires(F::output_dim > 1)
{
    detail::fill_out_of_domain(f, out, n);
}

// ---------------------------------------------------------------------------
// Sorted-input run dispatch (1D, zero scratch)
// ---------------------------------------------------------------------------

/// Walk ascending `xs` and call `fn(id, begin, count)` per maximal run of
/// points sharing a leaf id — the same callback shape as `for_each_run`.
/// This is the canonical walk: `Function::sorted` dispatches its kernels
/// through the same implementation, so library and guru callers share one
/// code path. The caller fuses its own per-run post-processing onto the
/// evaluated span while it is still hot:
///
///   for_each_sorted_run(f, xs, n, [&](u32 id, size_t begin, size_t count) {
///       if (id == f.out_of_domain_id()) {
///           fill_out_of_domain(f, out + OUT_DIM * begin, count);
///           return;
///       }
///       eval_leaf_aos(f, id, xs + begin, out + OUT_DIM * begin, count);
///       my_fixup(xs + begin, ..., count);            // fused: no extra sweep
///   });
///
/// Out-of-domain points arrive as `id == f.out_of_domain_id()`: prefix and
/// suffix come as single aggregated calls; an interior NaN comes one point
/// per call. Nothing is written for sentinel ids — the callback owns the
/// fill (`fill_out_of_domain`).
template <class F, class Fn>
auto for_each_sorted_run(const F &f, const typename F::value_type *xs, std::size_t n, Fn &&fn) -> void
    requires(F::input_dim == 1)
{
    static_assert(std::is_arithmetic_v<typename F::value_type>, "guru::for_each_sorted_run: real value types only");
    for_each_sorted_run_1d(f, xs, n, fn);
}

} // namespace treeweave::guru

#endif // TREEWEAVE_GURU_HPP
