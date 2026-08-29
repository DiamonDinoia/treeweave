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
/// For pooled reuse, pass a stateful allocator;
/// `std::pmr::polymorphic_allocator` over a `monotonic_buffer_resource`
/// is the idiomatic choice. See `tests/test_threadsafe.cpp`.
///
/// Memory cost. Tightening `tol`, raising `max_depth`, raising
/// `max_memory_mib`, or enabling `allow_max_depth_leaves` all increase the
/// leaf count `L`. The Function holds one persistent allocation: the tree
/// itself, `O(L)`, shared by all threads and reported by
/// `Function::print_stats()` / `memory_usage()`. Each batch call
/// allocates and frees on return a scratch of roughly
/// `tile_K * (4 + (input_dim + output_dim) * sizeof(value_type))` plus
/// `4 * (L + 1)` bytes (with `tile_K = max(65536, 32 * L)`) and frees it
/// on return.

#include <array>
#include <complex>
#include <concepts>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include <polyfit/polyfit.hpp>

#include <treeweave_version.h> // generated; single source of truth (the VERSION file)

#include <treeweave/detail/eval_policy.hpp>
#include <treeweave/detail/tol_kind.hpp>

#include <treeweave/detail/function_impl.hpp>

namespace treeweave {

/// The default inline kernel policy used across the batch and guru APIs.
/// (Defined under treeweave::detail; the alias is the documented spelling for
/// callers that reference the default type in their own wrappers.)
using default_kernel_policy = detail::InlineKernels;


/// Library version, sourced from the CMake-generated <treeweave_version.h>
/// (single source of truth: `project(VERSION)`); identical to the C-ABI
/// `TREEWEAVE_VERSION_*` macros in <treeweave.h>.
inline constexpr int version_major = TREEWEAVE_VERSION_MAJOR;
inline constexpr int version_minor = TREEWEAVE_VERSION_MINOR;
inline constexpr int version_patch = TREEWEAVE_VERSION_PATCH;
/// Combined integer MAJOR*10000 + MINOR*100 + PATCH (e.g. 1.2.3 -> 10203).
inline constexpr int version = TREEWEAVE_VERSION;
/// Dotted release string, e.g. "1.2.3".
inline constexpr const char *version_string = TREEWEAVE_VERSION_STRING;

/// Compile-time guard: true iff the headers are at least `maj.min.pat`.
constexpr auto version_at_least(int maj, int min, int pat) -> bool { return version >= maj * 10000 + min * 100 + pat; }

/// Runtime fit knobs. Every field here only affects fit-time
/// construction; there is no eval-time benefit to promoting any of them
/// to a template parameter, so they stay plain data.
struct options {
    /// How the convergence check interprets `tol`. The default
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
    ///   * `< 0` (default): auto, a small, dimension-scaled budget,
    ///     `4 << (input_dim-1)` MiB (4 / 8 / 16 for 1D / 2D / 3D). Leaf
    ///     storage grows ~geometrically with `input_dim`, so a single flat
    ///     cap is either too loose in 1D or too tight in 3D; scaling keeps
    ///     the default a genuine guardrail in every dimension.
    ///   * `0`: disabled (no cap).
    ///   * `> 0`: explicit cap in MiB.
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
    /// a uniform tree but not the table. Default 0: no forcing,
    /// tol-based refinement only.
    int min_uniform_depth = 0;
};

/// Any callable that accepts `Domain` and returns a value.
template <class F, class Domain>
concept Fittable = requires(F f, Domain x) { f(x); };

namespace detail {

// Auto memory budget (MiB): 4/8/16 for 1D/2D/3D (doubles per dim; a flat cap
// is too tight in 3D).
constexpr auto auto_memory_budget_mib(int input_dim) -> int {
    const unsigned d = input_dim < 1 ? 1U : static_cast<unsigned>(input_dim);
    return static_cast<int>(4U << (d - 1U));
}

// Default leaf degree: 7 wins or ties every (arch, dtype, dim) cell, and is
// spill-free in the wide SIMD cells.
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
constexpr auto midpoint(const Domain &a, const Domain &b) -> Domain {
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
constexpr auto half_length(const Domain &a, const Domain &b) -> Domain {
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

// ---- Ergonomic complex / scalar-input adaptors (issue #23) ---------------
// The core fitter only speaks real array-in / array-out. These map the
// natural spellings (scalar `double` in, `std::complex` out) onto it and back,
// purely at the fit()/operator() boundary; the leaf math stays real.

template <class T>
inline constexpr bool is_complex_v = false;
template <class T>
inline constexpr bool is_complex_v<std::complex<T>> = true;

template <class R>
struct complex_elem {
    using type = R;
};
template <class T>
struct complex_elem<std::complex<T>> {
    using type = T;
};
template <class R>
using complex_elem_t = complex_elem<R>::type;

// What the core Function fits: scalar domain -> 1-element array;
// complex result -> 2-element real array (re, im); everything else unchanged.
template <class D>
using canonical_input_t = std::conditional_t<std::is_arithmetic_v<D>, std::array<D, 1>, D>;
template <class R>
using canonical_output_t = std::conditional_t<is_complex_v<R>, std::array<complex_elem_t<R>, 2>, R>;

// fit() must interpose an adaptor when the result is complex, or a scalar
// domain has a vector output (the case the core static_assert rejects).
template <class Domain, class Result>
inline constexpr bool needs_adaptor_v =
    is_complex_v<Result> || (std::is_arithmetic_v<Domain> && domain_dim<canonical_output_t<Result>>() > 1);

// Wraps the user callable into canonical array-in / array-out so the core
// fitter (polyfit's FuncEvalND) only ever sees real vector math.
template <class Func, class Domain>
struct CanonicalFn {
    Func f;
    using result_t  = std::invoke_result_t<Func &, Domain>;
    using canon_in  = canonical_input_t<Domain>;
    using canon_out = canonical_output_t<result_t>;

    auto operator()(const canon_in &xi) const -> canon_out {
        if constexpr (is_complex_v<result_t>) {
            result_t r;
            if constexpr (std::is_arithmetic_v<Domain>)
                r = f(xi[0]);
            else
                r = f(xi);
            return {r.real(), r.imag()};
        } else if constexpr (std::is_arithmetic_v<Domain>) {
            return f(xi[0]);
        } else {
            return f(xi);
        }
    }
};

// Restores the user-facing spelling around a real-valued Function.
// std::complex<T> is layout-compatible with T[2] and std::array<T,N> with
// T[N], so the batch/sorted overloads just reinterpret the output buffer,
// with no per-point repack. Point eval converts the one returned value.
template <class Inner, class Domain, class Result>
class AdaptedFunction {
    Inner fn_;

  public:
    using value_type  = Inner::value_type;
    using input_type  = Domain;
    using output_type = Result;

    explicit AdaptedFunction(Inner fn) : fn_(std::move(fn)) {}

    // Escape hatch to the real Function (num_leaves, memory_usage, subtrees, ...).
    [[nodiscard]] auto function() const noexcept -> const Inner & { return fn_; }

    [[nodiscard]] auto operator()(const Domain &x) const -> Result {
        canonical_input_t<Domain> xi;
        if constexpr (std::is_arithmetic_v<Domain>)
            xi = {x};
        else
            xi = x;
        const auto r = fn_(xi);
        if constexpr (is_complex_v<Result>)
            return Result{r[0], r[1]};
        else
            return r;
    }

    template <class Allocator = std::allocator<value_type>>
    auto operator()(const value_type *xp, Result *res, std::size_t n, const Allocator &alloc = {}) const -> void {
        fn_(xp, reinterpret_cast<value_type *>(res), n, alloc);
    }

    auto sorted(const value_type *xp, Result *res, std::size_t n) const -> void
        requires std::is_arithmetic_v<Domain>
    {
        fn_.sorted(xp, reinterpret_cast<value_type *>(res), n);
    }
};

} // namespace detail

/// Canonical "fit f on [a, b] to tolerance" one-liner. Panel count falls out
/// of the adaptive tree; the leaf degree is a hyperparameter with a
/// reasonable default. `tol` is positional (no default) so every call site
/// makes its target accuracy explicit.
template <std::size_t Degree = detail::kDefaultDegree, EvalPolicy Policy = EvalPolicy::Balanced, class Func,
          class Domain>
    requires Fittable<std::remove_cvref_t<Func>, Domain>
// cppcheck-suppress passedByValue  ; options is 20 B and trivially copyable, and `= {}` needs a value
[[nodiscard]] auto fit(Func &&f, Domain a, Domain b, double tol, options opts = {}) {
    if (!(tol > 0.0))
        throw std::invalid_argument("treeweave::fit: tolerance must be > 0");

    // decay (not remove_cvref): a plain function passed by reference decays to a
    // function *pointer*, so the stored Func is never a bare function type. A bare
    // function type would make the internal `const Func&` params const-qualified
    // (harmless on gcc/clang, but MSVC warns C4180, fatal under /WX).
    using func_t   = std::decay_t<Func>;
    using result_t = std::invoke_result_t<func_t &, Domain>;

    if constexpr (detail::needs_adaptor_v<Domain, result_t>) {
        // Route scalar-in / complex-out spellings through the real ND path via
        // a canonical array-in / array-out wrapper (issue #23).
        using canon_fn_t      = detail::CanonicalFn<func_t, Domain>;
        using canon_in        = canon_fn_t::canon_in;
        using canon_out       = canon_fn_t::canon_out;
        constexpr int in_dim  = detail::domain_dim<canon_in>();
        constexpr int out_dim = detail::domain_dim<canon_out>();

        auto to_canon = [](Domain v) -> canon_in {
            if constexpr (std::is_arithmetic_v<Domain>)
                return canon_in{v};
            else
                return v;
        };
        auto input    = detail::make_input(in_dim, out_dim, static_cast<int>(Degree), tol, opts);
        using inner_t = Function<Degree, canon_fn_t, Policy>;
        inner_t inner(input, detail::midpoint(to_canon(a), to_canon(b)), detail::half_length(to_canon(a), to_canon(b)),
                      canon_fn_t{std::forward<Func>(f)});
        return detail::AdaptedFunction<inner_t, Domain, result_t>(std::move(inner));
    } else {
        constexpr int in_dim  = detail::domain_dim<Domain>();
        constexpr int out_dim = detail::domain_dim<result_t>();

        auto input = detail::make_input(in_dim, out_dim, static_cast<int>(Degree), tol, opts);
        return Function<Degree, func_t, Policy>(input, detail::midpoint(a, b), detail::half_length(a, b),
                                                std::forward<Func>(f));
    }
}

} // namespace treeweave

#endif // TREEWEAVE_TREEWEAVE_HPP
