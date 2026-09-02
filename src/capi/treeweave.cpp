/// src/capi/treeweave.cpp: the extern "C" surface declared in treeweave.h.
///
/// Holds the opaque handle definition, the thread-local last-error buffer, the
/// fit dispatch over (input_dim, output_dim) onto the per-shape factories
/// compiled in the CMake-generated TUs, and the dtype-checked eval /
/// introspection / lifetime shims. Each entry point is a one-line wrapper over
/// a `T`-templated body, so the f64 and f32 surfaces cannot drift. No C++
/// exception is allowed to escape any function here.
///
/// Degree is baked to detail::kDefaultDegree; these entry
/// points carry no degree argument and the opaque handle does not store one.

#include <array>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <limits>
#include <string>

#include <poet/core/dispatch.hpp>

#include <treeweave.h>
#include <treeweave_shapes.hpp> // generated: kMaxInputDim / kMaxOutputDim
#include <treeweave_version.h>

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
        // the prior message intact. No recovery is possible here, and nothing may
        // escape this noexcept function.
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

/// Cast `f->impl` to the correct typed IEval and invoke `fn` on it.
namespace {
template <class Fn>
auto with_typed_impl(treeweave_t f, Fn &&fn) -> decltype(auto) {
    if (f->dtype == TREEWEAVE_F64)
        return std::forward<Fn>(fn)(static_cast<treeweave::capi::IEval<double> *>(f->impl));
    return std::forward<Fn>(fn)(static_cast<treeweave::capi::IEval<float> *>(f->impl));
}
} // namespace

namespace {

/// Per-dtype constants for the templated bodies below: the handle's dtype tag
/// and the entry-point prefix that error messages carry.
template <class T>
struct Prec;
template <>
struct Prec<double> {
    static constexpr treeweave_dtype_t dtype  = TREEWEAVE_F64;
    static constexpr const char       *prefix = "treeweave";
};
template <>
struct Prec<float> {
    static constexpr treeweave_dtype_t dtype  = TREEWEAVE_F32;
    static constexpr const char       *prefix = "treeweavef";
};

/// Compose "<prefix>_fit: <text>" into the last-error buffer. Formats into a
/// stack buffer, because an allocation here could throw out of the C ABI.
void set_fit_error(const char *prefix, const char *text) noexcept {
    std::array<char, 128> msg{};
    (void)std::snprintf(msg.data(), msg.size(), "%s_fit: %s", prefix, text);
    treeweave::capi::set_last_error(msg.data());
}

/// Route a runtime (input_dim, output_dim) pair onto the matching
/// `make_eval_one` instantiation; nullptr when either falls outside the shape
/// set. The two dispatch params form the cartesian product, so the table is
/// the generated shape set itself.
template <class T>
auto make_eval(int input_dim, int output_dim, treeweave::capi::c_func_t<T> f, void *data, const T *a, const T *b,
               double tol, const treeweave::options &opts) -> treeweave::capi::IEval<T> * {
    return poet::dispatch(
        [&]<int In, int Out>() -> treeweave::capi::IEval<T> * {
            return treeweave::capi::make_eval_one<T, static_cast<std::size_t>(In), static_cast<std::size_t>(Out)>(
                f, data, a, b, tol, opts);
        },
        poet::dispatch_param<poet::inclusive_range<1, treeweave::capi::kMaxInputDim>>{input_dim},
        poet::dispatch_param<poet::inclusive_range<1, treeweave::capi::kMaxOutputDim>>{output_dim});
}

/// Same, for the two range messages that name their own bound.
void set_range_error(const char *prefix, const char *field, int bound) noexcept {
    std::array<char, 128> msg{};
    (void)std::snprintf(msg.data(), msg.size(), "%s_fit: %s must be in [1, %d]", prefix, field, bound);
    treeweave::capi::set_last_error(msg.data());
}

/// Body of treeweave_fit / treeweavef_fit. Both dims are range-checked here so
/// the error names the one that is wrong; the dispatch itself only routes.
template <class T>
auto fit_impl(treeweave::capi::c_func_t<T> f, int input_dim, int output_dim, const T *a, const T *b, double tol,
              void *context, const treeweave_opts *opts) -> treeweave_t {
    using namespace treeweave::capi;
    clear_error();
    if (f == nullptr || a == nullptr || b == nullptr) {
        set_fit_error(Prec<T>::prefix, "null callback or domain pointer");
        return nullptr;
    }
    if (input_dim < 1 || input_dim > kMaxInputDim) {
        set_range_error(Prec<T>::prefix, "input_dim", kMaxInputDim);
        return nullptr;
    }
    if (output_dim < 1 || output_dim > kMaxOutputDim) {
        set_range_error(Prec<T>::prefix, "output_dim", kMaxOutputDim);
        return nullptr;
    }

    try {
        IEval<T> *impl = make_eval<T>(input_dim, output_dim, f, context, a, b, tol, to_options(opts));
        if (impl == nullptr) {
            set_fit_error(Prec<T>::prefix, "no factory for this shape");
            return nullptr;
        }
        return new treeweave_function{
            .impl = impl, .dtype = Prec<T>::dtype, .input_dim = input_dim, .output_dim = output_dim};
    } catch (const std::exception &e) {
        set_last_error(e.what());
        return nullptr;
    } catch (...) {
        set_fit_error(Prec<T>::prefix, "unknown exception during fit");
        return nullptr;
    }
}

} // namespace

