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
/// Degree is baked to `treeweave::detail::kDefaultDegree`, the same default
/// the C++ API uses; the C ABI never varies it per CPU.

#include <array>
#include <cstddef>
#include <functional>
#include <type_traits>
#include <utility>

#include <treeweave.h>

#include <treeweave/detail/eval_policy.hpp>
#include <treeweave/detail/function.hpp>
#include <treeweave/detail/tol_kind.hpp>
#include <treeweave/treeweave.hpp>

namespace treeweave::capi {

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
/// fit, tree build and one eval pipeline, degree baked to `kDefaultDegree`.
/// Declared here (no body); defined in c_binding_dispatch.hpp and explicitly
/// instantiated, once and at baseline, by exactly one CMake-generated factory
/// TU (see cmake/treeweave_c_dispatch.cmake).
template <class T, std::size_t IN, std::size_t OUT>
auto make_eval_one(c_func_t<T> f, void *data, const T *a, const T *b, double tol,
                   const treeweave::options &opts) -> IEval<T> *;

/// Multi-arch only: the runtime kernel selection for one (T, IN, NC, OUT)
/// shape. Defined in src/capi/arch_dispatch.cpp (TREEWEAVE_FORCE_ARCH
/// override, else `xsimd::dispatch` picks the widest host-supported rung's
/// `make_kernels_for`). Single-arch builds never instantiate this.
template <class T, std::size_t IN, std::size_t NC, std::size_t OUT>
auto select_kernels() -> detail::KernelSet<T, IN, NC, OUT>;

/// Name of the ISA rung this build runs, read from that rung's own object.
/// Defined beside the factories above, in the same entry-point TU.
auto active_arch() -> const char *;

/// 1 when `name` is a ladder rung this host can run, 0 when the ladder has it
/// but the host cannot, -1 when the ladder has no rung of that name.
auto arch_available(const char *name) -> int;

} // namespace treeweave::capi

#endif // TREEWEAVE_DETAIL_C_BINDING_HPP
