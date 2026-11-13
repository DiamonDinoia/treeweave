/* fit_file.c: save a fit to a plain-text file and load it on the next fit.
 *
 * treeweave_opts.filename names the file. The first fit writes it. The next
 * fit with the same arguments loads it and does not call the callback. */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include <treeweave.h>

/* f(x) = exp(x) sin(5x). `data` counts the calls. */
static void kernel(const double *x, double *y, void *data) {
    ++*(int *)data;
    y[0] = exp(x[0]) * sin(5.0 * x[0]);
}

int main(void) {
    const char  *path = "treeweave_fit_file_example.txt";
    const double a    = 0.0;
    const double b    = 2.0;
    int          calls = 0;

    remove(path);
    treeweave_opts opts;
    treeweave_default_opts(&opts);
    opts.filename = path;

    /* First fit: the file does not exist, so treeweave fits f and writes the file. */
    treeweave_t fitted = treeweave_fit(kernel, 1, 1, &a, &b, /*tol=*/1e-10, &calls, &opts);
    if (fitted == NULL) {
        fprintf(stderr, "treeweave_fit failed: %s\n", treeweave_last_error());
        return EXIT_FAILURE;
    }
    printf("fit:  %d calls to f, file %s\n", calls, path);

    /* Second fit, same arguments: treeweave loads the file. */
    calls              = 0;
    treeweave_t loaded = treeweave_fit(kernel, 1, 1, &a, &b, /*tol=*/1e-10, &calls, &opts);
    if (loaded == NULL) {
        fprintf(stderr, "treeweave_fit failed: %s\n", treeweave_last_error());
        return EXIT_FAILURE;
    }
    printf("load: %d calls to f\n", calls);

    /* The file keeps the digits that tol needs, so the loaded fit differs
     * from the fitted one by at most tol * max|f| / 10. */
    double max_f = 0.0, max_diff = 0.0;
    for (int i = 0; i <= 100; ++i) {
        const double x  = 2.0 * i / 100.0;
        const double y1 = treeweave_eval_1d(fitted, x);
        const double y2 = treeweave_eval_1d(loaded, x);
        max_f           = fmax(max_f, fabs(y1));
        max_diff        = fmax(max_diff, fabs(y1 - y2));
    }
    printf("max |loaded - fitted| / max|f|: %g (tol 1e-10)\n", max_diff / max_f);

    fitted = treeweave_free(fitted);
    loaded = treeweave_free(loaded);
    remove(path);
    return calls == 0 && max_diff <= 1e-10 * max_f ? EXIT_SUCCESS : EXIT_FAILURE;
}
