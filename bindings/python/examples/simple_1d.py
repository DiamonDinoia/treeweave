"""simple_1d.py — minimal 1D -> 1D fit and evaluation."""
import math
import numpy as np
import treeweave


def func(x):
    return math.exp(x[0])


# Fit exp(x) on [0, 1] syntax is fit(callback, lower_bound, upper_bound, tol).
approx = treeweave.fit(func, 0.0, 1.0, tol=1e-10)
print(approx)  # dtype, dim, out_dim, memory_usage

# The fit domain is [a, b); evaluating exactly at the upper corner b is allowed
# as a convenience and returns the boundary value (not NaN).
xs = np.linspace(0.0, 1.0, 11)  # includes the endpoint b = 1.0
# Evaluate approx on 11 points and print the maximum error.
ys = approx(xs)
exact = np.exp(xs)
max_err = float(np.max(np.abs(ys - exact)))
print(f"max |approx - exp| over 11 points: {max_err:.3e}")
assert max_err < 1e-8, f"too large: {max_err}"
print("OK")
