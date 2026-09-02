"""decorator_1d.py: @fit(a, b, tol) replaces a function with its approximation."""

import math
import numpy as np
import treeweave


# Same options as fit(f, a, b, tol), with the callable omitted.
@treeweave.fit(0.0, 1.0, tol=1e-10)
def expensive(x):
    """A slow callable, evaluated once per fit sample instead of per call."""
    return math.exp(x[0])


print(expensive)  # dtype, dim, out_dim, memory_usage

xs = np.linspace(0.0, 1.0, 11)
max_err = float(np.max(np.abs(expensive(xs) - np.exp(xs))))
print(f"max |approx - exp| over 11 points: {max_err:.3e}")
assert max_err < 1e-8, f"too large: {max_err}"


# Every keyword of fit() applies to the decorator form, in any dimension.
# BEGIN DOCS_DECORATOR_2D
@treeweave.fit([0.0, 0.0], [1.0, 1.0], tol=1e-6, dtype="f32")
def surface(x):
    return math.cos(x[0] - x[1])


# END DOCS_DECORATOR_2D

pts = np.array([[0.1, 0.9], [0.5, 0.5], [1.0, 0.0]])
surface_err = float(np.max(np.abs(surface(pts) - np.cos(pts[:, 0] - pts[:, 1]))))
print(f"max |surface - cos| over 3 points: {surface_err:.3e}")
assert surface_err < 1e-5, f"too large: {surface_err}"

# The undecorated function stays reachable, as with functools.cache.
assert expensive.__wrapped__(np.array([0.5])) == math.exp(0.5)
assert expensive.__name__ == "expensive"
print("OK")
