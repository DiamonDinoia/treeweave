/* treeweave.h — C ABI for the treeweave piecewise-polynomial approximator.
 *
 * This surface mirrors the C++ `treeweave::fit(f, a, b, tol, options)` API
 * (see include/treeweave/treeweave.hpp): the domain is given as its lower/upper
 * corners `a`/`b` and the fit knobs live in `treeweave_opts`. It is *not* the
 * legacy `treeweave_input_t` / `treeweave_init` shape.
 *
 * Precision lives in the library prefix, FINUFFT/FFTW style: the `treeweave_*`
 * entry points operate on `double`, the `treeweavef_*` twins on `float`. Only
 * the value-carrying functions (fit + eval family) come in twin form; the
 * introspection and lifetime functions are dtype-independent and operate on
 * the opaque handle, so they stay single (`treeweave_` prefix, no `treeweavef_`).
 *
 * Degree is baked: the leaf polynomial degree is fixed to 7 for all
 * (arch, dtype, input_dim) cells (see arch_degree_table.hpp). It is not a
 * user parameter.
 *
 * Eval-only: a fitted function can be evaluated (single, AoS batch,
 * sorted-1D, SoA/transposed) but not serialized. There is one opaque handle
 * type carrying a runtime dtype tag (f64 or f32).
 *
 * Thread safety: once `treeweave_fit`/`treeweavef_fit` returns a handle, its eval
 * functions are safe to call concurrently from multiple threads provided
 * each call writes a disjoint output slice (the underlying C++ Function
 * allocates per-call scratch). `treeweave_last_error()` is thread-local.
 *
 * No C++ exception ever crosses this boundary: fit failures return NULL
 * and set `treeweave_last_error()`.
 */
#ifndef TREEWEAVE_H
#define TREEWEAVE_H

#include <stddef.h> /* size_t */

/* Library version. The TREEWEAVE_VERSION_* / TREEWEAVE_VERSION_STRING macros and
 * the TREEWEAVE_VERSION_AT_LEAST(maj,min,pat) compile-time guard live in the
 * generated treeweave_version.h, whose single source of truth is the VERSION
 * file at the repo root. The runtime getters below report the version of the
 * actually-linked library (which can differ from these macros when a shared
 * libtreeweave_c is swapped in). The matching C++ constants are
 * treeweave::version_* in <treeweave/treeweave.hpp>. */
#include <treeweave_version.h>

/* Symbol visibility. libtreeweave_c is built with hidden default visibility
 * (-fvisibility=hidden on GCC/Clang), so only the TREEWEAVE_EXPORT-tagged public
 * surface below is exported; all internal machinery stays local. TREEWEAVE_C_BUILD
 * is defined by the build only while compiling the library's own objects.
 * Consumers of the shared library leave it unset (they import); consumers of
 * the static archive define TREEWEAVE_STATIC (no decoration). */
#if defined(TREEWEAVE_STATIC)
#define TREEWEAVE_EXPORT
#elif defined(_WIN32) || defined(__CYGWIN__)
#if defined(TREEWEAVE_C_BUILD)
#define TREEWEAVE_EXPORT __declspec(dllexport)
#else
#define TREEWEAVE_EXPORT __declspec(dllimport)
#endif
#elif defined(TREEWEAVE_C_BUILD) && (defined(__GNUC__) || defined(__clang__))
#define TREEWEAVE_EXPORT __attribute__((visibility("default")))
#else
#define TREEWEAVE_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Tolerance interpretation — numeric values match treeweave::TolKind. */
typedef enum {
    TREEWEAVE_RELATIVE_TAIL = 0, /* relative tail-coefficient estimate (1D) */
    TREEWEAVE_ABSOLUTE_TAIL = 1, /* absolute tail-coefficient estimate (1D) */
    TREEWEAVE_RELATIVE_MAX  = 2, /* sample-based, max-abs relative error    */
    TREEWEAVE_ABSOLUTE_MAX  = 3, /* sample-based, max-abs absolute error    */
    TREEWEAVE_RELATIVE_L2   = 4, /* sample-based, L2 relative error         */
    TREEWEAVE_ABSOLUTE_L2   = 5  /* sample-based, L2 absolute error         */
} treeweave_tol_kind_t;

typedef enum { TREEWEAVE_F64 = 0, TREEWEAVE_F32 = 1 } treeweave_dtype_t;

/* Fit knobs — mirrors treeweave::options. `int` is used for the bool field so
 * the struct is plain C. */
typedef struct {
    treeweave_tol_kind_t tol_kind;
    int                  max_depth;
    /* Leaf-storage cap during the fit, in MiB. Tri-state: <0 (the default in
     * treeweave_default_opts) auto-selects a small, dimension-scaled budget
     * (4/8/16 MiB for input_dim 1/2/3); 0 disables the cap; >0 is an explicit
     * cap. Crossing it makes the fit return NULL with treeweave_last_error()
     * naming the offending panel. */
    int max_memory_mib;
    int allow_max_depth_leaves;
    int min_uniform_depth;
} treeweave_opts;

