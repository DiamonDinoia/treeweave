// arch_dispatch.cpp — runtime multi-architecture C-ABI entry points.
//
// Selected by CMake when TREEWEAVE_C_MULTIARCH is ON. Compiled at the family
// baseline -march; emits no SIMD itself, only a CPU-feature probe + indirect
// call. Dispatch functor returns a *factory pointer* (not the fitted object)
// so the fit runs outside xsimd::dispatch's noexcept context and exceptions
// propagate to treeweave.cpp's catch block. Uses available_architectures().has
// (not Arch::available(), which is constexpr-true and would SIGILL non-AVX512
// hosts). Arch_list: see dispatch_arch.hpp.

#include <cstdlib>
#include <string_view>

#include <xsimd/xsimd.hpp>

#include <treeweave/detail/c_binding.hpp>
#include <treeweave/detail/dispatch_arch.hpp>

namespace treeweave::capi {
namespace {

/// Testing-only: if TREEWEAVE_FORCE_ARCH names a supported ladder arch, return
/// its make_eval_for pointer so one capable host can exercise every fallback.
/// Unset / unknown / unsupported → nullptr (caller falls back to xsimd::dispatch).
template <class T, std::size_t IN>
auto force_select(std::string_view want) -> make_eval_fn_t<T> {
    const auto        archs = xsimd::available_architectures();
    make_eval_fn_t<T> out   = nullptr;
    auto              visit = [&]<class Arch>() {
        if (out == nullptr && want == Arch::name() && archs.has(Arch{})) {
            out = SelectMakeEval<T, IN>{}(Arch{});
        }
    };
    [&]<class... Arch>(xsimd::arch_list<Arch...> /*tag*/) {
        (visit.template operator()<Arch>(), ...);
    }(dispatch_arch_list{});
    return out;
}

/// TREEWEAVE_FORCE_ARCH override first; otherwise xsimd::dispatch picks the
/// widest host-supported arch. Returned pointer invoked by caller (outside noexcept).
template <class T, std::size_t IN>
auto select_make_eval() -> make_eval_fn_t<T> {
    if (const char *want = std::getenv("TREEWEAVE_FORCE_ARCH")) {
        if (auto *forced = force_select<T, IN>(want)) {
            return forced;
        }
    }
    return xsimd::dispatch<dispatch_arch_list>(SelectMakeEval<T, IN>{})();
}

} // namespace

auto make_eval_f64_dim1(int output_dim, treeweave_func_t f, void *data, const double *a, const double *b, double tol,
                        const treeweave::options &opts) -> IEval<double> * {
    return select_make_eval<double, 1>()(output_dim, f, data, a, b, tol, opts);
}
auto make_eval_f64_dim2(int output_dim, treeweave_func_t f, void *data, const double *a, const double *b, double tol,
                        const treeweave::options &opts) -> IEval<double> * {
    return select_make_eval<double, 2>()(output_dim, f, data, a, b, tol, opts);
}
auto make_eval_f64_dim3(int output_dim, treeweave_func_t f, void *data, const double *a, const double *b, double tol,
                        const treeweave::options &opts) -> IEval<double> * {
    return select_make_eval<double, 3>()(output_dim, f, data, a, b, tol, opts);
}
auto make_eval_f32_dim1(int output_dim, treeweavef_func_t f, void *data, const float *a, const float *b, double tol,
                        const treeweave::options &opts) -> IEval<float> * {
    return select_make_eval<float, 1>()(output_dim, f, data, a, b, tol, opts);
}
auto make_eval_f32_dim2(int output_dim, treeweavef_func_t f, void *data, const float *a, const float *b, double tol,
                        const treeweave::options &opts) -> IEval<float> * {
    return select_make_eval<float, 2>()(output_dim, f, data, a, b, tol, opts);
}
auto make_eval_f32_dim3(int output_dim, treeweavef_func_t f, void *data, const float *a, const float *b, double tol,
                        const treeweave::options &opts) -> IEval<float> * {
    return select_make_eval<float, 3>()(output_dim, f, data, a, b, tol, opts);
}

} // namespace treeweave::capi