extern "C" {

void treeweave_default_opts(treeweave_opts *opts) {
    if (opts == nullptr)
        return;
    *opts = {
        .tol_kind               = TREEWEAVE_RELATIVE_MAX,
        .max_depth              = 50,
        .max_memory_mib         = -1, // auto: dimension-scaled (4/8/16 MiB)
        .allow_max_depth_leaves = 0,
        .min_uniform_depth      = 0,
    };
}

auto treeweave_fit(treeweave_func_t f, int input_dim, int output_dim, const double *a, const double *b, double tol,
                   void *context, const treeweave_opts *opts) -> treeweave_t {
    return fit_impl<double>(f, input_dim, output_dim, a, b, tol, context, opts);
}

auto treeweavef_fit(treeweavef_func_t f, int input_dim, int output_dim, const float *a, const float *b, double tol,
                    void *context, const treeweave_opts *opts) -> treeweave_t {
    return fit_impl<float>(f, input_dim, output_dim, a, b, tol, context, opts);
}

} // extern "C"

/// Shared dtype guard for the eval shims. Returns the typed evaluator or
/// nullptr (after setting last_error) on a null handle or dtype mismatch.
namespace {
template <class T>
auto checked_impl(treeweave_t f) -> treeweave::capi::IEval<T> * {
    clear_error();
    if (f == nullptr) {
        treeweave::capi::set_last_error("treeweave eval: null handle");
        return nullptr;
    }
    if (f->dtype != Prec<T>::dtype) {
        treeweave::capi::set_last_error(Prec<T>::dtype == TREEWEAVE_F64
                                            ? "treeweave eval: dtype mismatch (called treeweave_* on an f32 handle)"
                                            : "treeweave eval: dtype mismatch (called treeweavef_* on an f64 handle)");
        return nullptr;
    }
    return static_cast<treeweave::capi::IEval<T> *>(f->impl);
}

/// Body of the by-value scalar eval shims: arity guard, then the pointer eval.
/// `y` starts as NaN, so a rejected call returns NaN without touching the
/// evaluator.
template <class T, int N>
auto eval_by_value(treeweave_t f, std::array<T, static_cast<std::size_t>(N)> x) -> T {
    constexpr T nan = std::numeric_limits<T>::quiet_NaN();
    if (f != nullptr && (f->input_dim != N || f->output_dim != 1)) {
        std::array<char, 128> msg{};
        (void)std::snprintf(msg.data(), msg.size(), "%s_eval_%dd: requires a %d-D, scalar-output handle",
                            Prec<T>::prefix, N, N);
        treeweave::capi::set_last_error(msg.data());
        return nan;
    }
    T y = nan;
    if (const auto *impl = checked_impl<T>(f))
        impl->eval(x.data(), &y);
    return y;
}
} // namespace