/* Defaults matching treeweave::options{}. Pass NULL for `opts` to use these. */
extern TREEWEAVE_EXPORT const treeweave_opts treeweave_default_opts;

/* Opaque, dtype-tagged handle. */
typedef struct treeweave_function *treeweave_t;

/* User callback: read `input_dim` coordinates from `x`, write `output_dim`
 * results to `y`. `context` is the opaque pointer passed to the fit (the C
 * stand-in for a C++ closure's captures); treeweave forwards it untouched. */
typedef void (*treeweave_func_t)(const double *x, double *y, void *context);
typedef void (*treeweavef_func_t)(const float *x, float *y, void *context);

/**
 * @brief Fit a function over a box domain and return an evaluable handle.
 *
 * @param f          User callback evaluating the target (see @ref treeweave_func_t).
 * @param input_dim  Domain dimension; supported values are 1, 2, 3.
 * @param output_dim Number of output components; supported values are 1, 2, 3.
 * @param a          Domain lower corner, @p input_dim elements.
 * @param b          Domain upper corner, @p input_dim elements.
 * @param tol        Target accuracy, interpreted per @c opts->tol_kind.
 * @param context    Opaque pointer forwarded to every @p f invocation (may be NULL).
 * @param opts       Fit knobs, or NULL for @ref treeweave_default_opts.
 * @return A handle on success, or NULL with treeweave_last_error() set when the
 *         (dtype, input_dim, output_dim) tuple is unsupported, the arguments
 *         are invalid, or the fit throws (MaxDepthExceeded / MemoryBudgetExceeded).
 * @note The leaf polynomial degree is fixed at 7 and is not a parameter.
 */
TREEWEAVE_EXPORT treeweave_t treeweave_fit(treeweave_func_t f, int input_dim, int output_dim, const double *a,
                                           const double *b, double tol, void *context, const treeweave_opts *opts);
/** @brief Float32 twin of treeweave_fit(). @see treeweave_fit */
TREEWEAVE_EXPORT treeweave_t treeweavef_fit(treeweavef_func_t f, int input_dim, int output_dim, const float *a,
                                            const float *b, double tol, void *context, const treeweave_opts *opts);

/* ---- eval ------------------------------------------------------------- *
 * Each typed eval validates that the handle's dtype matches; on mismatch it
 * sets treeweave_last_error() and returns without writing. Out-of-domain
 * points yield NaN. The 1-D scalar case uses the same pointer form:
 *   double x = 3.0, y; treeweave_eval(fn, &x, &y);
 *
 * A NULL handle is tolerated (sets treeweave_last_error() and returns without
 * writing), but the coordinate/result pointers (`x`, `y`, `res`, `soa`) must
 * not be NULL when the handle is non-NULL. */

/**
 * @brief Evaluate a single point.
 * @param f Handle from treeweave_fit() (NULL-safe: no-op).
 * @param x Input coordinates, @c input_dim elements (must not be NULL).
 * @param y Output buffer, @c output_dim elements (must not be NULL). Out-of-domain inputs yield NaN.
 */
TREEWEAVE_EXPORT void treeweave_eval(treeweave_t f, const double *x, double *y);
/** @brief Float32 twin of treeweave_eval(). @see treeweave_eval */
TREEWEAVE_EXPORT void treeweavef_eval(treeweave_t f, const float *x, float *y);

/**
 * @brief Evaluate @p n points, array-of-structs layout.
 * @param f   Handle from treeweave_fit() (NULL-safe: no-op).
 * @param x   @p n * @c input_dim packed coordinates.
 * @param res @p n * @c output_dim packed results.
 * @param n   Number of points.
 */
TREEWEAVE_EXPORT void treeweave_batch(treeweave_t f, const double *x, double *res, size_t n);
/** @brief Float32 twin of treeweave_batch(). @see treeweave_batch */
TREEWEAVE_EXPORT void treeweavef_batch(treeweave_t f, const float *x, float *res, size_t n);

/**
 * @brief Evaluate a sorted 1D batch (@c input_dim must be 1).
 * @param f   Handle from treeweave_fit() (NULL-safe: no-op).
 * @param x   @p n inputs; the caller promises @c x[i] <= x[i+1].
 * @param res @p n * @c output_dim packed results.
 * @param n   Number of points.
 */
TREEWEAVE_EXPORT void treeweave_sorted(treeweave_t f, const double *x, double *res, size_t n);
/** @brief Float32 twin of treeweave_sorted(). @see treeweave_sorted */
TREEWEAVE_EXPORT void treeweavef_sorted(treeweave_t f, const float *x, float *res, size_t n);

/**
 * @brief Evaluate @p n points, struct-of-arrays (transposed) output (@c output_dim must be > 1).
 * @param f   Handle from treeweave_fit() (NULL-safe: no-op).
 * @param x   @p n * @c input_dim packed coordinates.
 * @param soa @c output_dim buffers of @p n elements each; @c soa[d] receives component d.
 * @param n   Number of points.
 */
