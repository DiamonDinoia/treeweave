/* test_c_abi.c: pure-C conformance test for the treeweave C ABI (treeweave.h).
 *
 * Unlike tests/test_c.cpp (a C++ TU that checks the C API against a direct
 * treeweave::fit reference), this is compiled by the *C* compiler as C11 and
 * cannot call into C++ at all: so it exercises every entry point exactly as a
 * downstream C / Fortran consumer would, catching ABI and language-linkage
 * problems the C++ parity test cannot.
 *
 * Because C cannot reach treeweave::fit, correctness is checked two ways:
 *   - closed-form: eval(x) is within a generous margin of the exact kernel;
 *   - self-consistency: two paths on the same handle agree (often bit-exact).
 *
 * A tiny CHECK macro counts failures, prints each, and main() returns that
 * count: so 0 means pass and the process exit code is the failure count. */

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <treeweave.h>

/* ---- failure-counting harness --------------------------------------- */

static int g_failures = 0;

static void check_impl(int cond, const char *expr, const char *file, int line) {
    if (!cond) {
        fprintf(stderr, "FAIL %s:%d: %s\n", file, line, expr);
        ++g_failures;
    }
}
#define CHECK(cond) check_impl((cond) ? 1 : 0, #cond, __FILE__, __LINE__)

/* ---- kernels and their closed forms (smooth, O(1) on the unit box) --- */

static void k_1d_1(const double *x, double *y, void *d) {
    (void)d;
    y[0] = exp(0.5 * x[0]) + sin(3.0 * x[0]);
}
static double exact_1d_1(double x) { return exp(0.5 * x) + sin(3.0 * x); }

static void k_1d_2(const double *x, double *y, void *d) {
    (void)d;
    y[0] = exp(0.5 * x[0]);
    y[1] = sin(3.0 * x[0]) + 2.0;
}

static void k_2d_1(const double *x, double *y, void *d) {
    (void)d;
    y[0] = exp(0.3 * x[0]) + sin(2.0 * x[1]);
}
static double exact_2d_1(double x0, double x1) { return exp(0.3 * x0) + sin(2.0 * x1); }

static void k_2d_3(const double *x, double *y, void *d) {
    (void)d;
    y[0] = exp(0.3 * x[0]);
    y[1] = sin(2.0 * x[1]) + 2.0;
    y[2] = cos(x[0] * x[1]) + 2.0;
}

static void k_3d_1(const double *x, double *y, void *d) {
    (void)d;
    y[0] = exp(0.2 * x[0]) + sin(x[1]) + cos(x[2]);
}
static double exact_3d_1(double x0, double x1, double x2) { return exp(0.2 * x0) + sin(x1) + cos(x2); }

static void k_1d_1f(const float *x, float *y, void *d) {
    (void)d;
    y[0] = expf(0.5F * x[0]) + sinf(3.0F * x[0]);
}
static float exact_1d_1f(float x) { return expf(0.5F * x) + sinf(3.0F * x); }

/* A deterministic LCG scatter in [0, 1); no <stdlib.h> rand() dependence. */
static double next_unit(unsigned int *state) {
    *state = *state * 1103515245U + 12345U;
    return (double)(*state >> 8) / (double)(1U << 24);
}

/* ---- 1D scalar: auto-degree fit, eval parity, multi parity, tol check -- */

static void test_1d_scalar_auto_degree(void) {
    /* The C ABI auto-selects a register-optimal leaf degree per detected CPU.
     * The test checks the result against the requested tol, and checks that
     * scalar eval and multi eval agree. */
    const double tol = 1e-9;
    const double a = 0.0, b = 1.0;
    treeweave_t  h = treeweave_fit(k_1d_1, 1, 1, &a, &b, tol, NULL, NULL);
    CHECK(h != NULL);
    if (h == NULL)
        return;
    CHECK(treeweave_dtype(h) == TREEWEAVE_F64);
    CHECK(treeweave_input_dim(h) == 1);
    CHECK(treeweave_output_dim(h) == 1);
    CHECK(treeweave_memory_usage(h) > 0);

    enum { N = 256 };
    double       xs[N], multi[N];
    unsigned int seed = 42U;
    for (int i = 0; i < N; ++i)
        xs[i] = next_unit(&seed);
    treeweave_batch(h, xs, multi, N);

    double max_err = 0.0, max_parity = 0.0, max_mag = 1.0;
    for (int i = 0; i < N; ++i) {
        double y = 0.0;
        treeweave_eval(h, &xs[i], &y);
        const double exact   = exact_1d_1(xs[i]);
        const double rel_err = fabs(y - exact) / (fabs(exact) + 1e-30);
        if (rel_err > max_err)
            max_err = rel_err;
        if (fabs(multi[i]) > max_mag)
            max_mag = fabs(multi[i]);
        const double pe = fabs(y - multi[i]);
        if (pe > max_parity)
            max_parity = pe;
    }
    /* auto-degree should meet tol; allow a small factor for sampling bias */
    CHECK(max_err < tol * 100.0);
    /* Single-point (treeweave_eval) and batch (treeweave_batch) evaluate the same
     * polynomial but through different code paths (scalar Horner vs SIMD). With
     * hardware FMA the two are bit-identical; without it (e.g. an SSE2 baseline
     * build, as on MSVC x64) they differ by at most a few ULP. Accuracy is
     * checked above; this check only requires agreement to rounding. */
    CHECK(max_parity <= 16.0 * DBL_EPSILON * max_mag);
    treeweave_free(h);
}

/* ---- accuracy check: (dtype, input_dim) combinations meet tol ---------- */

