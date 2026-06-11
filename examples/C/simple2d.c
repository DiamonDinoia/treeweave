/* simple2d.c — 2D -> 1D scalar fit from C11.
 *
 * Mirrors examples/c++/simple2d.cpp: a Gaussian bump on the unit square.
 * Shows the scalar `treeweave_eval` and the AoS batch `treeweave_batch`
 * on a multi-dimensional domain, where `a`/`b` are the lower/upper corners. */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include <treeweave.h>

/* f(x, y) = exp(-100 (x-0.5)^2 - (y-0.5)^2). input_dim == 2, output_dim == 1:
 * x points at two coords, y at a single result. */
static void bump(const double *x, double *y, void *data) {
    (void)data;
    const double dx = x[0] - 0.5;
    const double dy = x[1] - 0.5;
    y[0]            = exp(-100.0 * dx * dx - dy * dy);
}

int main(void) {
    const double a[2] = {0.0, 0.0};
    const double b[2] = {1.0, 1.0};

    treeweave_t fn =
        treeweave_fit(bump,
                      /*input_dim=*/2, /*output_dim=*/1, a, b, /*tol=*/1e-8, /*context=*/NULL, /*opts=*/NULL);
    if (fn == NULL) {
        fprintf(stderr, "treeweave_fit failed: %s\n", treeweave_last_error());
        return EXIT_FAILURE;
    }

    printf("input_dim=%d output_dim=%d memory=%zu bytes\n", treeweave_input_dim(fn), treeweave_output_dim(fn),
           treeweave_memory_usage(fn));

    /* Scalar eval on a grid; track the worst absolute error vs the exact f. */
    double max_abs_err = 0.0;
    for (int i = 1; i < 10; ++i) {
        for (int j = 1; j < 10; ++j) {
            const double xy[2] = {(double)i / 10.0, (double)j / 10.0};
            double       y     = 0.0;
            treeweave_eval(fn, xy, &y);
            double exact = 0.0;
            bump(xy, &exact, NULL);
            const double err = fabs(y - exact);
            if (err > max_abs_err)
                max_abs_err = err;
        }
    }
    printf("max |approx - exact| over interior grid: %.3e\n", max_abs_err);

    /* AoS batch eval: 3 packed (x, y) coords -> 3 results. */
    const double xs[6] = {0.5, 0.5, 0.25, 0.75, 0.9, 0.1};
    double       ys[3] = {0};
    treeweave_batch(fn, xs, ys, 3);
    double exact_center = 0.0;
    bump(&xs[0], &exact_center, NULL);
    printf("f(0.5,0.5) approx = %.12f (exact %.12f)\n", ys[0], exact_center);

    fn = treeweave_free(fn);
    return (max_abs_err < 1e-6) ? EXIT_SUCCESS : EXIT_FAILURE;
}
