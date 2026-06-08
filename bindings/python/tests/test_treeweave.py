"""pytest suite for the treeweave Python bindings.

Each test is self-contained and exercises a specific part of the API. The
fitted object is *called* (no named eval methods); ``sorted=`` / ``transposed=``
select the alternate batch modes.
"""

import math
import numpy as np
import pytest
import treeweave


# ---------------------------------------------------------------------------
# 1. Accuracy: 1D scalar fit (dim & out_dim inferred)
# ---------------------------------------------------------------------------

def test_accuracy_1d():
    """fit exp(0.5x)+sin(3x) on [0,1]; eval agrees to < 1e-5."""

    def func(x):
        v = x[0]
        return math.exp(0.5 * v) + math.sin(3.0 * v)

    approx = treeweave.fit(func, 0.0, 1.0, tol=1e-8)
    assert approx.dim == 1
    assert approx.out_dim == 1
    assert approx.dtype == "f64"
    assert approx.memory_usage > 0

    # The fit domain is [a, b); evaluating exactly at b returns the boundary
    # value (a convenience), but this accuracy sweep stays inside [a, b).
    xs = np.linspace(0.0, 1.0, 200, endpoint=False)
    exact = np.exp(0.5 * xs) + np.sin(3.0 * xs)
    approx_vals = approx(xs)
    assert approx_vals.shape == (200,)
    max_err = np.max(np.abs(approx_vals - exact))
    assert max_err < 1e-5, f"max error {max_err} exceeds 1e-5"


# ---------------------------------------------------------------------------
# 2. AoS vs transposed parity — 2D → 3D (out_dim inferred from a probe)
# ---------------------------------------------------------------------------

def test_aos_transposed_parity_2d_3out():
    """approx(X) equals approx(X, transposed=True) component-by-component."""

    def func(x):
        return np.array([
            math.exp(0.3 * x[0]) + math.sin(2.0 * x[1]),
            math.cos(x[0] * x[1]) + 2.0,
            x[0] ** 2 + x[1] + 1.0,
        ])

    approx = treeweave.fit(func, [0.2, 0.2], [1.5, 1.5], tol=1e-7)
    assert approx.dim == 2
    assert approx.out_dim == 3  # inferred by probing func at the midpoint

    N = 64
    rng = np.random.default_rng(42)
    xs = rng.uniform([[0.2, 0.2]], [[1.5, 1.5]], size=(N, 2))

    aos = approx(xs)                     # (N, 3)
    tr = approx(xs, transposed=True)     # (3, N)

    assert aos.shape == (N, 3)
    assert tr.shape == (3, N)
    for d in range(3):
        np.testing.assert_array_equal(
            aos[:, d], tr[d],
            err_msg=f"AoS and transposed differ for component {d}",
        )


# ---------------------------------------------------------------------------
# 3. sorted == general batch for 1D (bit-exact on sorted input)
# ---------------------------------------------------------------------------

def test_sorted_equals_batch_1d():
    """approx(x, sorted=True) on sorted input is bit-exact with approx(x)."""

    def func(x):
        return math.sin(x[0]) * math.exp(-0.2 * x[0])

    approx = treeweave.fit(func, 0.0, 5.0, tol=1e-9)

    xs = np.sort(np.random.default_rng(7).uniform(0.0, 5.0, 256))
    batch_res = approx(xs)
    sorted_res = approx(xs, sorted=True)
    np.testing.assert_array_equal(
        batch_res, sorted_res,
        err_msg="sorted=True and the general batch differ on sorted input",
    )


# ---------------------------------------------------------------------------
# 4. NaN for out-of-domain points
# ---------------------------------------------------------------------------

def test_nan_out_of_domain():
    """Evaluating outside [a, b] returns NaN."""

    def func(x):
        return math.exp(x[0])

    approx = treeweave.fit(func, 0.0, 1.0, tol=1e-8)

    assert np.isnan(approx(-0.5))
    assert np.isnan(approx(2.0))


# ---------------------------------------------------------------------------
# 5. Raising Python callback propagates the exception
# ---------------------------------------------------------------------------

def test_raising_callback():
    """A callback that raises ValueError should make fit() raise.

    out_dim is given explicitly so the raise happens inside the C trampoline
    (not the inference probe), exercising the reverse-exception path.
    """

    def bad_func(x):
        raise ValueError("intentional test error")

    with pytest.raises((ValueError, RuntimeError)):
        treeweave.fit(bad_func, 0.0, 1.0, tol=1e-6, out_dim=1)


# ---------------------------------------------------------------------------
# 6. Too-tight tolerance / low max_depth raises RuntimeError
# ---------------------------------------------------------------------------

