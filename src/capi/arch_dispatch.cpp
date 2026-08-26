// arch_dispatch.cpp: runtime multi-architecture kernel selection.
//
// Selected by CMake when TREEWEAVE_C_MULTIARCH is ON. Compiled at the family
// baseline -march; emits no SIMD itself, only a CPU-feature probe. The
// factory entries call the baseline `make_eval_for` (fit, tree build and
// pipeline glue compile once); the per-ISA choice happens in
// `select_kernels`, which returns the `KernelSet` of the widest
// host-supported rung, or the TREEWEAVE_FORCE_ARCH override. Uses
// available_architectures().has (not Arch::available(), which is
// constexpr-true and would SIGILL non-AVX512 hosts). Arch list: see
// dispatch_arch.hpp.

#include <cstdlib>
#include <string_view>

#include <xsimd/xsimd.hpp>

#include <treeweave/detail/arch_degree_table.hpp>
#include <treeweave/detail/c_binding.hpp>
#include <treeweave/detail/dispatch_arch.hpp>

namespace treeweave::capi {
namespace {

/// `xsimd::dispatch` functor: builds the matched rung's kernel table.
/// `make_kernels_for` is noexcept and only fills a struct with function
/// pointers, so running it inside the dispatch is safe.
template <class T, std::size_t IN, std::size_t NC, std::size_t OUT>
struct SelectKernelsFn {
    template <class Arch>
    auto operator()(Arch /*tag*/) const noexcept -> detail::KernelSet<T, IN, NC, OUT> {
        return detail::make_kernels_for<Arch, T, IN, NC, OUT>();
    }
};

/// Testing-only: if TREEWEAVE_FORCE_ARCH names a supported ladder arch, fill
/// `out` with its kernel table so one capable host can exercise every
/// fallback. Unset / unknown / unsupported → false (caller falls back to
/// xsimd::dispatch).
template <class T, std::size_t IN, std::size_t NC, std::size_t OUT>
auto force_select(std::string_view want, detail::KernelSet<T, IN, NC, OUT> &out) -> bool {
    const auto archs = xsimd::available_architectures();
    bool       found = false;
    auto       visit = [&]<class Arch>() -> void {
        if (!found && want == Arch::name() && archs.has(Arch{})) {
            out   = detail::make_kernels_for<Arch, T, IN, NC, OUT>();
            found = true;
        }
    };
    [&]<class... Arch>(xsimd::arch_list<Arch...> /*tag*/) -> void {
        (visit.template operator()<Arch>(), ...);
    }(dispatch_arch_list{});
    return found;
}

} // namespace

template <class T, std::size_t IN, std::size_t NC, std::size_t OUT>
auto select_kernels() -> detail::KernelSet<T, IN, NC, OUT> {
    // NOLINTNEXTLINE(concurrency-mt-unsafe) — read-only env probe, set before any call.
    if (const char *want = std::getenv("TREEWEAVE_FORCE_ARCH")) {
        detail::KernelSet<T, IN, NC, OUT> forced{};
        if (force_select<T, IN, NC, OUT>(want, forced)) {
            return forced;
        }
    }
    return xsimd::dispatch<dispatch_arch_list>(SelectKernelsFn<T, IN, NC, OUT>{})();
}

// The shapes make_eval_impl can request. Single source of truth: the foreach
// lists in cmake/treeweave_c_dispatch.cmake — keep this list and
// kernels_arch.cpp's TREEWEAVE_KERNELS_FOR list in sync with them.
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage) — explicit-instantiation list.
#define TREEWEAVE_SELECT_KERNELS(T, IN, OUT)                                                                       \
    template auto select_kernels<T, IN, static_cast<std::size_t>(chosen_degree), OUT>()                           \
        -> detail::KernelSet<T, IN, static_cast<std::size_t>(chosen_degree), OUT>;

TREEWEAVE_SELECT_KERNELS(double, 1, 1)
TREEWEAVE_SELECT_KERNELS(double, 1, 2)
TREEWEAVE_SELECT_KERNELS(double, 1, 3)
TREEWEAVE_SELECT_KERNELS(double, 2, 1)
TREEWEAVE_SELECT_KERNELS(double, 2, 2)
TREEWEAVE_SELECT_KERNELS(double, 2, 3)
TREEWEAVE_SELECT_KERNELS(double, 3, 1)
TREEWEAVE_SELECT_KERNELS(double, 3, 2)
TREEWEAVE_SELECT_KERNELS(double, 3, 3)
TREEWEAVE_SELECT_KERNELS(float, 1, 1)
TREEWEAVE_SELECT_KERNELS(float, 1, 2)
TREEWEAVE_SELECT_KERNELS(float, 1, 3)
TREEWEAVE_SELECT_KERNELS(float, 2, 1)
TREEWEAVE_SELECT_KERNELS(float, 2, 2)
TREEWEAVE_SELECT_KERNELS(float, 2, 3)
TREEWEAVE_SELECT_KERNELS(float, 3, 1)
TREEWEAVE_SELECT_KERNELS(float, 3, 2)
TREEWEAVE_SELECT_KERNELS(float, 3, 3)

#undef TREEWEAVE_SELECT_KERNELS

auto make_eval_f64_dim1(int output_dim, treeweave_func_t f, void *data, const double *a, const double *b, double tol,
                        const treeweave::options &opts) -> IEval<double> * {
    return make_eval_for<double, 1>(output_dim, f, data, a, b, tol, opts);
}
auto make_eval_f64_dim2(int output_dim, treeweave_func_t f, void *data, const double *a, const double *b, double tol,
                        const treeweave::options &opts) -> IEval<double> * {
    return make_eval_for<double, 2>(output_dim, f, data, a, b, tol, opts);
}
auto make_eval_f64_dim3(int output_dim, treeweave_func_t f, void *data, const double *a, const double *b, double tol,
                        const treeweave::options &opts) -> IEval<double> * {
    return make_eval_for<double, 3>(output_dim, f, data, a, b, tol, opts);
}
auto make_eval_f32_dim1(int output_dim, treeweavef_func_t f, void *data, const float *a, const float *b, double tol,
                        const treeweave::options &opts) -> IEval<float> * {
    return make_eval_for<float, 1>(output_dim, f, data, a, b, tol, opts);
}
auto make_eval_f32_dim2(int output_dim, treeweavef_func_t f, void *data, const float *a, const float *b, double tol,
                        const treeweave::options &opts) -> IEval<float> * {
    return make_eval_for<float, 2>(output_dim, f, data, a, b, tol, opts);
}
auto make_eval_f32_dim3(int output_dim, treeweavef_func_t f, void *data, const float *a, const float *b, double tol,
                        const treeweave::options &opts) -> IEval<float> * {
    return make_eval_for<float, 3>(output_dim, f, data, a, b, tol, opts);
}

} // namespace treeweave::capi
