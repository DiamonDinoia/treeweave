"""zeta_bench.py: treeweave vs a fair brute-force Riemann-zeta eval.

See benchmarks/zeta_bench.cpp for the rationale. zeta(s) = sum_k k**-s summed
until the tail is negligible (rel 1e-10, <=160 terms) yet smooth on [2, 10]: fit
once, eval a polynomial. Times single/multi/sorted; the native rate is sampled
over n_native and reused. numpy Chebyshev is the native interpolator baseline.
TREEWEAVE_BENCH_YAML=path emits YAML. NumPy only (no SciPy).
"""

import os
import time

import numpy as np

import treeweave

a, b = 2.0, 10.0

# Fair baseline: sum k**-s until a term is below EPS relative to the running
# total, capped at MAX_TERMS: a competent zeta stops early once the tail is
# negligible, so this is an honest cost rather than a fixed-iteration strawman.
EPS = 1e-10
MAX_TERMS = 160


def zeta_partial(s):
    acc = 0.0
    for k in range(1, MAX_TERMS + 1):
        term = k ** (-s)
        acc += term
        if term < EPS * acc:
            break
    return acc


# Fit target: all MAX_TERMS terms. The early stop above makes zeta_partial jump
# by up to EPS relative where the term count changes, and a global Chebyshev
# series then needs degree 177 instead of 26 on [2, 10]. Every interpolator,
# treeweave included, fits this smooth sum; the native rate uses zeta_partial.
KS = np.arange(1.0, MAX_TERMS + 1.0)


def zeta_smooth(s):
    """Vectorized over s: one row of k**-s per point, summed over k."""
    return (KS[:, None] ** -np.atleast_1d(s)).sum(axis=0)


fn = treeweave.fit(lambda x: float(zeta_smooth(x[0])[0]), a, b, tol=1e-10)
print(fn)

n = 1_000_000  # batch / sorted points
n_scalar = 100_000  # scalar-API points
n_native = 256  # brute-force sample (<=160-term sum each)
rng = np.random.default_rng(7)
xs = rng.uniform(a, b, n)
xs_sorted = np.sort(xs)


def mevals(count, seconds):
    return count / (seconds * 1e6)


xs_native = xs[:n_native]

# --- native rate: brute-force sum over the small sample (mode-independent) ---
sink = 0.0
for s in xs_native:  # warm-up
    sink += zeta_partial(s)
t0 = time.perf_counter()
for s in xs_native:
    sink += zeta_partial(s)
nat_s = time.perf_counter() - t0
assert np.isfinite(sink)
nat_rate = mevals(n_native, nat_s)  # Mevals/s, reused in every mode

# --- single-eval: the scalar API, one point at a time ------------------------
xs_scalar = xs[:n_scalar]
sink = 0.0
for x in xs_scalar:  # warm-up
    sink += fn(float(x))
t0 = time.perf_counter()
for x in xs_scalar:
    sink += fn(float(x))
tw_single_s = time.perf_counter() - t0
assert np.isfinite(sink)

# --- multi-eval: the unsorted batch (in-place, allocation-free) --------------
tw_buf = np.empty(n)
fn(xs, out=tw_buf)  # warm-up
t0 = time.perf_counter()
fn(xs, out=tw_buf)
tw_multi_s = time.perf_counter() - t0
assert np.isfinite(tw_buf.sum())

# --- sorted-eval: the 1-D ascending fast path --------------------------------
fn(xs_sorted, sorted=True, out=tw_buf)  # warm-up
t0 = time.perf_counter()
fn(xs_sorted, sorted=True, out=tw_buf)
tw_sorted_s = time.perf_counter() - t0
assert np.isfinite(tw_buf.sum())

# --- accuracy of every method, treeweave included, on one check sample ------
# The criterion is treeweave's default RelativeMax, max|p - f| <= tol * max|f|;
# the report also prints the pointwise max |p - f| / |f| for every method.
tol = 1e-10
xs_check = xs[:4096]
ref_check = zeta_smooth(xs_check)


def rel_errors(p):
    err = np.abs(p(xs_check) - ref_check)
    return err.max() / np.abs(ref_check).max(), float(np.max(err / np.abs(ref_check)))


