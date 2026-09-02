"""simple_2d.py: 2D -> 1D Gaussian bump fit."""
import math
import numpy as np
import treeweave


# BEGIN DOCS_MULTIDIM
def bump(x):
    return math.exp(-100.0 * (x[0] - 0.5) ** 2 - (x[1] - 0.5) ** 2)


# Fit bump(x, y) on [0, 1]^2 syntax is fit(callback, lower_bound, upper_bound, tol).
approx = treeweave.fit(bump, [0.0, 0.0], [1.0, 1.0], tol=1e-8)
xs = np.array([[0.5, 0.5], [0.25, 0.75], [0.9, 0.1]])
ys = approx(xs)  # shape (N, dim) -> (N,), or (N, out_dim) when out_dim > 1
# END DOCS_MULTIDIM

print(approx)  # dtype, dim, out_dim, memory_usage

max_err = 0.0
for x, y_approx in zip(xs, ys):
    y_exact = bump(x)
    err = abs(y_approx - y_exact)
    max_err = max(max_err, err)
    print(
        f"f({x[0]:.2f},{x[1]:.2f}) approx={y_approx:.10f} exact={y_exact:.10f} err={err:.2e}"
    )
assert max_err < 1e-7, f"too large: {max_err}"
print("OK")
