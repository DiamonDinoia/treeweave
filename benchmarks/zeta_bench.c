/* Zeta bench (C ABI): treeweave vs adaptive-stop Riemann-zeta (<=160 terms, rel 1e-10); times
 * single/batch/sorted modes. TREEWEAVE_BENCH_YAML=path emits YAML. */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <treeweave.h>

#ifdef TREEWEAVE_BENCH_BASELINES
#include <baobzi.h>
#include <gsl/gsl_chebyshev.h>
#include <gsl/gsl_spline.h>
#endif

/* Fair baseline: sum k^-s until a term is below ZETA_EPS relative to the running
 * total, capped at ZETA_MAX_TERMS: a competent zeta stops early. */
#define ZETA_EPS 1e-10
#define ZETA_MAX_TERMS 160

/* ζ(s) ≈ Σ_k k^-s (early stop). Both the fit callback and the native baseline. */
static double zeta_partial(double s) {
    double acc = 0.0;
    for (long k = 1; k <= ZETA_MAX_TERMS; ++k) {
        const double term = pow((double)k, -s);
        acc += term;
        if (term < ZETA_EPS * acc)
            break;
    }
    return acc;
}

/* Fit target: all ZETA_MAX_TERMS terms. The early stop above makes zeta_partial
 * jump by up to ZETA_EPS relative where the term count changes, and a global
 * Chebyshev series then needs degree 177 instead of 26 on [2, 10]. Every
 * interpolator, treeweave included, fits this smooth sum; the native rate uses
 * zeta_partial. */
static double zeta_smooth(double s) {
    double acc = 0.0;
    for (long k = 1; k <= ZETA_MAX_TERMS; ++k)
        acc += pow((double)k, -s);
    return acc;
}

static void kernel(const double *x, double *y, void *context) {
    (void)context;
    y[0] = zeta_smooth(x[0]);
}

/* Seconds from an arbitrary epoch. CLOCK_MONOTONIC where available (POSIX),
 * else C11 timespec_get() (MSVC has no clock_gettime). */
static double now_s(void) {
    struct timespec ts;
#if defined(CLOCK_MONOTONIC)
    clock_gettime(CLOCK_MONOTONIC, &ts);
#else
    timespec_get(&ts, TIME_UTC);
#endif
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static int cmp_double(const void *pa, const void *pb) {
    const double a = *(const double *)pa;
    const double b = *(const double *)pb;
    return (a > b) - (a < b);
}

/* One eval-mode YAML block. %.17e always carries a '.', so YAML 1.1 reads a float. */
static void emit_block(FILE *y, const char *name, double tw, double nat) {
    fprintf(y, "%s:\n", name);
    fprintf(y, "  treeweave_mevals_s: %.17e\n", tw);
    fprintf(y, "  native_mevals_s: %.17e\n", nat);
    fprintf(y, "  speedup: %.17e\n", tw / nat);
}

/* One point through a library's scalar call. */
typedef double (*eval_1d_t)(const void *obj, double x);

static double tw_eval_1d(const void *obj, double x) { return treeweave_eval_1d((treeweave_t)obj, x); }

/* err[0] = max|p - f| / max|f|, treeweave's default RelativeMax criterion, which
 * every baseline is grown against; err[1] = max |p - f| / |f|, the pointwise
 * error. Both are reported for every method, treeweave included. */
static void rel_errors(eval_1d_t f, const void *obj, const double *xs, const double *ref, size_t n, double err[2]) {
    double max_abs = 0.0, max_f = 0.0, max_rel = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const double e = fabs(f(obj, xs[i]) - ref[i]);
        max_abs        = e > max_abs ? e : max_abs;
        max_f          = fabs(ref[i]) > max_f ? fabs(ref[i]) : max_f;
        max_rel        = e / fabs(ref[i]) > max_rel ? e / fabs(ref[i]) : max_rel;
    }
    err[0] = max_abs / max_f;
    err[1] = max_rel;
}

#ifdef TREEWEAVE_BENCH_BASELINES
/* A native interpolator grown until it meets the tolerance on the check sample,
 * then timed with the same protocol as treeweave. Every rate is Mevals/s. */
typedef struct {
    const char *name;
    char        size[64];
    double      relmax, max_rel;
    double      single, multi, sorted;
} baseline_t;

static double gsl_cheb_eval_1d(const void *obj, double x) { return gsl_cheb_eval((const gsl_cheb_series *)obj, x); }

/* A GSL spline with its lookup accelerator, which caches the last interval and so
 * favours sorted queries, as GSL intends. */
typedef struct {
    gsl_spline       *spline;
    gsl_interp_accel *acc;
} gsl_spline_obj;

