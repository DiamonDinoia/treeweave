#ifndef TREEWEAVE_TREEWEAVE_HPP
#define TREEWEAVE_TREEWEAVE_HPP

/// \file treeweave/treeweave.hpp
/// \brief Public C++ API for the treeweave piecewise-polynomial function
///        approximator. Built on polyfit's leaf evaluators; treeweave adds the
///        adaptive tree (paneling) layer on top.
///
/// Thread safety. Once `treeweave::fit(...)` returns, the resulting Function
/// is immutable through its `operator()` overloads, which are safe to call
/// concurrently from multiple threads provided each call writes to a
/// disjoint output slice. The batch path allocates and frees its scratch
/// on each call via the caller-supplied allocator (default
/// `std::allocator<value_type>`); no state is carried between calls.
/// Callers that want pooled reuse should pass a stateful allocator —
/// `std::pmr::polymorphic_allocator` over a `monotonic_buffer_resource`
/// is the idiomatic choice. See `tests/test_threadsafe.cpp`.
///
/// Memory cost. Tightening `tol`, raising `max_depth`, raising
/// `max_memory_mib`, or enabling `allow_max_depth_leaves` all increase the
/// leaf count `L`. The Function holds one persistent allocation: the tree
/// itself, `O(L)`, shared by all threads and reported by
/// `Function::print_stats()` / `memory_usage()`. Each batch call
/// allocates a stack-local scratch of roughly
/// `tile_K * (4 + (input_dim + output_dim) * sizeof(value_type))` plus
/// `4 * (L + 1)` bytes (with `tile_K = max(65536, 32 * L)`) and frees it
/// on return.

#include <array>
#include <concepts>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include <polyfit/polyfit.hpp>

#include <treeweave/detail/eval_policy.hpp>
#include <treeweave/detail/tol_kind.hpp>

#include <treeweave/detail/function_impl.hpp>

