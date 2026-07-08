/* simple.c — minimal treeweave C API usage, compiled as C11 (not C++).
 *
 * Doubles as the ABI smoke test: if this links and runs, the header is
 * C-clean and no C++ exception escaped the boundary. */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include <treeweave.h>

/* f(x) = exp(x) on [0, 1]. input_dim == output_dim == 1, so x and y each
 * point at a single value. */
static void kernel(const double *x, double *y, void *data) {
    (void)data;
    y[0] = exp(x[0]);
}

int main(void) {
    const double a = 0.0;
    const double b = 1.0;

    /* Fit exp(x) on [0, 1] syntax is
       treeweave_fit(callback, input_dim, output_dim, lower, upper, tolerance, context, options). */
    treeweave_t fn =
        treeweave_fit(kernel,
                      /*input_dim=*/1, /*output_dim=*/1, &a, &b, /*tol=*/1e-10, /*context=*/NULL, /*opts=*/NULL);
    if (fn == NULL) {
        fprintf(stderr, "treeweave_fit failed: %s\n", treeweave_last_error());
        return EXIT_FAILURE;
    }

    printf("dtype=%d input_dim=%d output_dim=%d memory=%zu bytes\n", (int)treeweave_dtype(fn), treeweave_input_dim(fn),
           treeweave_output_dim(fn), treeweave_memory_usage(fn));

    /* Evaluate fn on points in [0, 1] and print the maximum error. */
    /* Scalar eval at a few points; compare to the exact value. The by-value
       treeweave_eval_1d returns the result directly — no output pointer, which
       is the ergonomic form for a 1D->1D fit (y = f(x)). */
    double max_abs_err = 0.0;
    for (int i = 0; i <= 10; ++i) {
        const double x   = (double)i / 10.0;
        const double y   = treeweave_eval_1d(fn, x);
        const double err = fabs(y - exp(x));
        if (err > max_abs_err)
            max_abs_err = err;
    }
    printf("max |approx - exp| over 11 points: %.3e\n", max_abs_err);

    /* Closed upper endpoint: evaluating exactly at the upper corner b returns
       the boundary value (not NaN). The lower corner a is in-domain too. */
    printf("f(a=%.1f) approx = %.12f (exact %.12f)\n", a, treeweave_eval_1d(fn, a), exp(a));
    printf("f(b=%.1f) approx = %.12f (exact %.12f)\n", b, treeweave_eval_1d(fn, b), exp(b));

    /* Batch (AoS) eval. */
    double xs[5] = {0.05, 0.25, 0.5, 0.75, 0.95};
    double ys[5] = {0};
    treeweave_batch(fn, xs, ys, 5);
    printf("exp(0.5) approx = %.12f (exact %.12f)\n", ys[2], exp(0.5));

    fn = treeweave_free(fn);
    return (max_abs_err < 1e-8) ? EXIT_SUCCESS : EXIT_FAILURE;
}
