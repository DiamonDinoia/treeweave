#ifndef TREEWEAVE_DETAIL_C_BINDING_HPP
#define TREEWEAVE_DETAIL_C_BINDING_HPP

/// \file treeweave/detail/c_binding.hpp
/// \brief ABI-stable glue between the extern "C" surface (treeweave.h) and the
///        C++ template library. This header carries only the *light* surface:
///        the type-erased evaluator interface (`IEval<T>`), the C-callback /
///        options mapping, and the macro-free dispatch plumbing that routes a
///        runtime (arch, output_dim) onto a compile-time instantiation.
///
/// The heavy machinery is split out so the dispatcher / entry-point TUs stay
/// instantiation-free:
///   * c_binding_detail.hpp  — `EvalImpl` / `EvalFactory` / `wrap_callback`,
///     pulled into an anonymous namespace by each per-arch variant TU so its
///     instantiations get *internal* linkage and are never COMDAT-folded
///     across architectures.
///   * c_binding_dispatch.hpp — the body of `make_eval_for<Arch, …>`, the one
///     external symbol per (arch, dtype, dim). Keying it on the xsimd `Arch`
///     type gives each `-march` variant a distinct mangled name, so the four
///     variants coexist with external linkage and the baseline dispatcher can
///     bind to them through `xsimd::dispatch`.
///
/// `SelectMakeEval` (the `xsimd::dispatch` functor) lives here and instantiates
/// *no* kernels — it only takes the address of the declared-only `make_eval_for`
/// external — so every TU that includes this header stays cheap to compile.
///
/// Degree is baked to `chosen_degree<Arch,T,IN>` (= 7 everywhere; see
/// include/treeweave/detail/arch_degree_table.hpp). `select_degree` and the
/// per-degree poet dispatch are removed.

#include <array>
#include <cstddef>
#include <functional>
#include <type_traits>
#include <utility>

#include <poet/core/dispatch.hpp>
#include <treeweave.h>

#include <treeweave/detail/arch_degree_table.hpp>
#include <treeweave/detail/eval_policy.hpp>
#include <treeweave/detail/function.hpp>
#include <treeweave/detail/tol_kind.hpp>
#include <treeweave/treeweave.hpp>

