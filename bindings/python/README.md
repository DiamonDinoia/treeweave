# treeweave Python bindings

Python (nanobind) wrapper for the [treeweave](../../README.md) piecewise-polynomial
function approximator.

## Installation

```bash
pip install scikit-build-core nanobind numpy
pip install .          # regular install
# or for development:
pip install -e . --no-build-isolation
```

## Quick start

```python
import math
import numpy as np
import treeweave

# Fit exp(x) on [0, 1] to 1e-10 relative tolerance.
def func(x):
    return math.exp(x[0])   # x is a (dim,) ndarray

approx = treeweave.fit(func, 0.0, 1.0, tol=1e-10)   # dim & out_dim inferred

# Scalar eval
print(approx(0.5))          # ≈ exp(0.5)

# Batch eval
xs = np.linspace(0, 1, 1000)
ys = approx(xs)                  # shape (1000,)
ys = approx(xs, sorted=True)     # 1-D ascending fast path
```

## API

### `treeweave.fit(f, a, b, tol, *, dim=None, out_dim=None, dtype="f64", ...)`

Fits callable `f` and returns a callable `TreeweaveFunction`.

- `a`, `b`: domain corners (scalars for 1D, length-`dim` lists otherwise).
- `tol`: approximation tolerance (drives adaptive tree refinement).
- `dim`: input dimension; inferred from `len(a)` (scalar corners ⇒ 1) when omitted.
- `out_dim`: number of output components; inferred by probing `f` once at the
  box midpoint (`np.asarray(f(mid)).size`, scalar ⇒ 1) when omitted.
- `dtype`: `"f64"` (default) or `"f32"`.

### `treeweave.fit(a, b, tol, ...)` as a decorator

Omitting `f` returns a decorator, the `functools.cache` spelling:

```python
@treeweave.fit(0.0, 1.0, tol=1e-10)
def expensive(x):
    return math.exp(x[0])

expensive(0.5)          # evaluates the approximation
expensive.__wrapped__   # the original callable
```

`fit(a, b, tol)` and `fit(a, b, tol=...)` both work, and all keyword options
apply unchanged. The result is a plain `TreeweaveFunction` carrying the
original `__name__` / `__doc__`. Fitting happens at decoration time.

### `TreeweaveFunction`

Call the fitted object. There are no named eval methods. A point gives a point
result and a batch gives a batch result. Two optional keyword flags select the
other batch modes.

| Call / property | Description |
|---|---|
| `fn(x)` | Evaluate. Point: scalar (`dim==1`) or `(dim,)` → scalar / `(out_dim,)`. Batch: `(N,)` (`dim==1`) or `(N, dim)` → `(N,)` / `(N, out_dim)` |
| `fn(x, sorted=True)` | 1-D ascending-batch fast path (requires `dim == 1`) |
| `fn(x, transposed=True)` | Batch returning `(out_dim, N)` (requires `out_dim > 1`) |
| `.dim` | Input dimensionality |
| `.out_dim` | Output dimensionality |
| `.dtype` | `"f64"` or `"f32"` |
| `.memory_usage` | Bytes used by the approximation |
| `.print_stats()` | Print internal tree statistics |

A point whose length `!= dim`, a batch whose column count `!= dim`,
`sorted=True` with `dim != 1`, or `transposed=True` with `out_dim == 1` each
raise `ValueError` rather than silently mis-shaping the result.

## Running tests

```bash
pip install pytest
pytest tests/ -q
```