TREEWEAVE_EXPORT void treeweave_transposed(treeweave_t f, const double *x, double *const *soa, size_t n);
/** @brief Float32 twin of treeweave_transposed(). @see treeweave_transposed */
TREEWEAVE_EXPORT void treeweavef_transposed(treeweave_t f, const float *x, float *const *soa, size_t n);

/* ---- by-value scalar eval (C convenience) ---------------------------- *
 * Pass coordinates by value, return the scalar result — no output pointer.
 * These are thin wrappers over treeweave_eval/treeweavef_eval for the common
 * `y = f(x)` case. The `_1d`/`_2d`/`_3d` suffix is the call arity (== the
 * handle's input_dim). They require a scalar-output handle (output_dim == 1);
 * vector output keeps the pointer API. A NULL handle, a dtype mismatch, or a
 * dimension mismatch (the handle's input_dim/output_dim does not match this
 * arity / scalar-output) sets treeweave_last_error() and returns NaN.
 *
 * Closed upper endpoint: evaluating exactly at the domain's upper corner `b`
 * returns the boundary value (the last leaf's polynomial), not NaN; `x < a`
 * (lower side) still returns NaN. */

/** @brief Evaluate a 1-D, scalar-output handle at @p x0, returning f(x0). */
TREEWEAVE_EXPORT double treeweave_eval_1d(treeweave_t f, double x0);
/** @brief Evaluate a 2-D, scalar-output handle at (@p x0, @p x1). */
TREEWEAVE_EXPORT double treeweave_eval_2d(treeweave_t f, double x0, double x1);
/** @brief Evaluate a 3-D, scalar-output handle at (@p x0, @p x1, @p x2). */
TREEWEAVE_EXPORT double treeweave_eval_3d(treeweave_t f, double x0, double x1, double x2);
/** @brief Float32 twin of treeweave_eval_1d(). @see treeweave_eval_1d */
TREEWEAVE_EXPORT float treeweavef_eval_1d(treeweave_t f, float x0);
/** @brief Float32 twin of treeweave_eval_2d(). @see treeweave_eval_2d */
TREEWEAVE_EXPORT float treeweavef_eval_2d(treeweave_t f, float x0, float x1);
/** @brief Float32 twin of treeweave_eval_3d(). @see treeweave_eval_3d */
TREEWEAVE_EXPORT float treeweavef_eval_3d(treeweave_t f, float x0, float x1, float x2);

/* ---- introspection / lifetime (dtype-independent) -------------------- */

/** @brief Value type carried by the handle.
 *  NULL-safe: on a NULL handle sets treeweave_last_error() and returns TREEWEAVE_F64 (0). */
TREEWEAVE_EXPORT treeweave_dtype_t treeweave_dtype(treeweave_t f);
/** @brief Domain dimension the handle was fitted with.
 *  NULL-safe: on a NULL handle sets treeweave_last_error() and returns 0. */
TREEWEAVE_EXPORT int treeweave_input_dim(treeweave_t f);
/** @brief Number of output components the handle produces.
 *  NULL-safe: on a NULL handle sets treeweave_last_error() and returns 0. */
TREEWEAVE_EXPORT int treeweave_output_dim(treeweave_t f);
/** @brief Approximate bytes held by the approximation tree. NULL-safe: returns 0. */
TREEWEAVE_EXPORT size_t treeweave_memory_usage(treeweave_t f);
/** @brief Print internal fit/eval statistics to stdout. NULL-safe: no-op. */
TREEWEAVE_EXPORT void treeweave_print_stats(treeweave_t f);

/**
 * @brief Free the handle and return NULL.
 * @param f Handle to free (NULL-safe).
 * @return NULL, so callers can write @c h = treeweave_free(h);.
 */
TREEWEAVE_EXPORT treeweave_t treeweave_free(treeweave_t f);

/**
 * @brief Thread-local description of the most recent error on this thread.
 * @return The message, or an empty string if none. Valid until the next
 *         treeweave call on the same thread.
 */
TREEWEAVE_EXPORT const char *treeweave_last_error(void);

/* ---- version (runtime) ----------------------------------------------- *
 * The version of the linked library, for the shared-library case where it may
 * differ from the TREEWEAVE_VERSION / TREEWEAVE_VERSION_STRING macros the caller
 * compiled against. A consumer can assert agreement at startup, e.g.
 *   assert(treeweave_version() == TREEWEAVE_VERSION); */

/** @brief Linked library version as MAJOR*10000 + MINOR*100 + PATCH. */
TREEWEAVE_EXPORT int treeweave_version(void);
/** @brief Linked library version as a dotted string, e.g. "1.2.3". */
TREEWEAVE_EXPORT const char *treeweave_version_string(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* TREEWEAVE_H */
