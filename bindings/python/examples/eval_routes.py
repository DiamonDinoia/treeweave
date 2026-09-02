"""eval_routes.py: the four evaluation routes of a fitted object."""

import math
import numpy as np
import treeweave


def wave(x):
    return np.array([math.sin(x[0]), math.cos(x[0])])


# Fit wave(x) on [0, 5] syntax is fit(callback, lower_bound, upper_bound, tol).
approx = treeweave.fit(wave, 0.0, 5.0, tol=1e-9)
xs = np.linspace(0.0, 5.0, 1024)  # ascending, so the sorted route applies

# BEGIN DOCS_ROUTES
point = approx(3.5)  # single point -> (out_dim,)
batch = approx(xs)  # batch (N,)   -> (N, out_dim)
asc = approx(xs, sorted=True)  # xs promised non-decreasing (dim == 1)
cols = approx(xs, transposed=True)  # batch        -> (out_dim, N)
# END DOCS_ROUTES

assert point.shape == (2,)
assert batch.shape == (xs.size, 2)
assert asc.shape == batch.shape
assert cols.shape == (2, xs.size)
print(f"max |batch - sorted|     = {np.max(np.abs(batch - asc)):.3e} (expect 0)")
print(f"max |batch - transposed| = {np.max(np.abs(batch - cols.T)):.3e} (expect 0)")
assert np.array_equal(batch, asc)
assert np.array_equal(batch, cols.T)

exact = np.stack([np.sin(xs), np.cos(xs)], axis=1)
max_err = float(np.max(np.abs(batch - exact)))
print(f"max |approx - exact| over {xs.size} points: {max_err:.3e}")
assert max_err < 1e-8, f"too large: {max_err}"
print("OK")
