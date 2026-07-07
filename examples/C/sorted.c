/* sorted.c — 1D -> 1D fit with the sorted-batch fast path, from C11.
 *
 * `treeweave_sorted` is a 1D-only batch evaluator that assumes the
 * inputs are non-decreasing (x[i] <= x[i+1]) and walks leaves monotonically.
 * It must produce the same results as `treeweave_batch` on the same
 * sorted buffer — this demo asserts exactly that. */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include <treeweave.h>

/* f(x) = exp(0.5 x) + sin(3 x) on [0, 1]. */
static void kernel(const double *x, double *y, void *data) {
    (void)data;
    y[0] = exp(0.5 * x[0]) + sin(3.0 * x[0]);
}

/* qsort comparator for ascending doubles. */
static int cmp_double(const void *pa, const void *pb) {
    const double a = *(const double *)pa;
    const double b = *(const double *)pb;
    return (a > b) - (a < b);
}

int main(void) {
    const double a = 0.0;
    const double b = 1.0;

    /* Fit kernel(x) on [0, 1] syntax is
       treeweave_fit(callback, input_dim, output_dim, lower, upper, tolerance, context, options). */
    treeweave_t fn =
        treeweave_fit(kernel,
                      /*input_dim=*/1, /*output_dim=*/1, &a, &b, /*tol=*/1e-10, /*context=*/NULL, /*opts=*/NULL);
    if (fn == NULL) {
        fprintf(stderr, "treeweave_fit failed: %s\n", treeweave_last_error());
        return EXIT_FAILURE;
    }

    /* A pseudo-random scatter of points in (0, 1), then sorted ascending as
     * treeweave_sorted requires. */
    enum { N = 257 };
    double       xs[N];
    unsigned int seed = 12345U;
    for (int i = 0; i < N; ++i) {
        seed  = seed * 1103515245U + 12345U;
        xs[i] = (double)(seed >> 8) / (double)(1U << 24);
    }
    qsort(xs, N, sizeof(double), cmp_double);

    double ym[N]    = {0}; /* general AoS batch */
    double ysort[N] = {0}; /* sorted fast path  */
    /* Evaluate fn on sorted points with both batch paths and print their difference. */
    treeweave_batch(fn, xs, ym, N);
    treeweave_sorted(fn, xs, ysort, N);

    double max_diff = 0.0;
    for (int i = 0; i < N; ++i) {
        const double d = fabs(ysort[i] - ym[i]);
        if (d > max_diff)
            max_diff = d;
    }
    printf("evaluated %d sorted points\n", N);
    printf("max |sorted - multi| = %.3e (expect 0)\n", max_diff);

    fn = treeweave_free(fn);
    return (max_diff == 0.0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
