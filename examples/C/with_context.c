/* with_context.c — using the `context` pointer to parameterize a kernel.
 *
 * A C function pointer cannot capture state the way a C++ lambda does, so the
 * `context` argument to treeweave_fit() is the C stand-in for a closure's
 * captures: treeweave forwards it, untouched, to every invocation of the
 * callback. Here the kernel is f(x) = amplitude * sin(frequency * x); the two
 * parameters live in a Params struct that we hand to the fit as `context`,
 * instead of resorting to file-scope globals. */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include <treeweave.h>

/* Runtime parameters carried through `context`. */
typedef struct {
    double amplitude;
    double frequency;
} Params;

/* The callback recovers its parameters by casting `context` back to Params*.
 * It is plain, reentrant C — no globals — so the same function could be fit
 * concurrently with different parameter sets. */
static void kernel(const double *x, double *y, void *context) {
    const Params *p = (const Params *)context;
    y[0]            = p->amplitude * sin(p->frequency * x[0]);
}

int main(void) {
    const double a = 0.0;
    const double b = 1.0;

    Params params = {/*amplitude=*/2.5, /*frequency=*/7.0};

    /* Pass &params as `context`; default opts (NULL). */
    /* Fit kernel(x) on [0, 1] syntax is
       treeweave_fit(callback, input_dim, output_dim, lower, upper, tolerance, context, options). */
    treeweave_t fn = treeweave_fit(kernel,
                                   /*input_dim=*/1, /*output_dim=*/1, &a, &b, /*tol=*/1e-10, /*context=*/&params,
                                   /*opts=*/NULL);
    if (fn == NULL) {
        fprintf(stderr, "treeweave_fit failed: %s\n", treeweave_last_error());
        return EXIT_FAILURE;
    }

    /* Evaluate fn on points in [0, 1] and print the maximum error. */
    double max_abs_err = 0.0;
    for (int i = 0; i <= 20; ++i) {
        const double x = (double)i / 20.0;
        double       y = 0.0;
        treeweave_eval(fn, &x, &y);
        const double exact = params.amplitude * sin(params.frequency * x);
        const double err   = fabs(y - exact);
        if (err > max_abs_err)
            max_abs_err = err;
    }
    printf("fit f(x) = %.1f*sin(%.1f*x); max |approx - exact| = %.3e\n", params.amplitude, params.frequency,
           max_abs_err);

    fn = treeweave_free(fn);
    return (max_abs_err < 1e-8) ? EXIT_SUCCESS : EXIT_FAILURE;
}
