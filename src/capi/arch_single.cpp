// arch_single.cpp — single-architecture C-ABI entry points.
//
// Selected by CMake when multi-arch dispatch is OFF (the default) or the
// target is not x86. Compiled at the same `-march` as the variant TUs, so
// `xsimd::best_arch` here equals the `best_arch` those TUs exported their
// `make_eval_for` symbols under — the per-(dtype, dim) entry points route
// the call straight through `make_eval_for` with the baked degree.
//
// This TU instantiates no kernels: it only references declared-only
// `make_eval_for` externals, whose definitions live in the variant TUs.
// Degree is baked to `chosen_degree<Arch,T,IN>` (= 7 everywhere).

#include <xsimd/xsimd.hpp>

#include <treeweave.h>

#include <treeweave/detail/arch_degree_table.hpp>
#include <treeweave/detail/c_binding.hpp>
#include <treeweave/treeweave.hpp>

namespace treeweave::capi {

auto make_eval_f64_dim1(int output_dim, treeweave_func_t f, void *data, const double *a, const double *b, double tol,
                        const treeweave::options &opts) -> IEval<double> * {
    return make_eval_for<xsimd::best_arch, double, 1, chosen_degree<xsimd::best_arch, double, 1>>(output_dim, f, data,
                                                                                                  a, b, tol, opts);
}
auto make_eval_f64_dim2(int output_dim, treeweave_func_t f, void *data, const double *a, const double *b, double tol,
                        const treeweave::options &opts) -> IEval<double> * {
    return make_eval_for<xsimd::best_arch, double, 2, chosen_degree<xsimd::best_arch, double, 2>>(output_dim, f, data,
                                                                                                  a, b, tol, opts);
}
auto make_eval_f64_dim3(int output_dim, treeweave_func_t f, void *data, const double *a, const double *b, double tol,
                        const treeweave::options &opts) -> IEval<double> * {
    return make_eval_for<xsimd::best_arch, double, 3, chosen_degree<xsimd::best_arch, double, 3>>(output_dim, f, data,
                                                                                                  a, b, tol, opts);
}
auto make_eval_f32_dim1(int output_dim, treeweavef_func_t f, void *data, const float *a, const float *b, double tol,
                        const treeweave::options &opts) -> IEval<float> * {
    return make_eval_for<xsimd::best_arch, float, 1, chosen_degree<xsimd::best_arch, float, 1>>(output_dim, f, data, a,
                                                                                                b, tol, opts);
}
auto make_eval_f32_dim2(int output_dim, treeweavef_func_t f, void *data, const float *a, const float *b, double tol,
                        const treeweave::options &opts) -> IEval<float> * {
    return make_eval_for<xsimd::best_arch, float, 2, chosen_degree<xsimd::best_arch, float, 2>>(output_dim, f, data, a,
                                                                                                b, tol, opts);
}
auto make_eval_f32_dim3(int output_dim, treeweavef_func_t f, void *data, const float *a, const float *b, double tol,
                        const treeweave::options &opts) -> IEval<float> * {
    return make_eval_for<xsimd::best_arch, float, 3, chosen_degree<xsimd::best_arch, float, 3>>(output_dim, f, data, a,
                                                                                                b, tol, opts);
}

} // namespace treeweave::capi