namespace treeweave {

/// Library version. Mirrors the CMake `project(VERSION)` and the C-ABI
/// `TREEWEAVE_VERSION_*` macros in <treeweave.h>.
inline constexpr int version_major = 0;
inline constexpr int version_minor = 0;
inline constexpr int version_patch = 0;

/// Runtime fit knobs. Every field here only affects fit-time
/// construction; there is no eval-time benefit to promoting any of them
/// to a template parameter, so they stay plain data.
struct options {
    /// How `tol` is interpreted by the convergence check. The default
    /// (`RelativeMax`) compares max-abs error on a sample grid against
    /// `tol * max(|f|)`; switch to `Absolute*` when `f` can be zero or
    /// when relative accuracy isn't meaningful.
    TolKind tol_kind = TolKind::RelativeMax;
    /// Tree-depth ceiling for the adaptive paneler. Hitting it without
    /// converging throws `MaxDepthExceeded` (or, with
    /// `allow_max_depth_leaves`, accepts the panel as best-effort).
    /// 50 is far above what any non-singular function needs; lower it
    /// to fail fast when a near-singularity is suspected.
    int max_depth = 50;
    /// Hard cap on accumulated leaf storage during the fit, in MiB.
    /// Tri-state:
    ///   * `< 0` (default) — auto: a small, dimension-scaled budget,
    ///     `4 << (input_dim-1)` MiB (4 / 8 / 16 for 1D / 2D / 3D). Leaf
    ///     storage grows ~geometrically with `input_dim`, so a single flat
    ///     cap is either too loose in 1D or too tight in 3D; scaling keeps
    ///     the default a genuine guardrail in every dimension.
    ///   * `0` — disabled (no cap).
    ///   * `> 0` — explicit cap in MiB.
    /// The default is deliberately small so an adaptive paneler can't
    /// quietly grow into hundreds of MiB; ambitious 3D+ or oscillatory
    /// fits opt in explicitly with a larger value (or `0`). Crossing the
    /// cap throws `MemoryBudgetExceeded`, which carries the used/budget
    /// bytes and the offending panel so the caller can raise the budget or
    /// excise the singular region.
    int max_memory_mib = -1;
    /// When true, panels that fail tolerance at `max_depth` are kept
    /// as best-effort leaves. Inspect them via
    /// `Function::non_converged_panels()`. Default false (throw, with
    /// the panel list attached on the exception).
    bool allow_max_depth_leaves = false;
    /// Force BFS to refine every panel to at least this depth before
    /// the per-panel tolerance test exits. Useful for driving the
    /// leaf-table fast path (`PolyTree::quantize_one`): a uniformly-
    /// refined tree of depth D in input_dim K builds a 2^(K*D)-entry
    /// quantize table the eval-time lookup collapses to one SIMD
    /// quantize + one u32 load. Table size grows as
    /// 2^(K*D) * 4 B and is capped at 64 K entries (256 KiB) per
    /// subtree, so values that push past `K*D > 16` will still build
    /// a uniform tree but not the table. Default 0 (no forcing —
    /// tol-based refinement only).
    int min_uniform_depth = 0;
};

/// Any callable that accepts `Domain` and returns a value.
template <class F, class Domain>
concept Fittable = requires(F f, Domain x) { f(x); };

namespace detail {

// Auto memory budget (MiB) used when `options.max_memory_mib` is left
// negative. Leaf storage grows ~geometrically with input_dim, so a flat cap
// reasonable in 1D is far too tight in 3D; doubling per dimension keeps the
// default a small guardrail (4 / 8 / 16 MiB for 1D / 2D / 3D) while still
// forcing an explicit opt-in for genuinely large tables.
constexpr auto auto_memory_budget_mib(int input_dim) -> int {
    const unsigned d = input_dim < 1 ? 1U : static_cast<unsigned>(input_dim);
    return static_cast<int>(4U << (d - 1U));
}

// Default leaf degree. The C-ABI tuning campaign (see arch_degree_table.hpp)
// found degree 7 wins or ties in every (arch, dtype, input_dim) cell — within
// ~1% in 1D and 2-10x in 2D/3D, and the only spill-free degree in the
// register-pressured wide cells — so the C++ template default matches the
// baked C-ABI value. Override per call via the `Degree` template parameter.
inline constexpr std::size_t kDefaultDegree = 7;

inline auto make_input(int input_dim, int output_dim, int degree, double tol, const options &opts) -> TreeInput {
    TreeInput in{};
    in.input_dim              = input_dim;
    in.output_dim             = output_dim;
    in.degree                 = degree;
    in.tol                    = tol;
    in.tol_kind               = opts.tol_kind;
    in.max_depth              = opts.max_depth;
    in.max_memory_mib         = opts.max_memory_mib < 0 ? auto_memory_budget_mib(input_dim) : opts.max_memory_mib;
    in.allow_max_depth_leaves = opts.allow_max_depth_leaves;
    in.min_uniform_depth      = opts.min_uniform_depth;
    return in;
}

template <class Domain>
inline auto midpoint(const Domain &a, const Domain &b) -> Domain {
    // Scale by the domain's own element type so `float` corners stay in
    // float arithmetic (a literal `0.5` would promote them to double).
    if constexpr (std::is_arithmetic_v<Domain>) {
        return static_cast<Domain>(0.5) * (a + b);
    } else {
        using elem_t = Domain::value_type;
        Domain out{};
        for (std::size_t i = 0; i < a.size(); ++i)
            out[i] = static_cast<elem_t>(0.5) * (a[i] + b[i]);
        return out;
    }
}

template <class Domain>
inline auto half_length(const Domain &a, const Domain &b) -> Domain {
    if constexpr (std::is_arithmetic_v<Domain>) {
        return static_cast<Domain>(0.5) * (b - a);
    } else {
        using elem_t = Domain::value_type;
        Domain out{};
        for (std::size_t i = 0; i < a.size(); ++i)
            out[i] = static_cast<elem_t>(0.5) * (b[i] - a[i]);
        return out;
    }
}

template <class Domain>
constexpr auto domain_dim() -> int {
    if constexpr (std::is_arithmetic_v<Domain>)
        return 1;
    else
        return static_cast<int>(std::tuple_size_v<Domain>);
}

} // namespace detail

/// Canonical "fit f on [a, b] to tolerance" one-liner. Panel count falls out
/// of the adaptive tree; the leaf degree is a hyperparameter with a
/// reasonable default. `tol` is positional (no default) so every call site
/// makes its target accuracy explicit.
template <std::size_t Degree = detail::kDefaultDegree, EvalPolicy Policy = EvalPolicy::Balanced, class Func,
          class Domain>
    requires Fittable<std::remove_cvref_t<Func>, Domain>
[[nodiscard]] auto fit(Func &&f, Domain a, Domain b, double tol, options opts = {}) {
    if (!(tol > 0.0))
        throw std::invalid_argument("treeweave::fit: tolerance must be > 0");

    using func_t          = std::remove_cvref_t<Func>;
    using result_t        = std::invoke_result_t<func_t &, Domain>;
    constexpr int in_dim  = detail::domain_dim<Domain>();
    constexpr int out_dim = detail::domain_dim<result_t>();

    auto input = detail::make_input(in_dim, out_dim, static_cast<int>(Degree), tol, opts);
    return Function<Degree, func_t, Policy>(input, detail::midpoint(a, b), detail::half_length(a, b),
                                            std::forward<Func>(f));
}

} // namespace treeweave

#endif // TREEWEAVE_TREEWEAVE_HPP