static void test_auto_degree_meets_tol(void) {
    /* 1D f64 */
    {
        const double tol = 1e-9;
        const double a = 0.0, b = 1.0;
        treeweave_t  h = treeweave_fit(k_1d_1, 1, 1, &a, &b, tol, NULL, NULL);
        CHECK(h != NULL);
        if (h != NULL) {
            double       max_err = 0.0;
            unsigned int seed    = 1U;
            for (int i = 0; i < 500; ++i) {
                const double x = next_unit(&seed);
                double       y = 0.0;
                treeweave_eval(h, &x, &y);
                const double exact = exact_1d_1(x);
                const double re    = fabs(y - exact) / (fabs(exact) + 1e-30);
                if (re > max_err)
                    max_err = re;
            }
            CHECK(max_err < tol * 100.0);
            treeweave_free(h);
        }
    }
    /* 2D f64 */
    {
        const double tol  = 1e-8;
        const double a[2] = {0.0, 0.0}, b[2] = {1.0, 1.0};
        treeweave_t  h = treeweave_fit(k_2d_1, 2, 1, a, b, tol, NULL, NULL);
        CHECK(h != NULL);
        if (h != NULL) {
            double       max_err = 0.0;
            unsigned int seed    = 2U;
            for (int i = 0; i < 400; ++i) {
                const double p[2] = {next_unit(&seed), next_unit(&seed)};
                double       y    = 0.0;
                treeweave_eval(h, p, &y);
                const double exact = exact_2d_1(p[0], p[1]);
                const double re    = fabs(y - exact) / (fabs(exact) + 1e-30);
                if (re > max_err)
                    max_err = re;
            }
            CHECK(max_err < tol * 100.0);
            treeweave_free(h);
        }
    }
    /* 3D f64 */
    {
        const double tol  = 1e-7;
        const double a[3] = {0.0, 0.0, 0.0}, b[3] = {1.0, 1.0, 1.0};
        treeweave_t  h = treeweave_fit(k_3d_1, 3, 1, a, b, tol, NULL, NULL);
        CHECK(h != NULL);
        if (h != NULL) {
            double       max_err = 0.0;
            unsigned int seed    = 3U;
            for (int i = 0; i < 400; ++i) {
                const double p[3] = {next_unit(&seed), next_unit(&seed), next_unit(&seed)};
                double       y    = 0.0;
                treeweave_eval(h, p, &y);
                const double exact = exact_3d_1(p[0], p[1], p[2]);
                const double re    = fabs(y - exact) / (fabs(exact) + 1e-30);
                if (re > max_err)
                    max_err = re;
            }
            CHECK(max_err < tol * 100.0);
            treeweave_free(h);
        }
    }
    /* 1D f32 */
    {
        const float tol_f = 1e-5F;
        const float a = 0.0F, b = 1.0F;
        treeweave_t h = treeweavef_fit(k_1d_1f, 1, 1, &a, &b, (double)tol_f, NULL, NULL);
        CHECK(h != NULL);
        if (h != NULL) {
            float max_err = 0.0F;
            for (int i = 0; i <= 200; ++i) {
                const float x = (float)i / 200.0F;
                float       y = 0.0F;
                treeweavef_eval(h, &x, &y);
                const float exact = exact_1d_1f(x);
                const float re    = fabsf(y - exact) / (fabsf(exact) + 1e-10F);
                if (re > max_err)
                    max_err = re;
            }
            CHECK(max_err < tol_f * 100.0F);
            treeweave_free(h);
        }
    }
}

/* ---- sorted fast path == general multi on sorted input -------------- */

static int cmp_double(const void *pa, const void *pb) {
    const double a = *(const double *)pa, b = *(const double *)pb;
    return (a > b) - (a < b);
}

static void test_sorted_matches_multi(void) {
    const double a = 0.0, b = 1.0;
    treeweave_t  h = treeweave_fit(k_1d_1, 1, 1, &a, &b, 1e-10, NULL, NULL);
    CHECK(h != NULL);
    if (h == NULL)
        return;

    enum { N = 300 };
    double       xs[N], multi[N], sorted[N];
    unsigned int seed = 99U;
    for (int i = 0; i < N; ++i)
        xs[i] = next_unit(&seed);
    qsort(xs, N, sizeof(double), cmp_double);
    treeweave_batch(h, xs, multi, N);
    treeweave_sorted(h, xs, sorted, N);
    for (int i = 0; i < N; ++i)
        CHECK(sorted[i] == multi[i]);
    treeweave_free(h);
}

/* ---- SoA == AoS, component by component ----------------------------- */

static void test_soa_matches_aos(void) {
    const double a[2] = {0.0, 0.0}, b[2] = {1.0, 1.0};
    treeweave_t  h = treeweave_fit(k_2d_3, 2, 3, a, b, 1e-8, NULL, NULL);
    CHECK(h != NULL);
    if (h == NULL)
        return;
    CHECK(treeweave_output_dim(h) == 3);

    enum { N = 128 };
    double       xs[N * 2], aos[N * 3];
    double       c0[N], c1[N], c2[N];
    double      *soa[3] = {c0, c1, c2};
    unsigned int seed   = 7U;
    for (int i = 0; i < N * 2; ++i)
        xs[i] = next_unit(&seed);
    treeweave_batch(h, xs, aos, N);
    treeweave_transposed(h, xs, soa, N);

    double max_err = 0.0;
    for (int i = 0; i < N; ++i) {
        CHECK(c0[i] == aos[3 * i + 0]);
        CHECK(c1[i] == aos[3 * i + 1]);
        CHECK(c2[i] == aos[3 * i + 2]);
        double exact[3] = {0};
        k_2d_3(&xs[2 * i], exact, NULL);
        for (int k = 0; k < 3; ++k) {
            const double e = fabs(aos[3 * i + k] - exact[k]);
            if (e > max_err)
                max_err = e;
        }
    }
    CHECK(max_err < 1e-5);
    treeweave_free(h);
}

/* ---- 2D / 3D scalar + 1D vector closed-form parity ------------------ */

