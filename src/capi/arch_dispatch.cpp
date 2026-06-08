// arch_dispatch.cpp — runtime multi-architecture C-ABI entry points.
//
// Selected by CMake when TREEWEAVE_C_MULTIARCH is ON and the target is a family
// with a runtime variant fan-out (x86 ladder / aarch64 neon64 / riscv rvv).
// Compiled at the family baseline `-march` (x86-64 / armv8-a / rv64gcv) — it
// emits no SIMD itself, only a CPU-feature probe and indirect calls. Each entry
// point asks `xsimd::dispatch` for the widest arch the host CPU actually
// supports, then runs the fit through the `make_eval_for<Arch, …>` external for
// that arch — the declared-only symbols the linker binds to the matching
// per-`-march` variant object.
//
// Why `xsimd::dispatch` returns a *factory pointer* rather than the fitted
// object: `xsimd::dispatch(...)` and its `operator()` are `noexcept`, but the
// fit can throw (invalid tol, MaxDepthExceeded, …) and must reach the catch
// block in treeweave.cpp. So the dispatch functor only *selects* — it returns the
// address of `make_eval_for<Arch,…>` (no fit, no throw) — and we invoke that
// pointer afterwards, outside the noexcept dispatch, where exceptions propagate
// normally. Runtime arch selection itself is xsimd's
// `available_architectures().has(Arch)` (NOT `Arch::available()`, which is a
// constexpr `true` for every real arch and would blindly pick the widest
// *compiled* arch on every CPU and #UD on hosts lacking it).
//
// The arch_list is family-selected at compile time (see dispatch_arch.hpp):
// on x86 it is the fixed four-type ladder, each entry equal to the `best_arch`
// selected at the corresponding `-march` (sse2 ⇐ x86-64, sse4_2 ⇐ x86-64-v2,
// fma3<avx2> ⇐ x86-64-v3, avx512bw ⇐ x86-64-v4); on aarch64 it is the single
// neon64; on riscv the single rvv128. Each entry must match a `make_eval_for`
// symbol the variant TUs emit, else it surfaces as a clean undefined-symbol
// link error.
//
// Degree is baked to `chosen_degree<Arch,T,IN>` (= 7 everywhere); these entry
// points carry no degree argument.

#include <cstdlib>
#include <string_view>

#include <xsimd/xsimd.hpp>

#include <treeweave/detail/c_binding.hpp>
#include <treeweave/detail/dispatch_arch.hpp>

namespace treeweave::capi {
namespace {

/// Testing-only override: if TREEWEAVE_FORCE_ARCH names a ladder arch the host
/// actually supports (runtime-checked via available_architectures().has — a
/// level at or below the host's widest), return that arch's make_eval_for
/// pointer so a single capable box can exercise every fallback edge.
/// Unset / unknown / unsupported → nullptr, and the caller falls back to the
/// normal xsimd::dispatch selection.
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

/// Pick the per-arch `make_eval_for` factory for this (T, IN): the optional
/// TREEWEAVE_FORCE_ARCH test override first, otherwise xsimd::dispatch walks
/// `dispatch_arch_list` high→low and returns the widest host-supported arch's
/// factory pointer. The returned pointer is invoked by the caller (so the fit
/// runs outside xsimd::dispatch's noexcept context).
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
