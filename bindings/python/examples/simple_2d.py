"""simple_2d.py — 2D -> 1D Gaussian bump fit."""
import math
import numpy as np
import treeweave


def bump(x):
    dx = x[0] - 0.5
    dy = x[1] - 0.5
    return math.exp(-100.0 * dx * dx - dy * dy)


approx = treeweave.fit(bump, [0.0, 0.0], [1.0, 1.0], tol=1e-8)
print(approx)  # dtype, dim, out_dim, memory_usage

xs = np.array([[0.5, 0.5], [0.25, 0.75], [0.9, 0.1]])
ys = approx(xs)
for i, (x, y_approx) in enumerate(zip(xs, ys)):
    y_exact = bump(x)
    print(f"f({x[0]:.2f},{x[1]:.2f}) approx={y_approx:.10f}  exact={y_exact:.10f}  err={abs(y_approx-y_exact):.2e}")
print("OK")
