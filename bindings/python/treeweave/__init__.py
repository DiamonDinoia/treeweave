"""treeweave — piecewise-polynomial function approximation.

Public API
----------
fit(f, a, b, tol, *, ...)  -> TreeweaveFunction
    Fit a Python callable and return a *callable* evaluator. ``dim`` and
    ``out_dim`` are inferred from the domain corners and a one-shot probe of
    ``f``, so the common call is just ``treeweave.fit(f, a, b, tol)``.

TreeweaveFunction
    Callable evaluator. Call it with a point or a batch; ``sorted=`` and
    ``transposed=`` select the 1-D sorted fast path and the ``(out_dim, N)``
    layout. Exposes ``.dim``, ``.out_dim``, ``.dtype``, ``.memory_usage`` and
    ``print_stats()``.
"""

from __future__ import annotations

from typing import Callable
import numpy as np
import numpy as _np  # cached reference — used inside hot paths
from importlib.metadata import PackageNotFoundError, version as _pkg_version

from . import _treeweave  # compiled extension

try:
    __version__ = _pkg_version("treeweave")
except PackageNotFoundError:  # running from a source tree, not installed
    __version__ = "0.0.0"

__all__ = ["fit", "TreeweaveFunction", "__version__"]

# ---------------------------------------------------------------------------
# String → integer maps used by fit()
# ---------------------------------------------------------------------------

_TOL_KIND = {
    "relative_tail": 0,
    "absolute_tail": 1,
    "relative_max":  2,
    "absolute_max":  3,
    "relative_l2":   4,
    "absolute_l2":   5,
}


# ---------------------------------------------------------------------------
# Public TreeweaveFunction wrapper
# ---------------------------------------------------------------------------

class TreeweaveFunction:
    """Callable evaluator for a fitted treeweave approximation.

    Do not construct directly; use :func:`treeweave.fit`. The object is *called*
    to evaluate — there are no named eval methods. A point yields a point
    result, a batch yields a batch result, and the optional ``sorted`` /
    ``transposed`` flags select alternate batch modes.
    """

    def __init__(self, inner: _treeweave.TreeweaveFunction) -> None:
        self._inner = inner

    # ---- properties -------------------------------------------------------

    @property
    def dim(self) -> int:
        """Input dimensionality."""
        return self._inner.input_dim

    @property
    def out_dim(self) -> int:
        """Output dimensionality."""
        return self._inner.output_dim

    @property
    def memory_usage(self) -> int:
        """Approximate memory used by the approximation (bytes)."""
        return self._inner.memory_usage

    @property
    def dtype(self) -> str:
        """Value type: ``'f64'`` or ``'f32'``."""
        return self._inner.dtype

    # ---- evaluation -------------------------------------------------------

    def __call__(self, x, *, sorted: bool = False, transposed: bool = False):
        """Evaluate at *x*.

        Parameters
        ----------
        x : scalar | array-like
            A single point — a scalar (``dim == 1`` only) or a ``(dim,)``
            sequence — or a batch — ``(N,)`` for ``dim == 1`` or ``(N, dim)``
            otherwise.
        sorted : bool, optional
            1-D ascending-batch fast path. Requires ``dim == 1``; the caller
            promises ``x[i] <= x[i+1]``.
        transposed : bool, optional
            Return the batch result as ``(out_dim, N)`` (struct-of-arrays)
            instead of ``(N, out_dim)``. Requires ``out_dim > 1``.

        Returns
        -------
        scalar, (out_dim,), (N,), (N, out_dim), or (out_dim, N)
            A scalar / ``(out_dim,)`` array for a single point; an ``(N,)`` /
            ``(N, out_dim)`` array for a batch; ``(out_dim, N)`` when
            ``transposed=True``.

        Raises
        ------
        ValueError
            On a point whose length ``!= dim``, a batch whose column count
            ``!= dim``, ``sorted=True`` with ``dim != 1``, ``transposed=True``
            with ``out_dim == 1``, or both flags at once.
        """
        # Use local aliases to avoid the `sorted` parameter shadowing the builtin.
        use_sorted = sorted
        use_transposed = transposed

        if use_sorted and use_transposed:
            raise ValueError("sorted=True and transposed=True are mutually exclusive")

        dtype = _np.float32 if self.dtype == "f32" else _np.float64
        x = _np.asarray(x, dtype=dtype)

        # ---- batch-only modes selected by a flag ----
        if use_sorted:
            if self.dim != 1:
                raise ValueError(f"sorted=True requires dim == 1; this fit has dim={self.dim}")
            return self._inner.sorted(self._coerce_batch(x))

        if use_transposed:
            if self.out_dim == 1:
                raise ValueError("transposed=True requires out_dim > 1")
            soa = self._inner.eval_multi_soa(self._coerce_batch(x))
            return _np.stack(soa, axis=0)  # (out_dim, N)

        # ---- point vs batch dispatch ----
        if x.ndim == 0:
            if self.dim != 1:
                raise ValueError(f"a scalar is only a valid point for dim == 1; this fit has dim={self.dim}")
            return self._inner.eval_one(x.item())

        if x.ndim == 1:
            if self.dim == 1:
                # (1,) is the (dim,) point; longer is an (N,) batch.
                if x.shape[0] == 1:
                    return self._inner.eval_one(float(x[0]))
                return self._inner.eval_multi(_np.ascontiguousarray(x))
            # dim > 1: a 1-D input must be a single (dim,) point.
            if x.shape[0] != self.dim:
                raise ValueError(f"point has length {x.shape[0]} but dim == {self.dim}")
            return self._inner.eval_one(x)

        if x.ndim == 2:
            return self._inner.eval_multi(self._coerce_batch(x))

        raise ValueError(f"x must have ndim <= 2; got ndim={x.ndim}")

    def print_stats(self) -> None:
        """Print internal tree statistics to stdout."""
        self._inner.print_stats()

    def __repr__(self) -> str:
        return (
            f"TreeweaveFunction(dim={self.dim}, out_dim={self.out_dim}, "
            f"dtype={self.dtype!r}, memory={self.memory_usage} B)"
        )

    # ---- internal ---------------------------------------------------------

    def _coerce_batch(self, x: _np.ndarray) -> _np.ndarray:
        """Validate *x* as a batch and return a contiguous ``(N,)``/``(N, dim)`` array."""
        x = _np.ascontiguousarray(x)
        if self.dim == 1:
            if x.ndim == 1:
                return x
            if x.ndim == 2 and x.shape[1] == 1:
                return _np.ascontiguousarray(x[:, 0])
            raise ValueError(f"a dim == 1 batch must be (N,) or (N, 1); got shape {x.shape}")
        if x.ndim != 2 or x.shape[1] != self.dim:
            raise ValueError(f"a batch must be (N, {self.dim}); got shape {x.shape}")
        return x