tw_relmax, max_rel = rel_errors(fn)


def time_numpy_api(p):
    """single: the scalar call per point; multi/sorted: one vectorized call."""
    sink = 0.0
    for x in xs_scalar:  # warm-up
        sink += p(float(x))
    t0 = time.perf_counter()
    for x in xs_scalar:
        sink += p(float(x))
    single = mevals(n_scalar, time.perf_counter() - t0)
    assert np.isfinite(sink)
    rates = [single]
    for pts in (xs, xs_sorted):
        p(pts)  # warm-up
        t0 = time.perf_counter()
        y = p(pts)
        rates.append(mevals(n, time.perf_counter() - t0))
        assert np.isfinite(y.sum())
    return rates


baselines = []
# numpy Chebyshev.interpolate: degree-d series from d + 1 Chebyshev nodes. Grow
# the degree by one until it meets tol: the smallest series that does.
for deg in range(2, 1001):
    cheb = np.polynomial.Chebyshev.interpolate(zeta_smooth, deg, domain=[a, b])
    crit, max_rel_cheb = rel_errors(cheb)
    if crit <= tol:
        break
else:
    raise SystemExit(f"numpy Chebyshev never met tol {tol}")
baselines.append(
    ("numpy Chebyshev", f"degree {deg}", crit, max_rel_cheb, *time_numpy_api(cheb))
)

# --- throughput (Mevals/s) and speedup per mode ------------------------------
tw_single = mevals(n_scalar, tw_single_s)
tw_multi = mevals(n, tw_multi_s)
tw_sorted = mevals(n, tw_sorted_s)

print(
    f"zeta(s) = sum_k k^-s (<={MAX_TERMS} terms, stop at {EPS:.0e} rel), "
    f"fit on [{a:.1f}, {b:.1f}], relative tol 1e-10"
)
print(f"  max rel err: {max_rel:.3e}")
print(
    f"  single-eval  treeweave {tw_single:.1f}  native {nat_rate:.4f} Mevals/s  speedup {tw_single / nat_rate:.1f}x"
)
print(
    f"  multi-eval   treeweave {tw_multi:.1f}  native {nat_rate:.4f} Mevals/s  speedup {tw_multi / nat_rate:.1f}x"
)
print(
    f"  sorted-eval  treeweave {tw_sorted:.1f}  native {nat_rate:.4f} Mevals/s  speedup {tw_sorted / nat_rate:.1f}x"
)
for name, size, relmax, err, single, multi, srt in baselines:
    print(
        f"  {name} ({size}, max rel err {err:.3e})  single {single:.2f}  multi {multi:.2f}  sorted {srt:.2f} Mevals/s"
    )

# --- machine-readable YAML (optional) ----------------------------------------
# Hand-written (no PyYAML): %.17e carries a '.', so YAML 1.1 reads a float.
yaml_path = os.environ.get("TREEWEAVE_BENCH_YAML")
if yaml_path:

    def block(name, tw, nat):
        return (
            f"{name}:\n"
            f"  treeweave_mevals_s: {tw:.17e}\n"
            f"  native_mevals_s: {nat:.17e}\n"
            f"  speedup: {tw / nat:.17e}\n"
        )

    doc = (
        'language: "python"\n'
        f"domain: [{a:.17e}, {b:.17e}]\n"
        f"tol: {1e-10:.17e}\n"
        f"n_pts: {n}\n"
        f"relmax_err: {tw_relmax:.17e}\n"
        f"max_rel_err: {max_rel:.17e}\n"
        + block("single_eval", tw_single, nat_rate)
        + block("multi_eval", tw_multi, nat_rate)
        + block("sorted_eval", tw_sorted, nat_rate)
        + "interp:\n"
        + "".join(
            f'  - name: "{name}"\n'
            f'    size: "{size}"\n'
            f"    relmax_err: {relmax:.17e}\n"
            f"    max_rel_err: {err:.17e}\n"
            f"    single_eval: {single:.17e}\n"
            f"    multi_eval: {multi:.17e}\n"
            f"    sorted_eval: {srt:.17e}\n"
            for name, size, relmax, err, single, multi, srt in baselines
        )
    )
    with open(yaml_path, "w") as f:
        f.write(doc)