static void test_higher_dim_fits(void) {
    /* 2D -> 1D */
    {
        const double a[2] = {0.0, 0.0}, b[2] = {1.0, 1.0};
        treeweave_t  h = treeweave_fit(k_2d_1, 2, 1, a, b, 1e-8, NULL, NULL);
        CHECK(h != NULL);
        if (h != NULL) {
            CHECK(treeweave_input_dim(h) == 2);
            double       max_err = 0.0;
            unsigned int seed    = 3U;
            for (int i = 0; i < 200; ++i) {
                const double p[2] = {next_unit(&seed), next_unit(&seed)};
                double       y    = 0.0;
                treeweave_eval(h, p, &y);
                const double e = fabs(y - exact_2d_1(p[0], p[1]));
                if (e > max_err)
                    max_err = e;
            }
            CHECK(max_err < 1e-5);
            treeweave_free(h);
        }
    }
    /* 3D -> 1D */
    {
        const double a[3] = {0.0, 0.0, 0.0}, b[3] = {1.0, 1.0, 1.0};
        treeweave_t  h = treeweave_fit(k_3d_1, 3, 1, a, b, 1e-7, NULL, NULL);
        CHECK(h != NULL);
        if (h != NULL) {
            CHECK(treeweave_input_dim(h) == 3);
            double       max_err = 0.0;
            unsigned int seed    = 5U;
            for (int i = 0; i < 200; ++i) {
                const double p[3] = {next_unit(&seed), next_unit(&seed), next_unit(&seed)};
                double       y    = 0.0;
                treeweave_eval(h, p, &y);
                const double e = fabs(y - exact_3d_1(p[0], p[1], p[2]));
                if (e > max_err)
                    max_err = e;
            }
            CHECK(max_err < 1e-5);
            treeweave_free(h);
        }
    }
    /* 1D -> 2 vector (input spelled as 1D, output_dim == 2) */
    {
        const double a = 0.0, b = 1.0;
        treeweave_t  h = treeweave_fit(k_1d_2, 1, 2, &a, &b, 1e-9, NULL, NULL);
        CHECK(h != NULL);
        if (h != NULL) {
            CHECK(treeweave_output_dim(h) == 2);
            double       max_err = 0.0;
            unsigned int seed    = 11U;
            for (int i = 0; i < 200; ++i) {
                const double x    = next_unit(&seed);
                double       y[2] = {0};
                treeweave_eval(h, &x, y);
                double exact[2] = {0};
                k_1d_2(&x, exact, NULL);
                if (fabs(y[0] - exact[0]) > max_err)
                    max_err = fabs(y[0] - exact[0]);
                if (fabs(y[1] - exact[1]) > max_err)
                    max_err = fabs(y[1] - exact[1]);
            }
            CHECK(max_err < 1e-5);
            treeweave_free(h);
        }
    }
}

/* ---- f32 path ------------------------------------------------------- */

static void test_f32_path(void) {
    const float a = 0.0F, b = 1.0F;
    treeweave_t h = treeweavef_fit(k_1d_1f, 1, 1, &a, &b, 1e-5, NULL, NULL);
    CHECK(h != NULL);
    if (h == NULL)
        return;
    CHECK(treeweave_dtype(h) == TREEWEAVE_F32);

    enum { N = 128 };
    float xs[N], multi[N];
    for (int i = 0; i < N; ++i)
        xs[i] = (float)i / (float)N;
    treeweavef_batch(h, xs, multi, N);

    float max_err = 0.0F, max_parity = 0.0F, max_mag = 1.0F;
    for (int i = 0; i < N; ++i) {
        float y = 0.0F;
        treeweavef_eval(h, &xs[i], &y);
        const float ce = fabsf(y - exact_1d_1f(xs[i]));
        if (ce > max_err)
            max_err = ce;
        if (fabsf(multi[i]) > max_mag)
            max_mag = fabsf(multi[i]);
        const float pe = fabsf(y - multi[i]);
        if (pe > max_parity)
            max_parity = pe;
    }
    CHECK(max_err < 1e-3F);
    /* See the f64 case above: scalar vs batch agree to a few ULP, bit-exactly
     * only where hardware FMA is present. */
    CHECK(max_parity <= 16.0F * FLT_EPSILON * max_mag);
    treeweave_free(h);
}

/* ---- domain edges: closed upper endpoint, OOD -> NaN ---------------- *
 * The eval domain is closed at the upper corner `b` and open below `a`:
 *   x == b   -> the last leaf's polynomial (boundary value), not NaN
 *   x <  a   -> NaN
 *   x >  b   -> NaN (finite far-high points are out-of-domain on every
 *               path: scalar, batch and sorted all agree). */

static void test_out_of_domain_nan(void) {
    const double a = 0.0, b = 1.0;
    treeweave_t  h = treeweave_fit(k_1d_1, 1, 1, &a, &b, 1e-9, NULL, NULL);
    CHECK(h != NULL);
    if (h == NULL)
        return;

    /* OOD-low scalar -> NaN. */
    double xlo = a - 1.0, ylo = 0.0;
    treeweave_eval(h, &xlo, &ylo);
    CHECK(isnan(ylo));

    /* Closed upper endpoint: x == b returns a finite boundary value. */
    double xb = b, yb = 0.0;
    treeweave_eval(h, &xb, &yb);
    CHECK(!isnan(yb));
    CHECK(fabs(yb - exact_1d_1(b)) < 1e-6);

    /* Batch: x < a -> NaN; interior -> finite; x == b -> finite (closed
     * upper endpoint); x > b -> NaN (out-of-domain, matches the scalar API). */
    double xs[4] = {a - 2.0, 0.25, b, b + 2.0};
    double ys[4] = {0};
    treeweave_batch(h, xs, ys, 4);
    CHECK(isnan(ys[0]));
    CHECK(!isnan(ys[1]));
    CHECK(!isnan(ys[2]));
    CHECK(fabs(ys[2] - exact_1d_1(b)) < 1e-6);
    CHECK(isnan(ys[3]));
    treeweave_free(h);

    /* f32 path: same closed-upper-endpoint behavior at x == b. */
    const float af = 0.0F, bf = 1.0F;
    treeweave_t hf = treeweavef_fit(k_1d_1f, 1, 1, &af, &bf, 1e-5, NULL, NULL);
    CHECK(hf != NULL);
    if (hf == NULL)
        return;
    float xbf = bf, ybf = 0.0F;
    treeweavef_eval(hf, &xbf, &ybf);
    CHECK(!isnan(ybf));
    CHECK(fabsf(ybf - exact_1d_1f(bf)) < 1e-3F);
    float xlof = af - 1.0F, ylof = 0.0F;
    treeweavef_eval(hf, &xlof, &ylof);
    CHECK(isnan(ylof));
    treeweave_free(hf);
}

