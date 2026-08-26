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
///   * c_binding_detail.hpp: `EvalImpl` / `make_eval_impl` / `wrap_callback`,
///     pulled into an anonymous namespace by each baseline factory TU so its
///     instantiations get *internal* linkage.
///   * c_binding_dispatch.hpp: the body of `make_eval_one<T, IN, OUT>`, the
///     one external symbol per (dtype, dim, out). The fit, tree build and
///     pipeline glue behind it compile ONCE, at the family baseline `-march`,
///     one pipeline per TU.
///
/// The per-ISA fan-out covers only the hot loops: `kernels_arch.cpp` compiles
/// the leaf kernels once per rung, and `select_kernels` (defined in
/// arch_dispatch.cpp) picks one `KernelSet` of function pointers per handle
/// via `xsimd::dispatch`. Single-arch builds skip the indirection entirely:
/// `make_eval_impl` falls back to the header-only `InlineKernels` policy
/// (`TREEWEAVE_C_KERNELSET` undefined).
///
/// Degree is baked to `chosen_degree` (= 7; see
/// include/treeweave/detail/arch_degree_table.hpp).

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
//   degree     = 7 (baked; chosen_degree)
//   input_dim  in {1, 2, 3}
//   output_dim in {1, 2, 3}
// The CMake-driven factory-TU fan-out (dtype, input_dim, output_dim; see
// cmake/treeweave_c_dispatch.cmake) is the single source of truth for which
// shapes are instantiated; `make_eval_for` below routes the runtime
// output_dim onto them.

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
/// this interface, so no SIMD type, and therefore no arch-dependent `sizeof`,
/// ever crosses a TU boundary.
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

/// One external factory symbol per (value_type, input_dim, output_dim) shape:
/// fit, tree build and one eval pipeline, degree baked to `chosen_degree`.
/// Declared here (no body); defined in c_binding_dispatch.hpp and explicitly
/// instantiated, once and at baseline, by exactly one CMake-generated factory
/// TU (see cmake/treeweave_c_dispatch.cmake).
template <class T, std::size_t IN, std::size_t OUT>
auto make_eval_one(c_func_t<T> f, void *data, const T *a, const T *b, double tol,
                   const treeweave::options &opts) -> IEval<T> *;

/// Route a runtime `output_dim` onto the matching `make_eval_one`
/// instantiation; nullptr when `output_dim` is outside the supported set.
/// When the shape set gains an output_dim, widen the range here AND extend
/// the CMake fan-out plus the instantiation lists it points to.
template <class T, std::size_t IN>
auto make_eval_for(int output_dim, c_func_t<T> f, void *data, const T *a, const T *b, double tol,
                   const treeweave::options &opts) -> IEval<T> * {
    return poet::dispatch(
        [&]<int Out>() -> IEval<T> * {
            return make_eval_one<T, IN, static_cast<std::size_t>(Out)>(f, data, a, b, tol, opts);
        },
        poet::dispatch_param<poet::inclusive_range<1, 3>>{output_dim});
}

/// Multi-arch only: the runtime kernel selection for one (T, IN, NC, OUT)
/// shape. Defined in src/capi/arch_dispatch.cpp (TREEWEAVE_FORCE_ARCH
/// override, else `xsimd::dispatch` picks the widest host-supported rung's
/// `make_kernels_for`). Single-arch builds never instantiate this.
template <class T, std::size_t IN, std::size_t NC, std::size_t OUT>
auto select_kernels() -> detail::KernelSet<T, IN, NC, OUT>;

/// Per-(value_type, input_dim) factory. Declared here, defined (one each) in
/// the entry-point TU selected by CMake: src/capi/arch_single.cpp (single
/// arch; multiarch OFF or non-x86) or src/capi/arch_dispatch.cpp (x86 runtime
/// dispatch; multiarch ON). Degree is baked in, so these take no degree
/// argument.
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
