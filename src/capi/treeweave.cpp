/// src/capi/treeweave.cpp — the extern "C" surface declared in treeweave.h.
///
/// Holds the opaque handle definition, the thread-local last-error buffer,
/// the fit dispatch over input_dim (delegating to the per-(value_type,
/// input_dim) factories compiled in the sibling dispatch_*.cpp TUs), and the
/// dtype-checked eval / introspection / lifetime shims. No C++ exception is
/// allowed to escape any function here.
///
/// Degree is baked to chosen_degree<Arch,T,IN> (= 7 everywhere); these entry
/// points carry no degree argument and the opaque handle does not store one.

#include <cstddef>
#include <exception>
#include <limits>
#include <string>

#include <treeweave.h>

#include <treeweave/detail/c_binding.hpp>
#include <treeweave/treeweave.hpp>

namespace {

/// Thread-local description of the most recent error on the calling thread.
auto error_buffer() -> std::string & {
    thread_local std::string buf;
    return buf;
}

void clear_error() noexcept { error_buffer().clear(); }

} // namespace

namespace treeweave::capi {

void set_last_error(const char *msg) noexcept {
    try {
        error_buffer() = (msg != nullptr) ? msg : "";
        // At the C boundary a throwing std::string assignment (bad_alloc) leaves
        // the prior message intact; there is nothing else we can usefully do,
        // and nothing may escape this noexcept function.
    } catch (...) { // NOLINT(bugprone-empty-catch)
    }
}

} // namespace treeweave::capi

/// Opaque handle: a dtype tag plus a type-erased evaluator. `impl` is an
/// `IEval<double>*` when dtype == F64 and an `IEval<float>*` when F32.
struct treeweave_function {
    void             *impl;
    treeweave_dtype_t dtype;
    int               input_dim;
    int               output_dim;
};

extern "C" {

const treeweave_opts treeweave_default_opts = {
    .tol_kind               = TREEWEAVE_RELATIVE_MAX,
    .max_depth              = 50,
    .max_memory_mib         = -1, // auto: dimension-scaled (4/8/16 MiB)
    .allow_max_depth_leaves = 0,
    .min_uniform_depth      = 0,
};

auto treeweave_fit(treeweave_func_t f, int input_dim, int output_dim, const double *a, const double *b, double tol,
                   void *context, const treeweave_opts *opts) -> treeweave_t {
    using namespace treeweave::capi;
    clear_error();
    if (f == nullptr || a == nullptr || b == nullptr) {
        set_last_error("treeweave_fit: null callback or domain pointer");
        return nullptr;
    }

    const treeweave::options o    = to_options(opts);
    IEval<double>           *impl = nullptr;
    try {
        switch (input_dim) {
        case 1:
            impl = make_eval_f64_dim1(output_dim, f, context, a, b, tol, o);
            break;
        case 2:
            impl = make_eval_f64_dim2(output_dim, f, context, a, b, tol, o);
            break;
        case 3:
            impl = make_eval_f64_dim3(output_dim, f, context, a, b, tol, o);
            break;
        default:
            set_last_error("treeweave_fit: input_dim must be 1, 2, or 3");
            return nullptr;
        }
    } catch (const std::exception &e) {
        set_last_error(e.what());
        return nullptr;
    } catch (...) {
        set_last_error("treeweave_fit: unknown exception during fit");
        return nullptr;
    }

    if (impl == nullptr) {
        set_last_error("treeweave_fit: unsupported output_dim; "
                       "output_dim must be 1, 2, or 3");
        return nullptr;
    }
    return new treeweave_function{
        .impl = impl, .dtype = TREEWEAVE_F64, .input_dim = input_dim, .output_dim = output_dim};
}

auto treeweavef_fit(treeweavef_func_t f, int input_dim, int output_dim, const float *a, const float *b, double tol,
                    void *context, const treeweave_opts *opts) -> treeweave_t {
    using namespace treeweave::capi;
    clear_error();
    if (f == nullptr || a == nullptr || b == nullptr) {
        set_last_error("treeweavef_fit: null callback or domain pointer");
        return nullptr;
    }

    const treeweave::options o    = to_options(opts);
    IEval<float>            *impl = nullptr;
    try {
        switch (input_dim) {
        case 1:
            impl = make_eval_f32_dim1(output_dim, f, context, a, b, tol, o);
            break;
        case 2:
            impl = make_eval_f32_dim2(output_dim, f, context, a, b, tol, o);
            break;
        case 3:
            impl = make_eval_f32_dim3(output_dim, f, context, a, b, tol, o);
            break;
        default:
            set_last_error("treeweavef_fit: input_dim must be 1, 2, or 3");
            return nullptr;
        }
    } catch (const std::exception &e) {
        set_last_error(e.what());
        return nullptr;
    } catch (...) {
        set_last_error("treeweavef_fit: unknown exception during fit");
        return nullptr;
    }

    if (impl == nullptr) {
        set_last_error("treeweavef_fit: unsupported output_dim; "
                       "output_dim must be 1, 2, or 3");
        return nullptr;
    }
    return new treeweave_function{
        .impl = impl, .dtype = TREEWEAVE_F32, .input_dim = input_dim, .output_dim = output_dim};
}

} // extern "C"