extern "C" {

void treeweave_eval(treeweave_t f, const double *x, double *y) {
    if (const auto *impl = checked_impl<double>(f))
        impl->eval(x, y);
}
void treeweavef_eval(treeweave_t f, const float *x, float *y) {
    if (const auto *impl = checked_impl<float>(f))
        impl->eval(x, y);
}

void treeweave_batch(treeweave_t f, const double *x, double *res, size_t n) {
    if (const auto *impl = checked_impl<double>(f))
        impl->eval_multi(x, res, n);
}
void treeweavef_batch(treeweave_t f, const float *x, float *res, size_t n) {
    if (const auto *impl = checked_impl<float>(f))
        impl->eval_multi(x, res, n);
}

void treeweave_sorted(treeweave_t f, const double *x, double *res, size_t n) {
    if (const auto *impl = checked_impl<double>(f))
        impl->eval_sorted(x, res, n);
}
void treeweavef_sorted(treeweave_t f, const float *x, float *res, size_t n) {
    if (const auto *impl = checked_impl<float>(f))
        impl->eval_sorted(x, res, n);
}

void treeweave_transposed(treeweave_t f, const double *x, double *const *soa, size_t n) {
    if (const auto *impl = checked_impl<double>(f))
        impl->eval_multi_soa(x, soa, n);
}
void treeweavef_transposed(treeweave_t f, const float *x, float *const *soa, size_t n) {
    if (const auto *impl = checked_impl<float>(f))
        impl->eval_multi_soa(x, soa, n);
}

auto treeweave_eval_1d(treeweave_t f, double x0) -> double { return eval_by_value<double, 1>(f, {x0}); }
auto treeweave_eval_2d(treeweave_t f, double x0, double x1) -> double { return eval_by_value<double, 2>(f, {x0, x1}); }
auto treeweave_eval_3d(treeweave_t f, double x0, double x1, double x2) -> double {
    return eval_by_value<double, 3>(f, {x0, x1, x2});
}
auto treeweavef_eval_1d(treeweave_t f, float x0) -> float { return eval_by_value<float, 1>(f, {x0}); }
auto treeweavef_eval_2d(treeweave_t f, float x0, float x1) -> float { return eval_by_value<float, 2>(f, {x0, x1}); }
auto treeweavef_eval_3d(treeweave_t f, float x0, float x1, float x2) -> float {
    return eval_by_value<float, 3>(f, {x0, x1, x2});
}

auto treeweave_dtype(treeweave_t f) -> treeweave_dtype_t {
    if (f == nullptr) {
        treeweave::capi::set_last_error("treeweave_dtype: null handle");
        return TREEWEAVE_F64; // zero enumerator; sentinel for "unknown"
    }
    return f->dtype;
}
auto treeweave_input_dim(treeweave_t f) -> int {
    if (f == nullptr) {
        treeweave::capi::set_last_error("treeweave_input_dim: null handle");
        return 0;
    }
    return f->input_dim;
}
auto treeweave_output_dim(treeweave_t f) -> int {
    if (f == nullptr) {
        treeweave::capi::set_last_error("treeweave_output_dim: null handle");
        return 0;
    }
    return f->output_dim;
}

auto treeweave_memory_usage(treeweave_t f) -> size_t {
    if (f == nullptr)
        return 0;
    return with_typed_impl(f, [](auto *p) -> std::size_t { return p->memory_usage(); });
}

void treeweave_print_stats(treeweave_t f) {
    if (f == nullptr)
        return;
    with_typed_impl(f, [](auto *p) -> void { p->print_stats(); });
}

auto treeweave_free(treeweave_t f) -> treeweave_t {
    if (f == nullptr)
        return nullptr;
    with_typed_impl(f, [](auto *p) -> void { delete p; });
    delete f;
    return nullptr;
}

auto treeweave_last_error(void) -> const char * { return error_buffer().c_str(); }

auto treeweave_version(void) -> int { return TREEWEAVE_VERSION; }
auto treeweave_version_string(void) -> const char * { return TREEWEAVE_VERSION_STRING; }

auto treeweave_active_arch(void) -> const char * { return treeweave::capi::active_arch(); }
auto treeweave_arch_available(const char *name) -> int { return treeweave::capi::arch_available(name); }

} // extern "C"
