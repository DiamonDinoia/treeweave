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

    # Evaluating exactly at b returns the boundary value (a convenience), so the
    # sweep can include the endpoint.
    xs = np.linspace(0.0, 1.0, 200)
    exact = np.exp(0.5 * xs) + np.sin(3.0 * xs)
    approx_vals = approx(xs)
    assert approx_vals.shape == (200,)
    max_err = np.max(np.abs(approx_vals - exact))
    assert max_err < 1e-5, f"max error {max_err} exceeds 1e-5"


# ---------------------------------------------------------------------------
# 2. AoS vs transposed parity: 2D → 3D (out_dim inferred from a probe)
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
    """A callback that raises ValueError must propagate as ValueError with the original message.

    out_dim is given explicitly so the raise happens inside the C trampoline
    (not the inference probe), exercising the reverse-exception path.
    The fix for PY#1 ensures the original exception type (not RuntimeError) is
    preserved on Python < 3.12.
    """

    def bad_func(x):
        raise ValueError("intentional test error")

    with pytest.raises(ValueError, match="intentional test error"):
        treeweave.fit(bad_func, 0.0, 1.0, tol=1e-6, out_dim=1)


# ---------------------------------------------------------------------------
# 6. Too-tight tolerance / low max_depth raises RuntimeError
# ---------------------------------------------------------------------------

def test_max_depth_exceeded():
    """Overly tight tolerance with tiny max_depth raises RuntimeError."""

    def hard_func(x):
        # Highly oscillatory: hard to fit tightly in 2 levels.
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

    xs = np.linspace(0.0, 1.0, 50, dtype=np.float32)
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


# ---------------------------------------------------------------------------
# 12. out= writes in place and returns the caller's array (zero-copy path)
# ---------------------------------------------------------------------------


def test_out_param_batch_1d():
    """approx(xs, out=buf) fills buf in place and returns it, bit-exact with the
    allocating path."""
    approx = treeweave.fit(lambda x: math.exp(x[0]), 0.0, 1.0, tol=1e-8)
    xs = np.linspace(0.0, 1.0, 256)
    expected = approx(xs)
    buf = np.empty_like(xs)
    got = approx(xs, out=buf)
    assert got is buf
    np.testing.assert_array_equal(got, expected)


def test_out_param_sorted_1d():
    """out= works with the sorted=True fast path."""
    approx = treeweave.fit(lambda x: math.sin(x[0]), 0.0, 5.0, tol=1e-8)
    xs = np.sort(np.random.default_rng(1).uniform(0.0, 5.0, 200))
    expected = approx(xs, sorted=True)
    buf = np.empty_like(xs)
    got = approx(xs, sorted=True, out=buf)
    assert got is buf
    np.testing.assert_array_equal(got, expected)


def test_out_param_vector_output_1d():
    """out= with an (N, out_dim) buffer for a vector-valued fit."""
    approx = treeweave.fit(
        lambda x: np.array([math.sin(x[0]), math.cos(x[0])]), 0.0, math.pi, tol=1e-7
    )
    xs = np.linspace(0.1, 3.0, 50)
    expected = approx(xs)  # (50, 2)
    buf = np.empty((50, 2))
    got = approx(xs, out=buf)
    assert got is buf
    np.testing.assert_array_equal(got, expected)


def test_out_param_validation():
    """out= rejects the wrong size/dtype/layout and the non-batch call forms."""
    approx = treeweave.fit(lambda x: math.exp(x[0]), 0.0, 1.0, tol=1e-8)
    xs = np.linspace(0.0, 1.0, 100)

    with pytest.raises(ValueError):  # wrong size
        approx(xs, out=np.empty(50))
    with pytest.raises(ValueError):  # wrong dtype
        approx(xs, out=np.empty(100, dtype=np.float32))
    with pytest.raises(ValueError):  # non-contiguous
        approx(xs, out=np.empty(200)[::2])
    with pytest.raises(ValueError):  # out= with a single point
        approx(0.5, out=np.empty(1))

    fv = treeweave.fit(
        lambda x: np.array([math.sin(x[0]), math.cos(x[0])]), 0.0, 1.0, tol=1e-6
    )
    with pytest.raises(ValueError):  # out= with transposed=True
        fv(np.linspace(0.0, 1.0, 10), transposed=True, out=np.empty((2, 10)))


