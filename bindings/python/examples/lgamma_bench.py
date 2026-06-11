"""lgamma_bench.py — treeweave vs scipy.special.gammaln (log-Gamma).

The Python member of the cross-language lgamma benchmark family (see
examples/c++/lgamma_bench.cpp for the rationale). log-Gamma is fit on [3, 50) —
smooth, positive, monotone, so relative error is well defined — with treeweave's
default RelativeMax tolerance. The fit callback is the scalar ``math.lgamma``;
the throughput baseline is the vectorized ``scipy.special.gammaln`` (the fair
comparison against treeweave's vectorized batch path).

Requires NumPy and SciPy:  pip install numpy scipy
"""

import math
import time

import numpy as np
from scipy.special import gammaln  # vectorized log-Gamma baseline

import treeweave

a, b = 3.0, 50.0
# treeweave probes the scalar math.lgamma at Chebyshev nodes during the fit.
approx = treeweave.fit(lambda x: math.lgamma(x[0]), a, b, tol=1e-10)
print(approx)

n = 1_000_000
rng = np.random.default_rng(7)
xs = rng.uniform(a, b, n)

# --- accuracy vs the library -------------------------------------------------
yhat = approx(xs)
yref = gammaln(xs)
max_rel = float(np.max(np.abs(yhat - yref) / np.abs(yref)))

# --- throughput: treeweave vs scipy.special.gammaln --------------------------
approx(xs)  # warm-up
t0 = time.perf_counter()
r = approx(xs)
tw_s = time.perf_counter() - t0

gammaln(xs)  # warm-up
t0 = time.perf_counter()
ell = gammaln(xs)
lib_s = time.perf_counter() - t0

assert np.isfinite(r.sum() + ell.sum())  # keep the timed calls live

print(f"lgamma fit on [{a:.1f}, {b:.1f}), relative tol 1e-10")
print(f"  max rel err: {max_rel:.3e}")
print(f"  treeweave:  {n / (tw_s * 1e6):.1f} Mevals/s")
print(f"  library (scipy.special.gammaln): {n / (lib_s * 1e6):.1f} Mevals/s")
print(f"  speedup: {lib_s / tw_s:.2f}x")
