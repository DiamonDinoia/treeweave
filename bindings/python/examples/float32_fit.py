"""float32_fit.py — f32 dtype fit and evaluation."""
import math
import numpy as np
import treeweave


def func(x):
    return float(math.sin(x[0]))


# dtype selects the precision of the *approximant* (stored coefficients + eval
# arithmetic + I/O array dtype). It cannot be inferred from `func`: a Python
# callable just returns a float, so the precision is a deliberate fit-time knob
# (default "f64"). dim/out_dim *are* inferred, by probing func.
approx = treeweave.fit(func, 0.0, math.pi, tol=1e-4, dtype="f32")
print(approx)  # dtype='f32', dim, out_dim, memory_usage

# The fit domain is [a, b); evaluating exactly at the upper corner b is allowed
# as a convenience and returns the boundary value (not NaN).
xs = np.linspace(0.0, math.pi, 50, dtype=np.float32)  # includes the endpoint
ys = approx(xs)
assert ys.dtype == np.float32
max_err = float(np.max(np.abs(ys.astype(np.float64) - np.sin(xs))))
print(f"max |approx - sin| = {max_err:.3e}")
assert max_err < 1e-3
print("OK")
