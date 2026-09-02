/* The smallest useful treeweave C program: fit an expensive function once, then
 * evaluate the polynomial approximation instead of the function.
 *
 * Compiled by every C install route under examples/quickstart/ and run by
 * tools/ci/install-test.sh, so the quick-start snippet in the docs is code that
 * CI proved works. */

/* BEGIN DOCS_PROGRAM */
#include <treeweave.h>

#include <math.h>
#include <stdio.h>

/* treeweave calls this while fitting. x holds input_dim values, y holds
 * output_dim; here both are 1. */
static void zeta(const double *x, double *y, void *context) {
    (void)context;
    double sum = 0.0;
    for (int k = 1; k <= 1000; ++k)
        sum += pow((double)k, -x[0]);
    y[0] = sum;
}

int main(void) {
    const double a = 2.0, b = 10.0;

    /* treeweave_fit(callback, input_dim, output_dim, lower, upper, tolerance,
     *               context, options) */
    treeweave_t f = treeweave_fit(zeta, 1, 1, &a, &b, 1e-10, NULL, NULL);
    if (f == NULL) {
        fprintf(stderr, "treeweave_fit failed: %s\n", treeweave_last_error());
        return 1;
    }

    double exact;
    zeta((const double[]){3.5}, &exact, NULL);
    const double approx = treeweave_eval_1d(f, 3.5);
    const double err    = fabs(approx - exact) / fabs(exact);
    printf("f(3.5) = %.15g, relative error %.2e\n", approx, err);

    f = treeweave_free(f);
    return err < 1e-8 ? 0 : 1;
}
