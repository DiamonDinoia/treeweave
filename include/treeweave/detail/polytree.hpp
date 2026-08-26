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
#include <treeweave/detail/quantize.hpp>
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

    /// POD view of the leaf table + affine map for the free quantize kernels
    /// (quantize.hpp). Valid while this tree is alive and unmodified.
    [[nodiscard]] auto quantize_view() const noexcept -> QuantizeView<value_type, input_dim> {
        QuantizeView<value_type, input_dim> v{};
        v.table = leaf_table_.data();
        v.bits  = leaf_table_depth_;
        poet::static_for<input_dim>([&](auto D) -> void {
            constexpr std::size_t d = D;
            v.lower[d]              = lower_[d];
            v.upper[d]              = upper_[d];
            v.inv_span_bins[d]      = inv_span_bins_[d];
        });
        return v;
    }

    /// Quantize `x` to its leaf-table index; `ood_id` for out-of-domain/NaN.
    /// Forwards to the free kernel in quantize.hpp.
    [[nodiscard]] TREEWEAVE_ALWAYS_INLINE auto quantize_one(const input_type &x, std::uint32_t ood_id) const noexcept
        -> std::uint32_t {
        if constexpr (!poly_eval::detail::hasTupleSize_v<input_type>) {
            return detail::quantize_one(quantize_view(), &x, ood_id);
        } else {
            std::array<value_type, input_dim> xa{};
            poet::static_for<input_dim>([&](auto D) -> void {
                constexpr std::size_t d = D;
                xa[d]                   = x[d];
            });
            return detail::quantize_one(quantize_view(), xa.data(), ood_id);
        }
    }

    /// SIMD quantize over batch lanes + scalar `on_id` callback per lane.
    /// Forwards to the free kernel in quantize.hpp.
    template <class OnId>
    TREEWEAVE_ALWAYS_INLINE auto for_each_leaf_id_batch(const value_type *xp, std::uint32_t ood_id, std::size_t n,
                                                        OnId on_id) const -> void
        requires(input_dim == 1)
    {
        detail::for_each_leaf_id_batch(quantize_view(), xp, ood_id, n, on_id);
    }
    /// Write leaf ids for `n` points into `out`. Forwards to the free
    /// kernel in quantize.hpp.
    auto leaf_ids_batch(const value_type *xp, std::uint32_t *out, std::uint32_t ood_id, std::size_t n) const -> void
        requires(input_dim == 1)
    {
        detail::leaf_ids_batch(quantize_view(), xp, out, ood_id, n);
    }

    [[nodiscard]] TREEWEAVE_ALWAYS_INLINE auto find_leaf_id_with_ood(const input_type &x, std::uint32_t ood_id) const
        -> std::uint32_t {
        return quantize_one(x, ood_id);
    }

    /// Tree descent: recomputes mid = 0.5*(lo+hi) each level (no per-node center
    /// field; not load-bound). Bit-exactness at boundaries not guaranteed vs fit-time
    /// chained halving — tests assert relative tolerance.
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