/* ---- introspection / defaults --------------------------------------- */

static void test_introspection_and_defaults(void) {
    /* treeweave_default_opts() must match treeweave::options{} (verified in the
     * C++ surface; mirrored here as concrete values). */
    treeweave_opts defaults;
    treeweave_default_opts(&defaults);
    treeweave_default_opts(NULL);
    CHECK(defaults.tol_kind == TREEWEAVE_RELATIVE_MAX);
    CHECK(defaults.max_depth == 50);
    /* <0 = auto: a dimension-scaled budget resolved at fit time. */
    CHECK(defaults.max_memory_mib < 0);
    CHECK(defaults.allow_max_depth_leaves == 0);
    CHECK(defaults.min_uniform_depth == 0);

    /* NULL opts path must succeed (it selects the defaults). */
    const double a = 0.0, b = 1.0;
    treeweave_t  h = treeweave_fit(k_1d_1, 1, 1, &a, &b, 1e-9, NULL, NULL);
    CHECK(h != NULL);
    if (h == NULL)
        return;
    CHECK(treeweave_memory_usage(h) > 0);
    treeweave_print_stats(h); /* must run without crashing */
    treeweave_free(h);
}

/* ---- error paths ---------------------------------------------------- */

static void test_error_paths(void) {
    const double a = 0.0, b = 1.0;

    /* unsupported input_dim (4) -> NULL + error mentioning input_dim */
    {
        const double a4[4] = {0, 0, 0, 0}, b4[4] = {1, 1, 1, 1};
        treeweave_t  h = treeweave_fit(k_1d_1, 4, 1, a4, b4, 1e-9, NULL, NULL);
        CHECK(h == NULL);
        CHECK(strstr(treeweave_last_error(), "input_dim") != NULL);
    }
    /* tol <= 0 -> NULL + nonempty error */
    {
        treeweave_t h = treeweave_fit(k_1d_1, 1, 1, &a, &b, 0.0, NULL, NULL);
        CHECK(h == NULL);
        CHECK(strlen(treeweave_last_error()) > 0);
    }
    /* dtype mismatch: _f32 eval on an f64 handle leaves output untouched */
    {
        treeweave_t h = treeweave_fit(k_1d_1, 1, 1, &a, &b, 1e-9, NULL, NULL);
        CHECK(h != NULL);
        if (h != NULL) {
            float x = 0.5F, y = 42.0F;
            treeweavef_eval(h, &x, &y);
            CHECK(y == 42.0F);
            CHECK(strlen(treeweave_last_error()) > 0);
            treeweave_free(h);
        }
    }
    /* sorted on a 2D handle -> no write + error mentioning input_dim */
    {
        const double a2[2] = {0.0, 0.0}, b2[2] = {1.0, 1.0};
        treeweave_t  h = treeweave_fit(k_2d_1, 2, 1, a2, b2, 1e-8, NULL, NULL);
        CHECK(h != NULL);
        if (h != NULL) {
            double y = 123.0;
            treeweave_sorted(h, a2, &y, 1);
            CHECK(y == 123.0);
            CHECK(strstr(treeweave_last_error(), "input_dim") != NULL);
            treeweave_free(h);
        }
    }
    /* SoA on an output_dim == 1 handle -> no write + error mentioning output_dim */
    {
        treeweave_t h = treeweave_fit(k_1d_1, 1, 1, &a, &b, 1e-9, NULL, NULL);
        CHECK(h != NULL);
        if (h != NULL) {
            double  x = 0.5, c0 = 7.0;
            double *soa[1] = {&c0};
            treeweave_transposed(h, &x, soa, 1);
            CHECK(c0 == 7.0);
            CHECK(strstr(treeweave_last_error(), "output_dim") != NULL);
            treeweave_free(h);
        }
    }
    /* null-handle eval -> no crash + nonempty error */
    {
        double x = 0.0, y = 0.0;
        treeweave_eval(NULL, &x, &y);
        CHECK(strlen(treeweave_last_error()) > 0);
    }
    /* free(NULL) is a no-op returning NULL */
    CHECK(treeweave_free(NULL) == NULL);
}

/* ---- by-value scalar eval matches the pointer API ------------------- */

