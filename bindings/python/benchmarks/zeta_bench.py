"""zeta_bench.py: treeweave vs a fair brute-force Riemann-zeta eval.

See examples/c++/zeta_bench.cpp for the rationale. zeta(s) = sum_k k**-s summed
until the tail is negligible (rel 1e-10, <=160 terms) yet smooth on [2, 10]: fit
once, eval a polynomial. Times single/multi/sorted; the native rate is sampled
over n_native and reused. TREEWEAVE_BENCH_YAML=path emits YAML. NumPy only (no SciPy).
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


# treeweave probes the scalar zeta_partial at Chebyshev nodes during the fit.
fn = treeweave.fit(lambda x: zeta_partial(x[0]), a, b, tol=1e-10)
print(fn)

n = 1_000_000  # batch / sorted points
n_scalar = 100_000  # scalar-API points
n_native = 256  # brute-force sample (<=160-term sum each)
rng = np.random.default_rng(7)
xs = rng.uniform(a, b, n)
xs_sorted = np.sort(xs)


def mevals(count, seconds):
    return count / (seconds * 1e6)


# --- accuracy vs the brute-force sum, on the n_native sample -----------------
xs_native = xs[:n_native]
yhat = fn(xs_native)
yref = np.array([zeta_partial(s) for s in xs_native])
max_rel = float(np.max(np.abs(yhat - yref) / np.abs(yref)))

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
        f"max_rel_err: {max_rel:.17e}\n"
        + block("single_eval", tw_single, nat_rate)
        + block("multi_eval", tw_multi, nat_rate)
        + block("sorted_eval", tw_sorted, nat_rate)
    )
    with open(yaml_path, "w") as f:
        f.write(doc)
