/* vector_output.c: 2D -> 3D vector-valued fit from C11.
 *
 * Mirrors examples/c++/vector_output.cpp. Shows the multi-output path and the
 * two batch layouts: AoS (`treeweave_batch`, results packed
 * [c0,c1,c2, c0,c1,c2, ...]) and SoA (`treeweave_transposed`, one
 * contiguous buffer per output component). The two layouts must agree
 * element-for-element. */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include <treeweave.h>

/* A smooth 2 -> 3 map on a box off the origin. */
static void f(const double *x, double *y, void *data) {
    (void)data;
    y[0] = exp(0.3 * x[0]) + sin(2.0 * x[1]);
    y[1] = cos(x[0] * x[1]) + 2.0;
    y[2] = x[0] * x[0] + x[1] + 1.0;
}

int main(void) {
    const double a[2] = {0.2, 0.2};
    const double b[2] = {1.5, 1.5};

    /* Fit f(x, y) on [0.2, 1.5]^2 syntax is
       treeweave_fit(callback, input_dim, output_dim, lower, upper, tolerance, context, options). */
    treeweave_t fn =
        treeweave_fit(f,
                      /*input_dim=*/2, /*output_dim=*/3, a, b, /*tol=*/1e-8, /*context=*/NULL, /*opts=*/NULL);
    if (fn == NULL) {
        fprintf(stderr, "treeweave_fit failed: %s\n", treeweave_last_error());
        return EXIT_FAILURE;
    }

    enum { N = 64 };
    double xs[N * 2];
    for (int i = 0; i < N; ++i) {
        xs[2 * i + 0] = 0.25 + 1.2 * (double)i / (double)N;
        xs[2 * i + 1] = 1.45 - 1.2 * (double)i / (double)N;
    }

    /* AoS: N points * 3 components, packed per point. */
    double aos[N * 3];
    /* Evaluate fn on batched points and print AoS/SoA parity plus max error. */
    treeweave_batch(fn, xs, aos, N);

    /* SoA: three contiguous component buffers. */
    double  c0[N], c1[N], c2[N];
    double *soa[3] = {c0, c1, c2};
    treeweave_transposed(fn, xs, soa, N);

    /* The two layouts must be bit-for-bit identical, and both must track the
     * exact function within a generous margin of the fit tolerance. */
    double max_soa_aos_diff = 0.0;
    double max_abs_err      = 0.0;
    for (int i = 0; i < N; ++i) {
        double exact[3] = {0};
        f(&xs[2 * i], exact, NULL);
        const double *comp[3] = {c0, c1, c2};
        for (int k = 0; k < 3; ++k) {
            const double soa_val = comp[k][i];
            const double aos_val = aos[3 * i + k];
            const double d       = fabs(soa_val - aos_val);
            if (d > max_soa_aos_diff)
                max_soa_aos_diff = d;
            const double e = fabs(aos_val - exact[k]);
            if (e > max_abs_err)
                max_abs_err = e;
        }
    }
    printf("max |SoA - AoS| = %.3e (expect 0)\n", max_soa_aos_diff);
    printf("max |approx - exact| over %d points = %.3e\n", N, max_abs_err);

    fn = treeweave_free(fn);
    return (max_soa_aos_diff == 0.0 && max_abs_err < 1e-6) ? EXIT_SUCCESS : EXIT_FAILURE;
}
