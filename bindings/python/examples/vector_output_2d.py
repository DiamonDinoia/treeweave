"""vector_output_2d.py: 2D -> 3D vector-valued fit; AoS vs transposed parity."""
import math
import numpy as np
import treeweave


def func(x):
    return np.array([
        math.exp(0.3 * x[0]) + math.sin(2.0 * x[1]),
        math.cos(x[0] * x[1]) + 2.0,
        x[0] ** 2 + x[1] + 1.0,
    ])


# out_dim is inferred from a one-shot probe of func at the box midpoint.
# Fit func(x, y) on [0.2, 1.5]^2 syntax is fit(callback, lower_bound, upper_bound, tol).
approx = treeweave.fit(func, [0.2, 0.2], [1.5, 1.5], tol=1e-8)
print(approx)  # dtype, dim, out_dim, memory_usage

N = 64
rng = np.random.default_rng(0)
xs = rng.uniform([[0.2, 0.2]], [[1.5, 1.5]], size=(N, 2))

# Evaluate approx on random points and print AoS/transposed parity.
aos = approx(xs)                       # (N, out_dim)
tr = approx(xs, transposed=True)       # (out_dim, N)

max_diff = 0.0
for d in range(3):
    diff = float(np.max(np.abs(aos[:, d] - tr[d])))
    max_diff = max(max_diff, diff)

print(f"max |AoS - transposed| = {max_diff:.3e} (expect 0)")
assert max_diff == 0.0
print("OK")
