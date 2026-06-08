#ifndef TREEWEAVE_DETAIL_NODE_HPP
#define TREEWEAVE_DETAIL_NODE_HPP

#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include <polyfit/polyfit.hpp>

#include <treeweave/detail/compiler_macros.hpp>
#include <treeweave/detail/eval_policy.hpp>
#include <treeweave/detail/numerics.hpp>
#include <treeweave/detail/tol_kind.hpp>
#include <treeweave/detail/value.hpp>

namespace treeweave::detail {

/// 8-byte tree node: child link + leaf-id slot. The `center` is recomputed
/// during descent from the subtree's `(lo, hi)` bounds, so the node carries
/// no per-axis geometry. `first_child_idx == kLeafSentinel` marks a leaf;
/// `poly_eval_id` indexes the per-Function polyfits table and is meaningful
/// only on leaves. 8 nodes per cache line in every dim — descent loads few
/// cachelines.
template <class Func, std::size_t Degree, EvalPolicy Policy = EvalPolicy::Balanced>
class Node {
  public:
    using input_type     = std::remove_cvref_t<poly_eval::fitInput_t<Func>>;
    using output_type    = poly_eval::fitOutput_t<Func>;
    using value_type     = poly_eval::detail::value_type_or_t<input_type>;
    using poly_eval_type = poly_eval_type_for<Func, Degree, Policy>;

    static constexpr std::size_t input_dim  = value_dim_v<input_type>;
    static constexpr std::size_t output_dim = value_dim_v<output_type>;

    static constexpr std::uint32_t kLeafSentinel = std::numeric_limits<std::uint32_t>::max();

    Node() = default;

    [[nodiscard]] TREEWEAVE_ALWAYS_INLINE auto first_child_idx() const noexcept -> std::uint32_t {
        return first_child_idx_;
    }
    [[nodiscard]] TREEWEAVE_ALWAYS_INLINE auto poly_eval_id() const noexcept -> std::uint32_t { return poly_eval_id_; }
    [[nodiscard]] auto                         is_leaf() const -> bool { return first_child_idx_ == kLeafSentinel; }

    auto set_first_child_idx(std::uint32_t v) noexcept -> void { first_child_idx_ = v; }
    auto set_poly_eval_id(std::uint32_t v) noexcept -> void { poly_eval_id_ = v; }

    /// Fit this node to the requested tolerance. On success, stores the
    /// poly_eval_id into polyfits and returns true. Center/half_length are
    /// passed in (the runtime Node no longer carries `center`).
    auto fit(const TreeInput &input, const Func &func, const Value<value_type, input_dim> &center,
             const Value<value_type, input_dim> &half_length, const std::vector<value_type> &samples,
             std::vector<poly_eval_type> &polyfits) -> bool {
        if (!samples.empty())
            throw std::runtime_error("Treeweave fit error: sample points not yet supported");

        const auto       n_polyfit_before = polyfits.size();
        const input_type lb               = center - half_length;
        const input_type ub               = center + half_length;

        auto rollback_and_fail = [&polyfits, n_polyfit_before]() -> bool {
            while (polyfits.size() != n_polyfit_before)
                polyfits.pop_back();
            return false;
        };

        auto polyfit = polyfits.emplace_back(func, lb, ub);

        // tail_error reads coefficients out of polyfit's 1D `FuncEval` and is
        // not meaningful for the ND / array-output path (which uses
        // `FuncEvalND` with multi-axis coefficient storage). Gate the call so
        // array/ND fits still compile, and fail loudly at runtime if the user
        // asks for a Tail TolKind on an unsupported shape.
        constexpr bool kTailErrorSupported =
            !poly_eval::detail::hasTupleSize_v<input_type> && !poly_eval::detail::hasTupleSize_v<output_type>;
        const bool wants_tail = input.tol_kind == TolKind::RelativeTail || input.tol_kind == TolKind::AbsoluteTail;
        if constexpr (kTailErrorSupported) {
            if (wants_tail) {
                if (tail_error_below_tolerance(input.tol_kind, input.tol, polyfit))
                    return rollback_and_fail();
            } else if (sample_error_below_tolerance(kFitSamplesPerDim, input.tol_kind, input.tol, center, half_length,
                                                    func, polyfit)) {
                return rollback_and_fail();
            }
        } else {
            if (wants_tail)
                throw std::runtime_error("Treeweave fit error: TolKind::RelativeTail / AbsoluteTail "
                                         "is only supported for 1D scalar→scalar fits; use a "
                                         "sample-based TolKind for array-valued or ND fits");
            if (sample_error_below_tolerance(kFitSamplesPerDim, input.tol_kind, input.tol, center, half_length, func,
                                             polyfit))
                return rollback_and_fail();
        }

        poly_eval_id_ = static_cast<std::uint32_t>(n_polyfit_before);
        return true;
    }

    /// Accept the polynomial as a leaf even though the tolerance check
    /// failed. Used at `max_depth` when `allow_max_depth_leaves` is set.
    void force_fit_as_leaf(const Func &func, const Value<value_type, input_dim> &center,
                           const Value<value_type, input_dim> &half_length, std::vector<poly_eval_type> &polyfits) {
        const input_type lb = center - half_length;
        const input_type ub = center + half_length;
        polyfits.emplace_back(func, lb, ub);
        poly_eval_id_ = static_cast<std::uint32_t>(polyfits.size() - 1);
    }

    [[nodiscard]] auto memory_usage() const -> std::size_t { return sizeof(*this); }

  private:
    std::uint32_t first_child_idx_ = kLeafSentinel;
    std::uint32_t poly_eval_id_    = kLeafSentinel;
};

// Lock the 8-B node invariant. If this fires, an extra field was added
// or alignment regressed — descent load-volume depends on 8 nodes per
// cache line.
namespace detail_node_size_check {
struct ScalarFn {
    auto operator()(double) const -> double { return 0.0; }
};
struct Array2Fn {
    auto operator()(std::array<double, 2>) const -> std::array<double, 1> { return {0.0}; }
};
struct Array3Fn {
    auto operator()(std::array<double, 3>) const -> std::array<double, 1> { return {0.0}; }
};
static_assert(sizeof(Node<ScalarFn, 8>) == 8, "slim Node expected to be 8 B (1D)");
static_assert(sizeof(Node<Array2Fn, 8>) == 8, "slim Node expected to be 8 B (2D)");
static_assert(sizeof(Node<Array3Fn, 8>) == 8, "slim Node expected to be 8 B (3D)");
} // namespace detail_node_size_check

} // namespace treeweave::detail

#endif // TREEWEAVE_DETAIL_NODE_HPP