static void test_by_value_eval(void) {
    const double a1 = 0.0, b1 = 1.0;
    treeweave_t  h1    = treeweave_fit(k_1d_1, 1, 1, &a1, &b1, 1e-9, NULL, NULL);
    const double a2[2] = {0.0, 0.0}, b2[2] = {1.0, 1.0};
    treeweave_t  h2    = treeweave_fit(k_2d_1, 2, 1, a2, b2, 1e-9, NULL, NULL);
    const double a3[3] = {0.0, 0.0, 0.0}, b3[3] = {1.0, 1.0, 1.0};
    treeweave_t  h3  = treeweave_fit(k_3d_1, 3, 1, a3, b3, 1e-9, NULL, NULL);
    const float  a1f = 0.0F, b1f = 1.0F;
    treeweave_t  h1f = treeweavef_fit(k_1d_1f, 1, 1, &a1f, &b1f, 1e-5, NULL, NULL);
    CHECK(h1 != NULL && h2 != NULL && h3 != NULL && h1f != NULL);
    if (h1 == NULL || h2 == NULL || h3 == NULL || h1f == NULL)
        return;

    /* By-value result must equal the pointer-API result exactly (same path). */
    for (int i = 0; i <= 8; ++i) {
        const double t = (double)i / 8.0;

        double x1 = t, y1 = 0.0;
        treeweave_eval(h1, &x1, &y1);
        CHECK(treeweave_eval_1d(h1, t) == y1 || (isnan(treeweave_eval_1d(h1, t)) && isnan(y1)));

        double x2[2] = {t, 1.0 - t}, y2 = 0.0;
        treeweave_eval(h2, x2, &y2);
        CHECK(treeweave_eval_2d(h2, x2[0], x2[1]) == y2);

        double x3[3] = {t, 1.0 - t, 0.5 * t}, y3 = 0.0;
        treeweave_eval(h3, x3, &y3);
        CHECK(treeweave_eval_3d(h3, x3[0], x3[1], x3[2]) == y3);

        float xf = (float)t, yf = 0.0F;
        treeweavef_eval(h1f, &xf, &yf);
        CHECK(treeweavef_eval_1d(h1f, xf) == yf || (isnan(treeweavef_eval_1d(h1f, xf)) && isnan(yf)));
    }

    /* Closed upper endpoint via the by-value API. */
    CHECK(!isnan(treeweave_eval_1d(h1, b1)));
    CHECK(fabs(treeweave_eval_1d(h1, b1) - exact_1d_1(b1)) < 1e-6);

    /* Arity / scalar-output mismatch -> NaN + last_error set. */
    CHECK(isnan(treeweave_eval_2d(h1, 0.5, 0.5))); /* 1-D handle, 2-D call */
    CHECK(treeweave_last_error()[0] != '\0');
    CHECK(isnan(treeweave_eval_1d(NULL, 0.5))); /* null handle */

    treeweave_free(h1);
    treeweave_free(h2);
    treeweave_free(h3);
    treeweave_free(h1f);
}

/* ---- X2: NULL-handle introspection is safe ----------------------------- */

static void test_null_introspection(void) {
    /* treeweave_dtype/input_dim/output_dim must tolerate a NULL handle:
     * return a safe sentinel (dtype=TREEWEAVE_F64=0, dim=0) and set
     * last_error, consistent with treeweave_memory_usage. */
    treeweave_dtype_t dt = treeweave_dtype(NULL);
    CHECK(dt == TREEWEAVE_F64); /* sentinel: zero enumerator */
    CHECK(strlen(treeweave_last_error()) > 0);

    int indim = treeweave_input_dim(NULL);
    CHECK(indim == 0);
    CHECK(strlen(treeweave_last_error()) > 0);

    int outdim = treeweave_output_dim(NULL);
    CHECK(outdim == 0);
    CHECK(strlen(treeweave_last_error()) > 0);

    /* These were already NULL-safe; confirm they still are. */
    CHECK(treeweave_memory_usage(NULL) == 0);
    CHECK(treeweave_free(NULL) == NULL);
}

/* ---- X3/G8: by-value scalar eval agrees with buffer-based eval --------- *
 * (A dedicated test beyond test_by_value_eval: exercise all 6 entry points
 *  on in-domain points and confirm the results are bit-identical.) */

static void k_1d_1f_v(const float *x, float *y, void *d) {
    (void)d;
    y[0] = expf(0.5F * x[0]) + sinf(3.0F * x[0]);
}
static void k_2d_1f(const float *x, float *y, void *d) {
    (void)d;
    y[0] = expf(0.3F * x[0]) + sinf(2.0F * x[1]);
}
static void k_3d_1f(const float *x, float *y, void *d) {
    (void)d;
    y[0] = expf(0.2F * x[0]) + sinf(x[1]) + cosf(x[2]);
}
static void k_2d_1_d(const double *x, double *y, void *d) {
    (void)d;
    y[0] = exp(0.3 * x[0]) + sin(2.0 * x[1]);
}
static void k_3d_1_d(const double *x, double *y, void *d) {
    (void)d;
    y[0] = exp(0.2 * x[0]) + sin(x[1]) + cos(x[2]);
}