def test_max_depth_exceeded():
    """Overly tight tolerance with tiny max_depth raises RuntimeError."""

    def hard_func(x):
        # Highly oscillatory — hard to fit tightly in 2 levels.
        return math.sin(200.0 * x[0])

    with pytest.raises(RuntimeError, match="(?i)(maxdepth|depth|memory|budget|failed)"):
        treeweave.fit(hard_func, 0.0, 1.0, tol=1e-14, max_depth=2)


# ---------------------------------------------------------------------------
# 7. float32 path
# ---------------------------------------------------------------------------

def test_float32_path():
    """The f32 dtype path fits and evaluates correctly."""

    def func(x):
        return float(math.exp(x[0]))

    approx = treeweave.fit(func, 0.0, 1.0, tol=1e-4, dtype="f32")
    assert approx.dtype == "f32"

    xs = np.linspace(0.0, 1.0, 50, endpoint=False, dtype=np.float32)
    result = approx(xs)
    assert result.dtype == np.float32

    exact = np.exp(xs)
    max_err = float(np.max(np.abs(result.astype(np.float64) - exact)))
    assert max_err < 1e-3, f"f32 max error {max_err} too large"


# ---------------------------------------------------------------------------
# 8. Scalar __call__ for 1D
# ---------------------------------------------------------------------------

def test_scalar_call_1d():
    """TreeweaveFunction.__call__ accepts a Python scalar for dim==1."""

    def func(x):
        return math.exp(x[0])

    approx = treeweave.fit(func, 0.0, 1.0, tol=1e-8)
    result = approx(0.5)
    assert isinstance(result, float)
    assert abs(result - math.exp(0.5)) < 1e-7


# ---------------------------------------------------------------------------
# 9. Vector-valued 1D fit (out_dim inferred = 2)
# ---------------------------------------------------------------------------

def test_vector_output_1d():
    """1D → 2D vector-valued fit; out_dim inferred from the probe."""

    def func(x):
        return np.array([math.sin(x[0]), math.cos(x[0])])

    approx = treeweave.fit(func, 0.0, math.pi, tol=1e-7)
    assert approx.dim == 1
    assert approx.out_dim == 2

    xs = np.linspace(0.1, math.pi - 0.1, 100)
    result = approx(xs)
    assert result.shape == (100, 2)
    np.testing.assert_allclose(result[:, 0], np.sin(xs), atol=1e-5)
    np.testing.assert_allclose(result[:, 1], np.cos(xs), atol=1e-5)


# ---------------------------------------------------------------------------
# 10. Single (dim,) point for dim>1 returns an (out_dim,) vector
# ---------------------------------------------------------------------------

def test_point_eval_2d_vector():
    """A length-dim point yields a single (out_dim,) result."""

    def func(x):
        return np.array([x[0] + x[1], x[0] * x[1]])

    approx = treeweave.fit(func, [0.0, 0.0], [1.0, 1.0], tol=1e-7)
    assert approx.out_dim == 2
    y = approx([0.3, 0.4])
    assert y.shape == (2,)
    np.testing.assert_allclose(y, [0.7, 0.12], atol=1e-5)


# ---------------------------------------------------------------------------
# 11. Strict size / flag validation
# ---------------------------------------------------------------------------

def test_validation_errors():
    """The callable rejects mis-shaped inputs and illegal flag combinations."""

    f1 = treeweave.fit(lambda x: math.exp(x[0]), 0.0, 1.0, tol=1e-6)             # dim 1, out 1
    f2 = treeweave.fit(lambda x: x[0] + x[1], [0.0, 0.0], [1.0, 1.0], tol=1e-6)  # dim 2, out 1
    fv = treeweave.fit(lambda x: np.array([math.sin(x[0]), math.cos(x[0])]),
                    0.0, 1.0, tol=1e-6)                                       # dim 1, out 2

    # Point length must equal dim.
    with pytest.raises(ValueError):
        f2(np.array([0.5]))
    with pytest.raises(ValueError):
        f2(np.array([0.1, 0.2, 0.3]))

    # A scalar is only a point for dim == 1.
    with pytest.raises(ValueError):
        f2(0.5)

    # Batch column count must equal dim.
    with pytest.raises(ValueError):
        f2(np.zeros((5, 3)))

    # sorted requires dim == 1.
    with pytest.raises(ValueError):
        f2(np.zeros((5, 2)), sorted=True)

    # transposed requires out_dim > 1.
    with pytest.raises(ValueError):
        f1(np.linspace(0.0, 1.0, 10), transposed=True)

    # The two flags are mutually exclusive.
    with pytest.raises(ValueError):
        fv(np.linspace(0.0, 1.0, 10), sorted=True, transposed=True)
