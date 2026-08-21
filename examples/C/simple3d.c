/* simple3d.c: 3D -> 1D scalar fit from C11.
 *
 * Mirrors examples/c++/simple3d.cpp: an anisotropic Gaussian on [-1, 1]^3.
 * Shows scalar and AoS batch eval on a 3D domain. */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include <treeweave.h>

/* f(x, y, z) = exp(-0.5 x^2 - 1.5 y^2 - 2 z^2). */
static void gaussian(const double *x, double *y, void *data) {
    (void)data;
    y[0] = exp(-0.5 * x[0] * x[0] - 1.5 * x[1] * x[1] - 2.0 * x[2] * x[2]);
}

int main(void) {
    const double a[3] = {-1.0, -1.0, -1.0};
    const double b[3] = {1.0, 1.0, 1.0};

    /* Fit gaussian(x, y, z) on [-1, 1]^3 syntax is
       treeweave_fit(callback, input_dim, output_dim, lower, upper, tolerance, context, options). */
    treeweave_t fn =
        treeweave_fit(gaussian,
                      /*input_dim=*/3, /*output_dim=*/1, a, b, /*tol=*/1e-8, /*context=*/NULL, /*opts=*/NULL);
    if (fn == NULL) {
        fprintf(stderr, "treeweave_fit failed: %s\n", treeweave_last_error());
        return EXIT_FAILURE;
    }

    printf("input_dim=%d output_dim=%d memory=%zu bytes\n", treeweave_input_dim(fn), treeweave_output_dim(fn),
           treeweave_memory_usage(fn));

    /* Evaluate fn on a grid and print the maximum error. */
    /* Scalar eval over a coarse interior grid; track worst absolute error. */
    double max_abs_err = 0.0;
    for (int i = -8; i <= 8; ++i) {
        for (int j = -8; j <= 8; ++j) {
            for (int k = -8; k <= 8; ++k) {
                const double p[3] = {i / 10.0, j / 10.0, k / 10.0};
                double       y    = 0.0;
                treeweave_eval(fn, p, &y);
                double exact = 0.0;
                gaussian(p, &exact, NULL);
                const double err = fabs(y - exact);
                if (err > max_abs_err)
                    max_abs_err = err;
            }
        }
    }
    printf("max |approx - exact| over interior grid: %.3e\n", max_abs_err);

    /* AoS batch eval: 2 packed (x, y, z) coords -> 2 results. */
    const double xs[6] = {0.0, 0.0, 0.0, 0.3, -0.4, 0.5};
    double       ys[2] = {0};
    /* Evaluate fn on batched points and print the origin result. */
    treeweave_batch(fn, xs, ys, 2);
    printf("f(0,0,0) approx = %.12f (exact 1.0)\n", ys[0]);

    fn = treeweave_free(fn);
    return (max_abs_err < 1e-6) ? EXIT_SUCCESS : EXIT_FAILURE;
}