static void test_by_value_all_arities(void) {
    /* f64 handles */
    const double a1 = 0.1, b1 = 0.9;
    treeweave_t  h1d = treeweave_fit(k_1d_1, 1, 1, &a1, &b1, 1e-9, NULL, NULL);

    const double a2[2] = {0.1, 0.1}, b2[2] = {0.9, 0.9};
    treeweave_t  h2d = treeweave_fit(k_2d_1_d, 2, 1, a2, b2, 1e-9, NULL, NULL);

    const double a3[3] = {0.1, 0.1, 0.1}, b3[3] = {0.9, 0.9, 0.9};
    treeweave_t  h3d = treeweave_fit(k_3d_1_d, 3, 1, a3, b3, 1e-9, NULL, NULL);

    /* f32 handles */
    const float af1 = 0.1F, bf1 = 0.9F;
    treeweave_t h1f = treeweavef_fit(k_1d_1f_v, 1, 1, &af1, &bf1, 1e-5, NULL, NULL);

    const float af2[2] = {0.1F, 0.1F}, bf2[2] = {0.9F, 0.9F};
    treeweave_t h2f = treeweavef_fit(k_2d_1f, 2, 1, af2, bf2, 1e-5, NULL, NULL);

    const float af3[3] = {0.1F, 0.1F, 0.1F}, bf3[3] = {0.9F, 0.9F, 0.9F};
    treeweave_t h3f = treeweavef_fit(k_3d_1f, 3, 1, af3, bf3, 1e-5, NULL, NULL);

    CHECK(h1d != NULL && h2d != NULL && h3d != NULL);
    CHECK(h1f != NULL && h2f != NULL && h3f != NULL);
    if (h1d == NULL || h2d == NULL || h3d == NULL || h1f == NULL || h2f == NULL || h3f == NULL)
        goto cleanup;

    {
        /* Test each by-value entry point against the buffer API at a handful
         * of in-domain points; the two code paths are identical so results
         * must be bit-exact. */
        const double pts1d[5] = {0.2, 0.35, 0.5, 0.65, 0.8};
        for (int i = 0; i < 5; ++i) {
            double yref = 0.0;
            treeweave_eval(h1d, &pts1d[i], &yref);
            double yval = treeweave_eval_1d(h1d, pts1d[i]);
            CHECK(yval == yref || (isnan(yval) && isnan(yref)));
        }

        const double pts2d[5][2] = {{0.2, 0.3}, {0.4, 0.5}, {0.6, 0.7}, {0.3, 0.8}, {0.5, 0.5}};
        for (int i = 0; i < 5; ++i) {
            double yref = 0.0;
            treeweave_eval(h2d, pts2d[i], &yref);
            double yval = treeweave_eval_2d(h2d, pts2d[i][0], pts2d[i][1]);
            CHECK(yval == yref || (isnan(yval) && isnan(yref)));
        }

        const double pts3d[5][3] = {
            {0.2, 0.3, 0.4}, {0.5, 0.5, 0.5}, {0.6, 0.7, 0.8}, {0.3, 0.2, 0.1}, {0.4, 0.6, 0.5}};
        for (int i = 0; i < 5; ++i) {
            double yref = 0.0;
            treeweave_eval(h3d, pts3d[i], &yref);
            double yval = treeweave_eval_3d(h3d, pts3d[i][0], pts3d[i][1], pts3d[i][2]);
            CHECK(yval == yref || (isnan(yval) && isnan(yref)));
        }

        /* f32 */
        const float pts1f[5] = {0.2F, 0.35F, 0.5F, 0.65F, 0.8F};
        for (int i = 0; i < 5; ++i) {
            float yref = 0.0F;
            treeweavef_eval(h1f, &pts1f[i], &yref);
            float yval = treeweavef_eval_1d(h1f, pts1f[i]);
            CHECK(yval == yref || (isnan(yval) && isnan(yref)));
        }

        const float pts2f[5][2] = {{0.2F, 0.3F}, {0.4F, 0.5F}, {0.6F, 0.7F}, {0.3F, 0.8F}, {0.5F, 0.5F}};
        for (int i = 0; i < 5; ++i) {
            float yref = 0.0F;
            treeweavef_eval(h2f, pts2f[i], &yref);
            float yval = treeweavef_eval_2d(h2f, pts2f[i][0], pts2f[i][1]);
            CHECK(yval == yref || (isnan(yval) && isnan(yref)));
        }

        const float pts3f[5][3] = {
            {0.2F, 0.3F, 0.4F}, {0.5F, 0.5F, 0.5F}, {0.6F, 0.7F, 0.8F}, {0.3F, 0.2F, 0.1F}, {0.4F, 0.6F, 0.5F}};
        for (int i = 0; i < 5; ++i) {
            float yref = 0.0F;
            treeweavef_eval(h3f, pts3f[i], &yref);
            float yval = treeweavef_eval_3d(h3f, pts3f[i][0], pts3f[i][1], pts3f[i][2]);
            CHECK(yval == yref || (isnan(yval) && isnan(yref)));
        }
    }

cleanup:
    treeweave_free(h1d);
    treeweave_free(h2d);
    treeweave_free(h3d);
    treeweave_free(h1f);
    treeweave_free(h2f);
    treeweave_free(h3f);
}

/* ---- G1: OOD contract, below-a -> NaN, above-b -> NaN, at-b -> finite -
 * Both scalar (treeweave_eval) and batch (treeweave_batch) tag all points
 * outside [a, b] as out-of-domain and NaN-fill them.  The one asymmetry is
 * that exactly-at-b returns the last leaf's boundary value (finite), not NaN.
 * (The batch path documents this in Function::operator(): "The OOD bucket is
 * filled with NaN instead of evaluated.") */
static void test_ood_contract(void) {
    const double a = 0.0, b = 1.0;
    treeweave_t  h = treeweave_fit(k_1d_1, 1, 1, &a, &b, 1e-9, NULL, NULL);
    CHECK(h != NULL);
    if (h == NULL)
        return;

    /* scalar path */
    double xlo = a - 1.0, ylo = 0.0;
    treeweave_eval(h, &xlo, &ylo);
    CHECK(isnan(ylo)); /* below a -> NaN */

    double xhi = b + 1.0, yhi = 0.0;
    treeweave_eval(h, &xhi, &yhi);
    CHECK(isnan(yhi)); /* above b -> NaN */

    double xb = b, yb = 0.0;
    treeweave_eval(h, &xb, &yb);
    CHECK(!isnan(yb)); /* exactly at b -> finite */

    /* batch path: same contract. */
    double xs[5] = {a - 2.0, a, 0.5, b, b + 5.0};
    double ys[5] = {0.0, 0.0, 0.0, 0.0, 0.0};
    treeweave_batch(h, xs, ys, 5);
    CHECK(isnan(ys[0]));  /* below a -> NaN */
    CHECK(!isnan(ys[1])); /* exactly at a -> finite (interior) */
    CHECK(!isnan(ys[2])); /* interior -> finite */
    CHECK(!isnan(ys[3])); /* exactly at b -> finite */
    CHECK(isnan(ys[4]));  /* above b -> NaN */

    treeweave_free(h);
}