static double gsl_spline_eval_1d(const void *obj, double x) {
    const gsl_spline_obj *o = (const gsl_spline_obj *)obj;
    return gsl_spline_eval(o->spline, x, o->acc);
}

static double baobzi_eval_1d(const void *obj, double x) {
    double y;
    baobzi_eval((const baobzi_t)obj, &x, &y);
    return y;
}

static double gsl_target(double x, void *params) {
    (void)params;
    return zeta_smooth(x);
}

static void baobzi_target(const double *x, double *y, const void *data) {
    (void)data;
    y[0] = zeta_smooth(x[0]);
}

/* Mevals/s of the scalar call over pts[0..n). */
static double scalar_rate(eval_1d_t f, const void *obj, const double *pts, size_t n) {
    volatile double sink = 0.0;
    for (size_t i = 0; i < n; ++i)
        sink = sink + f(obj, pts[i]); /* warm-up (untimed) */
    const double t0 = now_s();
    for (size_t i = 0; i < n; ++i)
        sink = sink + f(obj, pts[i]);
    return (double)n / ((now_s() - t0) * 1e6);
}

/* Mevals/s of baobzi's batch call over pts[0..n). */
static double baobzi_multi_rate(baobzi_t f, const double *pts, double *out, size_t n) {
    baobzi_eval_multi(f, pts, out, (int)n); /* warm-up (untimed) */
    const double t0 = now_s();
    baobzi_eval_multi(f, pts, out, (int)n);
    return (double)n / ((now_s() - t0) * 1e6);
}
#endif

