/* Zeta bench (C ABI): treeweave vs adaptive-stop Riemann-zeta (<=160 terms, rel 1e-10); times
 * single/batch/sorted modes. TREEWEAVE_BENCH_YAML=path emits YAML. (see devel/agents/build-notes.md) */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <treeweave.h>

/* Fair baseline: sum k^-s until a term is below ZETA_EPS relative to the running
 * total, capped at ZETA_MAX_TERMS — a competent zeta stops early. */
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

static void kernel(const double *x, double *y, void *context) {
    (void)context;
    y[0] = zeta_partial(x[0]);
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

    double max_rel = 0.0;
    for (size_t i = 0; i < n_native; ++i) {
        const double approx = treeweave_eval_1d(fn, xs[i]);
        const double exact  = zeta_partial(xs[i]);
        const double rel    = fabs(approx - exact) / fabs(exact);
        if (rel > max_rel)
            max_rel = rel;
    }

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

    const char *yaml_path = getenv("TREEWEAVE_BENCH_YAML");
    if (yaml_path != NULL) {
        FILE *y = fopen(yaml_path, "w");
        if (y != NULL) {
            fprintf(y, "language: \"c\"\n");
            fprintf(y, "domain: [%.17e, %.17e]\n", a, b);
            fprintf(y, "tol: %.17e\n", tol);
            fprintf(y, "n_pts: %zu\n", n);
            fprintf(y, "max_rel_err: %.17e\n", max_rel);
            emit_block(y, "single_eval", tw_single, nat_rate);
            emit_block(y, "multi_eval", tw_multi, nat_rate);
            emit_block(y, "sorted_eval", tw_sorted, nat_rate);
            fclose(y);
        }
    }

    free(xs);
    free(out);
    free(xs_sorted);
    treeweave_free(fn);
    return (max_rel < 1e-7) ? EXIT_SUCCESS : EXIT_FAILURE;
}