/* ---- G2: NaN input in 2D and 3D returns NaN (per-output) -------------- */

static void test_nan_input_2d_3d(void) {
    /* 2D */
    {
        const double a[2] = {0.0, 0.0}, b[2] = {1.0, 1.0};
        treeweave_t  h = treeweave_fit(k_2d_1, 2, 1, a, b, 1e-8, NULL, NULL);
        CHECK(h != NULL);
        if (h != NULL) {
            /* NaN in first coordinate */
            const double x0[2] = {NAN, 0.5}; /* NaN, 0.5 */
            double       y0    = 0.0;
            treeweave_eval(h, x0, &y0);
            CHECK(isnan(y0));

            /* NaN in second coordinate */
            const double x1[2] = {0.5, NAN}; /* 0.5, NaN */
            double       y1    = 0.0;
            treeweave_eval(h, x1, &y1);
            CHECK(isnan(y1));

            treeweave_free(h);
        }
    }
    /* 3D */
    {
        const double a[3] = {0.0, 0.0, 0.0}, b[3] = {1.0, 1.0, 1.0};
        treeweave_t  h = treeweave_fit(k_3d_1, 3, 1, a, b, 1e-7, NULL, NULL);
        CHECK(h != NULL);
        if (h != NULL) {
            /* NaN in middle coordinate */
            const double xm[3] = {0.5, NAN, 0.5};
            double       ym    = 0.0;
            treeweave_eval(h, xm, &ym);
            CHECK(isnan(ym));

            treeweave_free(h);
        }
    }
}

/* ---- G10: AbsoluteMax tol_kind through the C ABI ----------------------- */

static void test_absolute_max_tol(void) {
    treeweave_opts opts;
    treeweave_default_opts(&opts);
    opts.tol_kind = TREEWEAVE_ABSOLUTE_MAX;

    const double tol = 1e-6;
    const double a = 0.0, b = 1.0;
    treeweave_t  h = treeweave_fit(k_1d_1, 1, 1, &a, &b, tol, NULL, &opts);
    CHECK(h != NULL);
    if (h == NULL)
        return;

    /* Verify the fit meets the ABSOLUTE (not relative) tolerance. */
    double       max_abs_err = 0.0;
    unsigned int seed        = 77U;
    for (int i = 0; i < 300; ++i) {
        const double x = next_unit(&seed);
        double       y = 0.0;
        treeweave_eval(h, &x, &y);
        const double e = fabs(y - exact_1d_1(x));
        if (e > max_abs_err)
            max_abs_err = e;
    }
    CHECK(max_abs_err < tol * 100.0);
    treeweave_free(h);
}

/* ---- G6: allow_max_depth_leaves option through the C ABI --------------- *
 * With allow_max_depth_leaves=1 the fit must NOT throw even if max_depth
 * is reached; it accepts approximate leaves and returns a valid handle. */

static void k_rough(const double *x, double *y, void *d) {
    /* A rapidly-oscillating function that forces deep subdivision. */
    (void)d;
    y[0] = sin(200.0 * x[0]);
}

static void test_allow_max_depth_leaves(void) {
    treeweave_opts opts;
    treeweave_default_opts(&opts);
    opts.max_depth              = 5; /* very shallow, hits the cap */
    opts.allow_max_depth_leaves = 1;

    const double a = 0.0, b = 1.0;
    /* Tight tol on a high-frequency function will hit max_depth.
     * With allow_max_depth_leaves=1, the fit should succeed (not NULL). */
    treeweave_t h = treeweave_fit(k_rough, 1, 1, &a, &b, 1e-12, NULL, &opts);
    CHECK(h != NULL);
    if (h != NULL) {
        /* The returned handle must be evaluable. */
        const double x = 0.5;
        double       y = 0.0;
        treeweave_eval(h, &x, &y);
        CHECK(!isnan(y));
        treeweave_free(h);
    }
}

/* ---- G7: max_memory_mib = 0 disables the memory cap ------------------- *
 * Per the treeweave_opts doc and the C source: <0 = auto (4/8/16 MiB),
 * 0 = no cap, >0 = explicit cap. With 0 the fit must not fail with a
 * MemoryBudgetExceeded error on a simple function. */

static void test_max_memory_mib_zero(void) {
    treeweave_opts opts;
    treeweave_default_opts(&opts);
    opts.max_memory_mib = 0; /* no cap */

    const double a = 0.0, b = 1.0;
    treeweave_t  h = treeweave_fit(k_1d_1, 1, 1, &a, &b, 1e-9, NULL, &opts);
    /* With no memory cap, a simple fit must succeed. */
    CHECK(h != NULL);
    CHECK(strlen(treeweave_last_error()) == 0);
    if (h != NULL) {
        double x = 0.5, y = 0.0;
        treeweave_eval(h, &x, &y);
        CHECK(!isnan(y));
        treeweave_free(h);
    }
}

/* ---- G8: max_depth exceeded fails the fit through the C ABI ------------ *
 * Same rough function as G6 with allow_max_depth_leaves=0 (the default):
 * the fit must return NULL and last_error must carry MaxDepthExceeded's
 * message. Exercises the exception path of the baseline-compiled factory
 * TUs of the multi-arch build. */

static void test_max_depth_exceeded_error(void) {
    treeweave_opts opts;
    treeweave_default_opts(&opts);
    opts.max_depth = 5; /* very shallow, hits the cap */

    const double a = 0.0, b = 1.0;
    treeweave_t  h = treeweave_fit(k_rough, 1, 1, &a, &b, 1e-12, NULL, &opts);
    CHECK(h == NULL);
    CHECK(strstr(treeweave_last_error(), "depth exceeded") != NULL);
}