/// Shared dtype guard for the eval shims. Returns the typed evaluator or
/// nullptr (after setting last_error) on a null handle or dtype mismatch.
namespace {
template <class T>
auto checked_impl(treeweave_t f, treeweave_dtype_t want) -> treeweave::capi::IEval<T> * {
    clear_error();
    if (f == nullptr) {
        treeweave::capi::set_last_error("treeweave eval: null handle");
        return nullptr;
    }
    if (f->dtype != want) {
        treeweave::capi::set_last_error(want == TREEWEAVE_F64
                                            ? "treeweave eval: dtype mismatch (called treeweave_* on an f32 handle)"
                                            : "treeweave eval: dtype mismatch (called treeweavef_* on an f64 handle)");
        return nullptr;
    }
    return static_cast<treeweave::capi::IEval<T> *>(f->impl);
}
} // namespace

extern "C" {

void treeweave_eval(treeweave_t f, const double *x, double *y) {
    if (const auto *impl = checked_impl<double>(f, TREEWEAVE_F64))
        impl->eval(x, y);
}
void treeweavef_eval(treeweave_t f, const float *x, float *y) {
    if (const auto *impl = checked_impl<float>(f, TREEWEAVE_F32))
        impl->eval(x, y);
}

void treeweave_batch(treeweave_t f, const double *x, double *res, size_t n) {
    if (const auto *impl = checked_impl<double>(f, TREEWEAVE_F64))
        impl->eval_multi(x, res, n);
}
void treeweavef_batch(treeweave_t f, const float *x, float *res, size_t n) {
    if (const auto *impl = checked_impl<float>(f, TREEWEAVE_F32))
        impl->eval_multi(x, res, n);
}

void treeweave_sorted(treeweave_t f, const double *x, double *res, size_t n) {
    if (const auto *impl = checked_impl<double>(f, TREEWEAVE_F64))
        impl->eval_sorted(x, res, n);
}
void treeweavef_sorted(treeweave_t f, const float *x, float *res, size_t n) {
    if (const auto *impl = checked_impl<float>(f, TREEWEAVE_F32))
        impl->eval_sorted(x, res, n);
}

void treeweave_transposed(treeweave_t f, const double *x, double *const *soa, size_t n) {
    if (const auto *impl = checked_impl<double>(f, TREEWEAVE_F64))
        impl->eval_multi_soa(x, soa, n);
}
void treeweavef_transposed(treeweave_t f, const float *x, float *const *soa, size_t n) {
    if (const auto *impl = checked_impl<float>(f, TREEWEAVE_F32))
        impl->eval_multi_soa(x, soa, n);
}

/// By-value scalar eval. Validate arity/scalar-output here (before delegating,
/// since treeweave_eval clears the error buffer), then forward to the pointer
/// API. `y` is pre-seeded with NaN so a null-handle or dtype-mismatch no-op in
/// treeweave_eval/treeweavef_eval surfaces as NaN, matching the OOD convention.
auto treeweave_eval_1d(treeweave_t f, double x0) -> double {
    constexpr double nan = std::numeric_limits<double>::quiet_NaN();
    if (f != nullptr && (f->input_dim != 1 || f->output_dim != 1)) {
        treeweave::capi::set_last_error("treeweave_eval_1d: requires a 1-D, scalar-output handle");
        return nan;
    }
    const double x[1] = {x0};
    double       y    = nan;
    treeweave_eval(f, x, &y);
    return y;
}
auto treeweave_eval_2d(treeweave_t f, double x0, double x1) -> double {
    constexpr double nan = std::numeric_limits<double>::quiet_NaN();
    if (f != nullptr && (f->input_dim != 2 || f->output_dim != 1)) {
        treeweave::capi::set_last_error("treeweave_eval_2d: requires a 2-D, scalar-output handle");
        return nan;
    }
    const double x[2] = {x0, x1};
    double       y    = nan;
    treeweave_eval(f, x, &y);
    return y;
}
auto treeweave_eval_3d(treeweave_t f, double x0, double x1, double x2) -> double {
    constexpr double nan = std::numeric_limits<double>::quiet_NaN();
    if (f != nullptr && (f->input_dim != 3 || f->output_dim != 1)) {
        treeweave::capi::set_last_error("treeweave_eval_3d: requires a 3-D, scalar-output handle");
        return nan;
    }
    const double x[3] = {x0, x1, x2};
    double       y    = nan;
    treeweave_eval(f, x, &y);
    return y;
}
auto treeweavef_eval_1d(treeweave_t f, float x0) -> float {
    constexpr float nan = std::numeric_limits<float>::quiet_NaN();
    if (f != nullptr && (f->input_dim != 1 || f->output_dim != 1)) {
        treeweave::capi::set_last_error("treeweavef_eval_1d: requires a 1-D, scalar-output handle");
        return nan;
    }
    const float x[1] = {x0};
    float       y    = nan;
    treeweavef_eval(f, x, &y);
    return y;
}
auto treeweavef_eval_2d(treeweave_t f, float x0, float x1) -> float {
    constexpr float nan = std::numeric_limits<float>::quiet_NaN();
    if (f != nullptr && (f->input_dim != 2 || f->output_dim != 1)) {
        treeweave::capi::set_last_error("treeweavef_eval_2d: requires a 2-D, scalar-output handle");
        return nan;
    }
    const float x[2] = {x0, x1};
    float       y    = nan;
    treeweavef_eval(f, x, &y);
    return y;
}
auto treeweavef_eval_3d(treeweave_t f, float x0, float x1, float x2) -> float {
    constexpr float nan = std::numeric_limits<float>::quiet_NaN();
    if (f != nullptr && (f->input_dim != 3 || f->output_dim != 1)) {
        treeweave::capi::set_last_error("treeweavef_eval_3d: requires a 3-D, scalar-output handle");
        return nan;
    }
    const float x[3] = {x0, x1, x2};
    float       y    = nan;
    treeweavef_eval(f, x, &y);
    return y;
}

auto treeweave_dtype(treeweave_t f) -> treeweave_dtype_t { return f->dtype; }
auto treeweave_input_dim(treeweave_t f) -> int { return f->input_dim; }
auto treeweave_output_dim(treeweave_t f) -> int { return f->output_dim; }

auto treeweave_memory_usage(treeweave_t f) -> size_t {
    if (f == nullptr)
        return 0;
    if (f->dtype == TREEWEAVE_F64)
        return static_cast<treeweave::capi::IEval<double> *>(f->impl)->memory_usage();
    return static_cast<treeweave::capi::IEval<float> *>(f->impl)->memory_usage();
}

void treeweave_print_stats(treeweave_t f) {
    if (f == nullptr)
        return;
    if (f->dtype == TREEWEAVE_F64)
        static_cast<treeweave::capi::IEval<double> *>(f->impl)->print_stats();
    else
        static_cast<treeweave::capi::IEval<float> *>(f->impl)->print_stats();
}

auto treeweave_free(treeweave_t f) -> treeweave_t {
    if (f == nullptr)
        return nullptr;
    if (f->dtype == TREEWEAVE_F64)
        delete static_cast<treeweave::capi::IEval<double> *>(f->impl);
    else
        delete static_cast<treeweave::capi::IEval<float> *>(f->impl);
    delete f;
    return nullptr;
}

auto treeweave_last_error(void) -> const char * { return error_buffer().c_str(); }

} // extern "C"
