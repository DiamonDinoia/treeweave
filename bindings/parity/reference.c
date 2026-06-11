/* reference.c — cross-language parity reference.
 *
 * Fits the 2D -> 3D kernel from examples/C/vector_output.c via the C ABI and
 * prints `x0,x1,y0,y1,y2` (full double precision) for a fixed point set. The
 * Python and Julia parity scripts fit the identical kernel and must reproduce
 * these lines; run_parity.sh diffs them numerically. */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include <treeweave.h>

static void f(const double *x, double *y, void *data) {
    (void)data;
    y[0] = exp(0.3 * x[0]) + sin(2.0 * x[1]);
    y[1] = cos(x[0] * x[1]) + 2.0;
    y[2] = (x[0] * x[0]) + x[1] + 1.0;
}

/* Fixed evaluation points, identical in parity.py / parity.jl. */
static const double PTS[][2] = {
    {0.3, 0.4}, {0.7, 1.1}, {1.0, 0.5}, {1.2, 1.3}, {0.5, 0.9},
};
enum { NPTS = sizeof(PTS) / sizeof(PTS[0]) };

int main(void) {
    const double a[2] = {0.2, 0.2};
    const double b[2] = {1.5, 1.5};

    treeweave_t fn = treeweave_fit(f, 2, 3, a, b, 1e-8, NULL, NULL);
    if (fn == NULL) {
        fprintf(stderr, "reference fit failed: %s\n", treeweave_last_error());
        return EXIT_FAILURE;
    }
    for (int i = 0; i < NPTS; ++i) {
        double y[3] = {0};
        treeweave_eval(fn, PTS[i], y);
        printf("%.17g,%.17g,%.17g,%.17g,%.17g\n", PTS[i][0], PTS[i][1], y[0], y[1], y[2]);
    }
    treeweave_free(fn);
    return EXIT_SUCCESS;
}