namespace treeweave::capi {

// Supported shape set (mirrored in treeweave.h and the fit error messages):
//   degree     = 7 (baked; chosen_degree<Arch,T,IN> = 7 everywhere)
//   input_dim  in {1, 2, 3}
//   output_dim in {1, 2, 3}
// The poet::dispatch sequence (output_dim) and the CMake-driven variant-TU
// fan-out (dtype, input_dim) are the single source of truth for which
// (in, out) are instantiated.

/// Set the calling thread's last-error string (defined in src/capi/treeweave.cpp).
void set_last_error(const char *msg) noexcept;

/// Map the C options struct (or the defaults, when `opts == nullptr`) onto
/// the C++ fit knobs.
inline auto to_options(const treeweave_opts *opts) -> treeweave::options {
    treeweave::options out{};
    if (opts == nullptr)
        return out;
    out.tol_kind               = static_cast<treeweave::TolKind>(opts->tol_kind);
    out.max_depth              = opts->max_depth;
    out.max_memory_mib         = opts->max_memory_mib;
    out.allow_max_depth_leaves = opts->allow_max_depth_leaves != 0;
    out.min_uniform_depth      = opts->min_uniform_depth;
    return out;
}

/// C callback pointer type for value type `T`.
template <class T>
using c_func_t = void (*)(const T *, T *, void *);

/// Runtime-polymorphic evaluator interface for value type `T`. One concrete
/// `EvalImpl` per supported (IN, OUT, Policy) implements it; the C shims hold
/// an `IEval<T>*` behind the opaque handle. Only scalar `const T*`/`T*` cross
/// this interface, so no SIMD type — and therefore no arch-dependent
/// `sizeof` — ever crosses a TU boundary.
template <class T>
struct IEval {
    virtual ~IEval() = default;
    /// Single point: `x` has input_dim coords, `y` has output_dim slots.
    virtual void eval(const T *x, T *y) const = 0;
    /// AoS batch: `x` is n*input_dim, `res` is n*output_dim.
    virtual void eval_multi(const T *x, T *res, std::size_t n) const = 0;
    /// Sorted 1D batch; sets last_error and no-ops for input_dim != 1.
    virtual void eval_sorted(const T *x, T *res, std::size_t n) const = 0;
    /// SoA batch; sets last_error and no-ops for output_dim == 1.
    virtual void               eval_multi_soa(const T *x, T *const *soa, std::size_t n) const = 0;
    [[nodiscard]] virtual auto memory_usage() const -> std::size_t                            = 0;
    virtual void               print_stats() const                                            = 0;
};

/// One external factory symbol per (arch, value_type, input_dim).
/// Declared here (no body); defined in c_binding_dispatch.hpp and explicitly
/// instantiated — once, at `xsimd::best_arch` — by each per-arch variant TU.
/// `Arch` keys the mangled name so the per-`-march` variants stay distinct;
/// the body ignores it (vector width comes from the TU's `-march` macros).
/// Degree is baked to `chosen_degree<Arch,T,IN>` (= 7). Builds and returns a
/// fitted `IEval<T>` for the requested `output_dim`, or nullptr when
/// `output_dim` is outside the supported set.
template <class Arch, class T, std::size_t IN, int Deg>
auto make_eval_for(int output_dim, c_func_t<T> f, void *data, const T *a, const T *b, double tol,
                   const treeweave::options &opts) -> IEval<T> *;

/// Type-erased factory pointer for a fixed value type `T`: the (arch-resolved)
/// `make_eval_for` signature with the `Arch`/`Deg` template parameters stripped.
/// The signature is identical for every arch, so a single pointer type carries
/// any selected variant.
template <class T>
using make_eval_fn_t = auto (*)(int output_dim, c_func_t<T> f, void *data, const T *a, const T *b, double tol,
                                const treeweave::options &opts) -> IEval<T> *;

/// `xsimd::dispatch` functor for a fixed (value_type, input_dim). For the widest
/// arch the host CPU supports, `operator()(Arch)` returns the *address* of
/// `make_eval_for<Arch, T, IN, chosen_degree<Arch,T,IN>>` — it does NOT run the
/// fit. That keeps the functor `noexcept` (safe inside `xsimd::dispatch`'s
/// noexcept walk); the caller invokes the returned pointer afterwards, outside
/// the dispatch, where the fit may throw and propagate to the extern "C" shim.
/// Degree is baked — the functor carries no state.
template <class T, std::size_t IN>
struct SelectMakeEval {
    template <class Arch>
    auto operator()(Arch /*tag*/) const noexcept -> make_eval_fn_t<T> {
        return &make_eval_for<Arch, T, IN, chosen_degree<Arch, T, IN>>;
    }
};

/// Per-(value_type, input_dim) factory. Declared here, defined (one each) in
/// the entry-point TU selected by CMake: src/capi/arch_single.cpp (single
/// arch; multiarch OFF or non-x86) or src/capi/arch_dispatch.cpp (x86 runtime
/// dispatch; multiarch ON). Degree is baked — these take no degree argument.
auto make_eval_f64_dim1(int output_dim, treeweave_func_t f, void *data, const double *a, const double *b, double tol,
                        const treeweave::options &opts) -> IEval<double> *;
auto make_eval_f64_dim2(int output_dim, treeweave_func_t f, void *data, const double *a, const double *b, double tol,
                        const treeweave::options &opts) -> IEval<double> *;
auto make_eval_f64_dim3(int output_dim, treeweave_func_t f, void *data, const double *a, const double *b, double tol,
                        const treeweave::options &opts) -> IEval<double> *;
auto make_eval_f32_dim1(int output_dim, treeweavef_func_t f, void *data, const float *a, const float *b, double tol,
                        const treeweave::options &opts) -> IEval<float> *;
auto make_eval_f32_dim2(int output_dim, treeweavef_func_t f, void *data, const float *a, const float *b, double tol,
                        const treeweave::options &opts) -> IEval<float> *;
auto make_eval_f32_dim3(int output_dim, treeweavef_func_t f, void *data, const float *a, const float *b, double tol,
                        const treeweave::options &opts) -> IEval<float> *;

} // namespace treeweave::capi

#endif // TREEWEAVE_DETAIL_C_BINDING_HPP