int main(void) {
    const double a   = 2.0;
    const double b   = 10.0;
    const double tol = 1e-10;

    /* NULL opts -> defaults (tol_kind RELATIVE_MAX). */
    treeweave_t fn = treeweave_fit(kernel, 1, 1, &a, &b, tol, /*context=*/NULL, /*opts=*/NULL);
    if (fn == NULL) {
        fprintf(stderr, "treeweave_fit failed: %s\n", treeweave_last_error());
        return EXIT_FAILURE;
    }

    const size_t n         = 1000000; /* batch / sorted points */
    const size_t n_scalar  = 100000;  /* scalar-API points */
    const size_t n_native  = 256;     /* brute-force sample (<=160 pows each) */
    double      *xs        = (double *)malloc(n * sizeof(double));
    double      *out       = (double *)malloc(n * sizeof(double));
    double      *xs_sorted = (double *)malloc(n * sizeof(double));
    if (xs == NULL || out == NULL || xs_sorted == NULL) {
        fprintf(stderr, "alloc failed\n");
        free(xs);
        free(out);
        free(xs_sorted);
        treeweave_free(fn);
        return EXIT_FAILURE;
    }

    srand(7);
    for (size_t i = 0; i < n; ++i)
        xs[i] = a + (b - a) * ((double)rand() / (double)RAND_MAX);

    /* Accuracy of every method, treeweave included, on the same sample. */
    enum { n_check = 4096 };
    static double ref_check[n_check];
    for (size_t i = 0; i < n_check; ++i)
        ref_check[i] = zeta_smooth(xs[i]);
    double tw_err[2];
    rel_errors(tw_eval_1d, fn, xs, ref_check, n_check, tw_err);
    const double max_rel = tw_err[1];

    volatile double sink = 0.0; /* anti-DCE sink for every timed loop */
    double          t0, t1;

    for (size_t i = 0; i < n_native; ++i)
        sink = sink + zeta_partial(xs[i]); /* warm-up (untimed) */
    t0 = now_s();
    for (size_t i = 0; i < n_native; ++i)
        sink = sink + zeta_partial(xs[i]);
    t1                    = now_s();
    const double nat_s    = t1 - t0;
    const double nat_rate = (double)n_native / (nat_s * 1e6); /* Mevals/s, all modes */

    for (size_t i = 0; i < n_scalar; ++i)
        sink = sink + treeweave_eval_1d(fn, xs[i]); /* warm-up (untimed) */
    t0 = now_s();
    for (size_t i = 0; i < n_scalar; ++i)
        sink = sink + treeweave_eval_1d(fn, xs[i]);
    t1                       = now_s();
    const double tw_single_s = t1 - t0;

    treeweave_batch(fn, xs, out, n); /* warm-up (untimed) */
    sink = out[0];
    t0   = now_s();
    treeweave_batch(fn, xs, out, n);
    t1                      = now_s();
    const double tw_multi_s = t1 - t0;
    sink                    = out[0];

    memcpy(xs_sorted, xs, n * sizeof(double));
    qsort(xs_sorted, n, sizeof(double), cmp_double); /* untimed */
    treeweave_sorted(fn, xs_sorted, out, n);         /* warm-up (untimed) */
    sink = out[0];
    t0   = now_s();
    treeweave_sorted(fn, xs_sorted, out, n);
    t1                       = now_s();
    const double tw_sorted_s = t1 - t0;
    sink                     = out[0];

#ifdef TREEWEAVE_BENCH_BASELINES
    baseline_t baselines[4];
    size_t     n_baselines = 0;
    {
        /* Every baseline must meet tol on the check sample before it is timed. */
        double err[2];

        /* GSL gsl_cheb: Chebyshev series from order + 1 Chebyshev nodes. Grow
         * the order by one until it meets tol: the smallest series that does. */
        gsl_function gsl_f = {gsl_target, NULL};
        for (size_t order = 2;; ++order) {
            gsl_cheb_series *cs = gsl_cheb_alloc(order);
            gsl_cheb_init(cs, &gsl_f, a, b);
            rel_errors(gsl_cheb_eval_1d, cs, xs, ref_check, n_check, err);
            if (err[0] <= tol) {
                baseline_t *bl = &baselines[n_baselines++];
                bl->name       = "GSL gsl_cheb";
                snprintf(bl->size, sizeof bl->size, "degree %zu", order);
                bl->relmax  = err[0];
                bl->max_rel = err[1];
                bl->single  = scalar_rate(gsl_cheb_eval_1d, cs, xs, n_scalar);
                bl->multi   = scalar_rate(gsl_cheb_eval_1d, cs, xs, n);
                bl->sorted  = scalar_rate(gsl_cheb_eval_1d, cs, xs_sorted, n);
                gsl_cheb_free(cs);
                break;
            }
            gsl_cheb_free(cs);
            if (order == 1000) {
                fprintf(stderr, "GSL gsl_cheb never met tol %g\n", tol);
                return EXIT_FAILURE;
            }
        }

        /* GSL splines on uniform knots: double the knot count from 16 until the
         * spline meets tol. Linear interpolation is the lookup-table method. */
        const struct {
            const char            *name;
            const gsl_interp_type *type;
        } splines[] = {{"GSL cubic spline", gsl_interp_cspline}, {"GSL linear", gsl_interp_linear}};
        for (size_t k = 0; k < sizeof splines / sizeof splines[0]; ++k) {
            for (size_t knots = 16;; knots *= 2) {
                double *kx = (double *)malloc(knots * sizeof(double));
                double *ky = (double *)malloc(knots * sizeof(double));
                if (kx == NULL || ky == NULL) {
                    fprintf(stderr, "alloc failed\n");
                    return EXIT_FAILURE;
                }
                for (size_t i = 0; i < knots; ++i) {
                    kx[i] = a + (b - a) * (double)i / (double)(knots - 1);
                    ky[i] = zeta_smooth(kx[i]);
                }
                gsl_spline_obj sp = {gsl_spline_alloc(splines[k].type, knots), gsl_interp_accel_alloc()};
                gsl_spline_init(sp.spline, kx, ky, knots);
                free(kx);
                free(ky);
                rel_errors(gsl_spline_eval_1d, &sp, xs, ref_check, n_check, err);
                const int done = err[0] <= tol;
                if (done) {
                    baseline_t *bl = &baselines[n_baselines++];
                    bl->name       = splines[k].name;
                    snprintf(bl->size, sizeof bl->size, "%zu knots", knots);
                    bl->relmax  = err[0];
                    bl->max_rel = err[1];
                    bl->single  = scalar_rate(gsl_spline_eval_1d, &sp, xs, n_scalar);
                    bl->multi   = scalar_rate(gsl_spline_eval_1d, &sp, xs, n);
                    bl->sorted  = scalar_rate(gsl_spline_eval_1d, &sp, xs_sorted, n);
                }
                gsl_spline_free(sp.spline);
                gsl_interp_accel_free(sp.acc);
                if (done)
                    break;
                if (knots >= (size_t)1 << 24) {
                    fprintf(stderr, "%s never met tol %g\n", splines[k].name, tol);
                    return EXIT_FAILURE;
                }
            }
        }

        /* baobzi: adaptive tree of Chebyshev panels, the library that inspired
         * treeweave. Its own tol is per leaf, so for every order it supports in
         * 1-D, take the loosest of 1e-6 ... 1e-15 that meets ours, and keep the
         * configuration with the fastest batch. The box is widened by 1e-9
         * relative: baobzi reads out of bounds for a query exactly on the upper
         * edge (segfault on the zeta target with a linspace grid, 2026-09-01). */
        const double center = 0.5 * (a + b), half = 0.5 * (b - a) * (1.0 + 1e-9);
        baseline_t  *best = &baselines[n_baselines];
        best->multi       = 0.0;
        for (int order = 6; order <= 16; order += 2) {
            for (double btol = 1e-6; btol >= 1e-15; btol /= 10) {
                baobzi_input_t input = baobzi_input_default;
                input.func           = baobzi_target;
                input.dim            = 1;
                input.output_dim     = 1;
                input.order          = order;
                input.tol            = btol;
                baobzi_t bz          = baobzi_init(&input, &center, &half);
                if (bz == NULL) {
                    fprintf(stderr, "baobzi_init failed (order %d, tol %g)\n", order, btol);
                    return EXIT_FAILURE;
                }
                rel_errors(baobzi_eval_1d, bz, xs, ref_check, n_check, err);
                if (err[0] <= tol) {
                    const double multi = baobzi_multi_rate(bz, xs, out, n);
                    if (multi > best->multi) {
                        best->name = "baobzi";
                        snprintf(best->size, sizeof best->size, "order %d, tol %.0e", order, btol);
                        best->relmax  = err[0];
                        best->max_rel = err[1];
                        best->single  = scalar_rate(baobzi_eval_1d, bz, xs, n_scalar);
                        best->multi   = multi;
                        best->sorted  = baobzi_multi_rate(bz, xs_sorted, out, n);
                    }
                    baobzi_free(bz);
                    break;
                }
                baobzi_free(bz);
            }
        }
        if (best->multi == 0.0) {
            fprintf(stderr, "baobzi never met tol %g\n", tol);
            return EXIT_FAILURE;
        }
        ++n_baselines;
    }
#endif

    const double tw_single = (double)n_scalar / (tw_single_s * 1e6);
    const double tw_multi  = (double)n / (tw_multi_s * 1e6);
    const double tw_sorted = (double)n / (tw_sorted_s * 1e6);

    printf("zeta(s) = sum_k k^-s (<=%d terms, stop at %.0e rel), fit on [%.1f, %.1f], relative tol %.0e\n",
           ZETA_MAX_TERMS, (double)ZETA_EPS, a, b, tol);
    printf("  max rel err: %.3e\n", max_rel);
    printf("  single-eval  treeweave %.1f  native %.4f Mevals/s  speedup %.1fx\n", tw_single, nat_rate,
           tw_single / nat_rate);
    printf("  multi-eval   treeweave %.1f  native %.4f Mevals/s  speedup %.1fx\n", tw_multi, nat_rate,
           tw_multi / nat_rate);
    printf("  sorted-eval  treeweave %.1f  native %.4f Mevals/s  speedup %.1fx\n", tw_sorted, nat_rate,
           tw_sorted / nat_rate);
#ifdef TREEWEAVE_BENCH_BASELINES
    for (size_t i = 0; i < n_baselines; ++i)
        printf("  %s (%s, max rel err %.3e)  single %.2f  multi %.2f  sorted %.2f Mevals/s\n", baselines[i].name,
               baselines[i].size, baselines[i].max_rel, baselines[i].single, baselines[i].multi, baselines[i].sorted);
#endif

    const char *yaml_path = getenv("TREEWEAVE_BENCH_YAML");
    if (yaml_path != NULL) {
        FILE *y = fopen(yaml_path, "w");
        if (y != NULL) {
            fprintf(y, "language: \"c\"\n");
            fprintf(y, "domain: [%.17e, %.17e]\n", a, b);
            fprintf(y, "tol: %.17e\n", tol);
            fprintf(y, "n_pts: %zu\n", n);
            fprintf(y, "relmax_err: %.17e\n", tw_err[0]);
            fprintf(y, "max_rel_err: %.17e\n", max_rel);
            emit_block(y, "single_eval", tw_single, nat_rate);
            emit_block(y, "multi_eval", tw_multi, nat_rate);
            emit_block(y, "sorted_eval", tw_sorted, nat_rate);
#ifdef TREEWEAVE_BENCH_BASELINES
            if (n_baselines > 0)
                fprintf(y, "interp:\n");
            for (size_t i = 0; i < n_baselines; ++i) {
                fprintf(y, "  - name: \"%s\"\n", baselines[i].name);
                fprintf(y, "    size: \"%s\"\n", baselines[i].size);
                fprintf(y, "    relmax_err: %.17e\n", baselines[i].relmax);
                fprintf(y, "    max_rel_err: %.17e\n", baselines[i].max_rel);
                fprintf(y, "    single_eval: %.17e\n", baselines[i].single);
                fprintf(y, "    multi_eval: %.17e\n", baselines[i].multi);
                fprintf(y, "    sorted_eval: %.17e\n", baselines[i].sorted);
            }
#endif
            fclose(y);
        }
    }

    free(xs);
    free(out);
    free(xs_sorted);
    treeweave_free(fn);
    return (max_rel < 1e-7) ? EXIT_SUCCESS : EXIT_FAILURE;
}
