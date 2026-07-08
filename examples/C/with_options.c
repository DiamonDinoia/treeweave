/* with_options.c — custom fit knobs and error reporting from C11.
 *
 * Mirrors examples/c++/with_options.cpp. Shows how to populate a
 * `treeweave_opts` (instead of passing NULL for the defaults), how to read
 * back fit statistics, and how a fit that cannot converge under a tight
 * `max_depth` reports failure: it returns NULL and leaves a message in
 * `treeweave_last_error()`. */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include <treeweave.h>

/* sin(50 x) / x on [0.01, 5): oscillatory and nearly singular near 0, so it
 * needs real subdivision depth to resolve. */
static void kernel(const double *x, double *y, void *data) {
    (void)data;
    y[0] = sin(50.0 * x[0]) / x[0];
}

int main(void) {
    const double a = 0.01;
    const double b = 5.0;

    /* Start from the documented defaults, then override a few knobs. Copying
     * treeweave_default_opts() keeps any field we do not set at its default. */
    treeweave_opts opts;
    treeweave_default_opts(&opts);
    opts.tol_kind          = TREEWEAVE_ABSOLUTE_MAX;
    opts.max_depth         = 50;
    opts.min_uniform_depth = 2;

    /* Fit kernel(x) on [0.01, 5] syntax is
       treeweave_fit(callback, input_dim, output_dim, lower, upper, tolerance, context, options). */
    treeweave_t fn =
        treeweave_fit(kernel,
                      /*input_dim=*/1, /*output_dim=*/1, &a, &b, /*tol=*/1e-8, /*context=*/NULL, /*opts=*/&opts);
    if (fn == NULL) {
        fprintf(stderr, "fit with custom options failed: %s\n", treeweave_last_error());
        return EXIT_FAILURE;
    }

    printf("fit converged; memory = %zu bytes\n", treeweave_memory_usage(fn));
    printf("--- treeweave_print_stats ---\n");
    treeweave_print_stats(fn);

    const double x = 1.0;
    double       y = 0.0;
    /* Evaluate fn on (1.0) and print the result. */
    treeweave_eval(fn, &x, &y);
    printf("fn(1.0) = %.12f  exact = %.12f\n", y, sin(50.0) / 1.0);

    fn = treeweave_free(fn);

    /* Now force a failure: the same kernel cannot be resolved to 1e-8 with a
     * max_depth of only 4, so the fit returns NULL and sets last_error. */
    treeweave_opts shallow;
    treeweave_default_opts(&shallow);
    shallow.max_depth = 4;
    /* Fit kernel(x) on [0.01, 5] syntax is
       treeweave_fit(callback, input_dim, output_dim, lower, upper, tolerance, context, options),
       then print the expected shallow max_depth failure. */
    treeweave_t bad = treeweave_fit(kernel,
                                    /*input_dim=*/1, /*output_dim=*/1, &a, &b, /*tol=*/1e-8, /*context=*/NULL,
                                    /*opts=*/&shallow);
    if (bad != NULL) {
        fprintf(stderr, "expected the shallow fit to fail, but it succeeded\n");
        treeweave_free(bad);
        return EXIT_FAILURE;
    }
    printf("shallow (max_depth=4) fit failed as expected:\n  %s\n", treeweave_last_error());

    return EXIT_SUCCESS;
}