/* ---- G9: memory budget exceeded fails the fit through the C ABI -------- *
 * A frequency high enough that a 1 MiB leaf budget trips before the tree
 * converges or reaches max_depth: the fit must return NULL and last_error
 * must carry MemoryBudgetExceeded's message. Exercises the other
 * exception path of the baseline-compiled factory TUs. */

static void k_very_rough(const double *x, double *y, void *d) {
    (void)d;
    y[0] = sin(20000.0 * x[0]);
}

static void test_memory_budget_exceeded_error(void) {
    treeweave_opts opts;
    treeweave_default_opts(&opts);
    opts.max_memory_mib = 1;

    const double a = 0.0, b = 1.0;
    treeweave_t  h = treeweave_fit(k_very_rough, 1, 1, &a, &b, 1e-12, NULL, &opts);
    CHECK(h == NULL);
    CHECK(strstr(treeweave_last_error(), "exceeded budget") != NULL);
}

/* ---- G11: (dim=1, out_dim=3) shape fit + eval -------------------------- */

static void k_1d_3(const double *x, double *y, void *d) {
    (void)d;
    y[0] = sin(x[0]);
    y[1] = cos(x[0]);
    y[2] = exp(0.5 * x[0]);
}

static void test_1d_3out_shape(void) {
    const double a = 0.0, b = 1.0;
    treeweave_t  h = treeweave_fit(k_1d_3, 1, 3, &a, &b, 1e-8, NULL, NULL);
    CHECK(h != NULL);
    if (h == NULL)
        return;
    CHECK(treeweave_input_dim(h) == 1);
    CHECK(treeweave_output_dim(h) == 3);

    double       max_err = 0.0;
    unsigned int seed    = 55U;
    for (int i = 0; i < 200; ++i) {
        const double x    = next_unit(&seed);
        double       y[3] = {0};
        treeweave_eval(h, &x, y);
        const double e0 = fabs(y[0] - sin(x));
        const double e1 = fabs(y[1] - cos(x));
        const double e2 = fabs(y[2] - exp(0.5 * x));
        if (e0 > max_err)
            max_err = e0;
        if (e1 > max_err)
            max_err = e1;
        if (e2 > max_err)
            max_err = e2;
    }
    CHECK(max_err < 1e-5);
    treeweave_free(h);
}

/* ---- G16: n == 0 batch eval is a clean no-op (no OOB, no crash) -------- */

static void test_zero_batch_noop(void) {
    const double a = 0.0, b = 1.0;
    treeweave_t  h = treeweave_fit(k_1d_1, 1, 1, &a, &b, 1e-9, NULL, NULL);
    CHECK(h != NULL);
    if (h == NULL)
        return;

    /* n=0: must not read or write any data, must not crash. */
    /* Pass a non-null but tiny buffer to catch any off-by-one OOB. */
    double x_sentinel = 0.5, y_sentinel = 0.0;
    treeweave_batch(h, &x_sentinel, &y_sentinel, 0);
    /* Output must be untouched by a zero-length batch. */
    CHECK(y_sentinel == 0.0);

    /* sorted and transposed n=0 as well. */
    treeweave_sorted(h, &x_sentinel, &y_sentinel, 0);
    CHECK(y_sentinel == 0.0);

    /* f32 path. */
    const float af = 0.0F, bf = 1.0F;
    treeweave_t hf = treeweavef_fit(k_1d_1f, 1, 1, &af, &bf, 1e-5, NULL, NULL);
    CHECK(hf != NULL);
    if (hf != NULL) {
        float xf = 0.5F, yf = 0.0F;
        treeweavef_batch(hf, &xf, &yf, 0);
        CHECK(yf == 0.0F);
        treeweave_free(hf);
    }

    treeweave_free(h);
}

/* ---- G3: f32 sorted fast path through the C ABI ------------------------ *
 * treeweave_sorted / treeweavef_sorted are C-ABI entry points (confirmed in
 * include/treeweave.h lines 168-170). Verify that the f32 sorted path
 * agrees with the f32 general-batch path on the same sorted input. */

static void test_f32_sorted_fast_path(void) {
    const float af = 0.0F, bf = 1.0F;
    treeweave_t h = treeweavef_fit(k_1d_1f, 1, 1, &af, &bf, 1e-5, NULL, NULL);
    CHECK(h != NULL);
    if (h == NULL)
        return;

    enum { N = 200 };
    float xs[N], multi[N], sorted_res[N];
    /* Build a sorted sequence of in-domain points. */
    for (int i = 0; i < N; ++i)
        xs[i] = af + (bf - af) * (float)i / (float)(N - 1);

    treeweavef_batch(h, xs, multi, N);
    treeweavef_sorted(h, xs, sorted_res, N);

    /* Sorted and batch paths must produce identical results on sorted input. */
    for (int i = 0; i < N; ++i)
        CHECK(sorted_res[i] == multi[i]);

    treeweave_free(h);
}

int main(void) {
    test_1d_scalar_auto_degree();
    test_auto_degree_meets_tol();
    test_sorted_matches_multi();
    test_soa_matches_aos();
    test_higher_dim_fits();
    test_f32_path();
    test_out_of_domain_nan();
    test_by_value_eval();
    test_introspection_and_defaults();
    test_error_paths();

    /* New tests (audit coverage gaps) */
    test_null_introspection();
    test_by_value_all_arities();
    test_ood_contract();
    test_nan_input_2d_3d();
    test_absolute_max_tol();
    test_allow_max_depth_leaves();
    test_max_memory_mib_zero();
    test_max_depth_exceeded_error();
    test_memory_budget_exceeded_error();
    test_1d_3out_shape();
    test_zero_batch_noop();
    test_f32_sorted_fast_path();

    printf("%d failures\n", g_failures);
    return g_failures;
}
