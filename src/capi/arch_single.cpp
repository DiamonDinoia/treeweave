// arch_single.cpp: single-architecture C-ABI entry points.
//
// Selected by CMake when multi-arch dispatch is OFF (the default) or the
// target has no ISA ladder. The variant TUs compile at the same `-march` as
// this TU and use the header-only `InlineKernels` policy, so every kernel is
// fully inlined into the pipeline — no function-pointer indirection. The
// per-(dtype, dim) entry points route straight through `make_eval_for` with
// the baked degree.
//
// This TU instantiates no kernels: it only references declared-only
// `make_eval_for` externals, whose definitions live in the variant TUs.

#include <treeweave.h>

#include <treeweave/detail/c_binding.hpp>
#include <treeweave/treeweave.hpp>

namespace treeweave::capi {

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