# ---------------------------------------------------------------------------
# fit() — main entry point
# ---------------------------------------------------------------------------

def fit(
    f: Callable,
    a,
    b,
    tol: float,
    *,
    dim: int | None = None,
    out_dim: int | None = None,
    dtype: str = "f64",
    tol_kind: str = "relative_max",
    max_depth: int = 50,
    max_memory_mib: int = -1,
    allow_max_depth_leaves: bool = False,
    min_uniform_depth: int = 0,
) -> TreeweaveFunction:
    """Fit a Python callable and return a callable :class:`TreeweaveFunction`.

    Parameters
    ----------
    f : callable
        ``f(x) -> scalar`` or ``f(x) -> array(out_dim,)``. For f64 fits *x* is
        a ``float64`` ndarray of shape ``(dim,)``; for f32 fits a ``float32``
        one. For ``dim == 1`` *x* is a 1-element array (shape ``(1,)``).
    a, b : scalar or sequence of length *dim*
        Domain corners. If scalars, ``dim`` is inferred as 1.
    tol : float
        Approximation tolerance.
    dim : int, optional
        Input dimension. Inferred from ``len(a)`` (scalar corners ⇒ 1) when
        not given.
    out_dim : int, optional
        Output dimension. When not given it is inferred by probing
        ``f`` once at the box midpoint and taking ``np.asarray(result).size``
        (a scalar result ⇒ 1).
    dtype : {'f64', 'f32'}
        Floating-point precision.
    tol_kind : str
        Tolerance interpretation. One of ``'relative_max'``,
        ``'absolute_max'``, ``'relative_l2'``, ``'absolute_l2'``,
        ``'relative_tail'``, ``'absolute_tail'``.
    max_depth : int
        Maximum adaptive tree depth.
    max_memory_mib : int
        Memory budget in MiB. ``-1`` (default) auto-selects a
        dimension-scaled budget (4/8/16 MiB for dim 1/2/3); ``0`` disables
        the cap; a positive value is an explicit cap.
    allow_max_depth_leaves : bool
        Allow leaves at max depth (relaxes convergence).
    min_uniform_depth : int
        Minimum uniform refinement depth before adaptivity kicks in.

    Returns
    -------
    TreeweaveFunction

    Raises
    ------
    RuntimeError
        If the fit fails (MaxDepthExceeded, MemoryBudgetExceeded, …) or if the
        callback *f* raises (in which case the original exception propagates).
    """
    # ---- infer / validate dim -------------------------------------------
    try:
        a_seq = list(a)
    except TypeError:
        a_seq = [a]
    try:
        b_seq = list(b)
    except TypeError:
        b_seq = [b]

    inferred_dim = len(a_seq)
    if len(b_seq) != inferred_dim:
        raise ValueError("a and b must have the same length")

    if dim is None:
        dim = inferred_dim
    elif dim != inferred_dim:
        raise ValueError(f"dim={dim} but len(a)={inferred_dim}; they must agree")

    # ---- infer out_dim by probing f at the box midpoint -----------------
    if out_dim is None:
        midpoint = _np.array(
            [(float(av) + float(bv)) * 0.5 for av, bv in zip(a_seq, b_seq)],
            dtype=_np.float32 if dtype == "f32" else _np.float64,
        )
        probe = f(midpoint)
        out_dim = int(_np.asarray(probe).size)

    # ---- validate choices -----------------------------------------------
    if out_dim < 1 or out_dim > 3:
        raise ValueError(f"out_dim must be 1-3; got {out_dim}")
    if dim < 1 or dim > 3:
        raise ValueError(f"dim must be 1-3; got {dim}")
    if dtype not in ("f64", "f32"):
        raise ValueError(f"dtype must be 'f64' or 'f32'; got {dtype!r}")

    tol_kind_int = _TOL_KIND.get(tol_kind.lower())
    if tol_kind_int is None:
        raise ValueError(f"Unknown tol_kind {tol_kind!r}; choose from {list(_TOL_KIND)}")

    # ---- dispatch to the typed C-level fit ------------------------------
    common_kw = dict(
        input_dim=dim, output_dim=out_dim,
        tol=float(tol),
        tol_kind=tol_kind_int,
        max_depth=max_depth,
        max_memory_mib=max_memory_mib,
        allow_max_depth_leaves=int(allow_max_depth_leaves),
        min_uniform_depth=min_uniform_depth,
    )

    fit_fn = _treeweave.fit_f64 if dtype == "f64" else _treeweave.fit_f32
    inner = fit_fn(
        f,
        a=[float(v) for v in a_seq],
        b=[float(v) for v in b_seq],
        **common_kw,
    )

    return TreeweaveFunction(inner)
