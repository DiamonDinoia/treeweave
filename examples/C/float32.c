/* float32.c — single-precision (f32) fit and eval from C11.
 *
 * Precision lives in the library prefix: the `treeweavef_*` entry points are the
 * float twins of the `treeweave_*` (double) functions, so the callback, domain
 * corners, and eval buffers are all `float` here. Accuracy is checked at a
 * float-appropriate tolerance (f32 has ~7 significant digits). */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include <treeweave.h>

/* f(x) = exp(0.5 x) + sin(3 x) on [0, 1], in single precision. */
static void kernel(const float *x, float *y, void *data) {
    (void)data;
    y[0] = expf(0.5F * x[0]) + sinf(3.0F * x[0]);
}

int main(void) {
    const float a = 0.0F;
    const float b = 1.0F;

    /* tol is a double even on the f32 path (it matches treeweave::fit's signature);
     * 1e-5 is about as tight as single precision can meaningfully resolve. */
    treeweave_t fn =
        treeweavef_fit(kernel,
                       /*input_dim=*/1, /*output_dim=*/1, &a, &b, /*tol=*/1e-5, /*context=*/NULL, /*opts=*/NULL);
    if (fn == NULL) {
        fprintf(stderr, "treeweavef_fit failed: %s\n", treeweave_last_error());
        return EXIT_FAILURE;
    }

    printf("dtype=%d (1 == F32) input_dim=%d output_dim=%d memory=%zu bytes\n", (int)treeweave_dtype(fn),
           treeweave_input_dim(fn), treeweave_output_dim(fn), treeweave_memory_usage(fn));

    /* Scalar eval at a few points. */
    float max_abs_err = 0.0F;
    for (int i = 0; i <= 20; ++i) {
        const float x = (float)i / 20.0F;
        float       y = 0.0F;
        treeweavef_eval(fn, &x, &y);
        float exact = 0.0F;
        kernel(&x, &exact, NULL);
        const float err = fabsf(y - exact);
        if (err > max_abs_err)
            max_abs_err = err;
    }
    printf("max |approx - exact| (scalar) = %.3e\n", (double)max_abs_err);

    /* AoS batch eval. */
    const float xs[5] = {0.05F, 0.25F, 0.5F, 0.75F, 0.95F};
    float       ys[5] = {0};
    treeweavef_batch(fn, xs, ys, 5);
    float exact_mid = 0.0F;
    kernel(&xs[2], &exact_mid, NULL);
    printf("fn(0.5) approx = %.7f (exact %.7f)\n", (double)ys[2], (double)exact_mid);

    fn = treeweave_free(fn);
    return (max_abs_err < 1e-4F) ? EXIT_SUCCESS : EXIT_FAILURE;
}
