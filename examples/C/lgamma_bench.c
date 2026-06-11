/* lgamma_bench.c — treeweave vs the C standard-library lgamma(), via the C ABI.
 *
 * The C member of the cross-language lgamma benchmark family (see
 * examples/c++/lgamma_bench.cpp for the rationale). log-Gamma is fit on [3, 50)
 * — where it is smooth, positive and monotone, so relative error is well
 * defined — with treeweave's default RelativeMax tolerance, then compared to the
 * native lgamma() on max relative error, throughput, and speedup.
 *
 * Compiled as C11 (not C++): also exercises the C ABI on a real workload. */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <treeweave.h>

/* lgamma on [3, 50). input_dim == output_dim == 1. */
static void kernel(const double *x, double *y, void *context) {
    (void)context;
    y[0] = lgamma(x[0]);
}

/* Seconds from an arbitrary epoch. CLOCK_MONOTONIC is POSIX (Linux, macOS);
 * MSVC has no clock_gettime, so fall back to C11 timespec_get(), which every
 * C11 runtime provides. Either is ample for timing a multi-million-eval loop. */
static double now_s(void) {
    struct timespec ts;
#if defined(CLOCK_MONOTONIC)
    clock_gettime(CLOCK_MONOTONIC, &ts);
#else
    timespec_get(&ts, TIME_UTC);
#endif
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

int main(void) {
    const double a = 3.0;
    const double b = 50.0;

    /* opts = NULL -> defaults, whose tol_kind is already RELATIVE_MAX (the right
       measure for this zero-free, monotone function). */
    treeweave_t fn = treeweave_fit(kernel, 1, 1, &a, &b, /*tol=*/1e-10, /*context=*/NULL, /*opts=*/NULL);
    if (fn == NULL) {
        fprintf(stderr, "treeweave_fit failed: %s\n", treeweave_last_error());
        return EXIT_FAILURE;
    }

    const size_t n   = 1000000;
    double      *xs  = (double *)malloc(n * sizeof(double));
    double      *out = (double *)malloc(n * sizeof(double));
    if (xs == NULL || out == NULL) {
        fprintf(stderr, "alloc failed\n");
        free(xs);
        free(out);
        treeweave_free(fn);
        return EXIT_FAILURE;
    }

    srand(7);
    for (size_t i = 0; i < n; ++i)
        xs[i] = a + (b - a) * ((double)rand() / (double)RAND_MAX);

    /* --- accuracy vs the library ------------------------------------------ */
    double max_rel = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const double approx = treeweave_eval_1d(fn, xs[i]);
        const double exact  = lgamma(xs[i]);
        const double rel    = fabs(approx - exact) / fabs(exact);
        if (rel > max_rel)
            max_rel = rel;
    }

    /* --- throughput: treeweave vs the library ------------------------------- *
     * `volatile` sink defeats dead-code elimination of both timed loops. */
    volatile double sink = 0.0;

    treeweave_batch(fn, xs, out, n); /* warm-up (untimed) */
    sink = out[0];

    const double t0 = now_s();
    treeweave_batch(fn, xs, out, n); /* timed */
    const double t1 = now_s();
    sink            = out[0];

    const double t2 = now_s();
    for (size_t i = 0; i < n; ++i)
        sink = sink + lgamma(xs[i]);
    const double t3 = now_s();

    const double treeweave_s = t1 - t0;
    const double lib_s       = t3 - t2;

    printf("lgamma fit on [%.1f, %.1f), relative tol %.0e\n", a, b, 1e-10);
    printf("  max rel err: %.3e\n", max_rel);
    printf("  treeweave:  %.1f Mevals/s\n", (double)n / (treeweave_s * 1e6));
    printf("  library: %.1f Mevals/s\n", (double)n / (lib_s * 1e6));
    printf("  speedup: %.2fx\n", lib_s / treeweave_s);

    free(xs);
    free(out);
    treeweave_free(fn);
    return (max_rel < 1e-7) ? EXIT_SUCCESS : EXIT_FAILURE;
}