# ---------------------------------------------------------------------------
# 13. Decorator form: @fit(a, b, tol) replaces the function with the fit
# ---------------------------------------------------------------------------


def test_decorator_matches_direct_fit():
    """@fit(a, b, tol) gives the same values as fit(f, a, b, tol)."""

    def func(x):
        return math.exp(x[0])

    direct = treeweave.fit(func, 0.0, 1.0, tol=1e-8)

    @treeweave.fit(0.0, 1.0, 1e-8)
    def decorated(x):
        return math.exp(x[0])

    xs = np.linspace(0.0, 1.0, 101)
    np.testing.assert_array_equal(decorated(xs), direct(xs))
    assert isinstance(decorated, treeweave.TreeweaveFunction)


def test_decorator_keyword_tol_and_options():
    """The decorator accepts tol= and every fit option, and infers the dims."""

    @treeweave.fit([0.0, 0.0], [1.0, 1.0], tol=1e-6, dtype="f32", max_depth=20)
    def surface(x):
        return np.array([x[0] + x[1], math.cos(x[0] - x[1])])

    assert surface.dim == 2
    assert surface.out_dim == 2
    assert surface.dtype == "f32"

    got = surface(np.array([[0.25, 0.75]], dtype=np.float32))
    assert got.shape == (1, 2)
    assert abs(got[0, 0] - 1.0) < 1e-4
    assert abs(got[0, 1] - math.cos(-0.5)) < 1e-4


def test_decorator_preserves_metadata():
    """update_wrapper keeps the name/doc and exposes the original callable."""

    @treeweave.fit(0.0, 1.0, 1e-8)
    def logistic(x):
        """1 / (1 + exp(-x))."""
        return 1.0 / (1.0 + math.exp(-x[0]))

    assert logistic.__name__ == "logistic"
    assert logistic.__doc__ == "1 / (1 + exp(-x))."
    assert logistic.__wrapped__(np.array([0.5])) == 1.0 / (1.0 + math.exp(-0.5))
    assert abs(logistic(0.5) - logistic.__wrapped__(np.array([0.5]))) < 1e-8


def test_decorator_missing_tol():
    """Omitting the tolerance raises TypeError, not a confusing fit error."""
    with pytest.raises(TypeError):
        treeweave.fit(0.0, 1.0)
    with pytest.raises(TypeError):
        treeweave.fit(lambda x: x[0], 0.0, 1.0)


def test_options_default_to_the_library_defaults():
    """Omitting an option must equal passing treeweave_default_opts()' value.

    Nothing in the Python layer hard-codes a default, so the check fails if the
    None-means-default wiring drops an option or reaches the C shim with None.
    """
    import inspect

    from treeweave import _treeweave

    defaults = _treeweave.default_opts()
    assert set(defaults) == {
        "tol_kind",
        "max_depth",
        "max_memory_mib",
        "allow_max_depth_leaves",
        "min_uniform_depth",
    }

    params = inspect.signature(treeweave.fit).parameters
    for name in defaults:
        assert params[name].default is None, f"{name} still carries a Python default"

    def func(x):
        return math.exp(x[0])

    implicit = treeweave.fit(func, 0.0, 1.0, tol=1e-8)
    explicit = treeweave.fit(
        func,
        0.0,
        1.0,
        tol=1e-8,
        tol_kind="relative_max",
        max_depth=defaults["max_depth"],
        max_memory_mib=defaults["max_memory_mib"],
        allow_max_depth_leaves=bool(defaults["allow_max_depth_leaves"]),
        min_uniform_depth=defaults["min_uniform_depth"],
    )
    assert defaults["tol_kind"] == treeweave._TOL_KIND["relative_max"]
    assert implicit.memory_usage == explicit.memory_usage

    xs = np.linspace(0.0, 1.0, 101)
    np.testing.assert_array_equal(implicit(xs), explicit(xs))
