Python
======

Install
-------

Install the latest release from PyPI:

.. code-block:: bash

   pip install treeweave

The wheel bundles the C ABI statically; the x86-64 wheel dispatches across the
x86 SIMD ISAs at runtime. NumPy is the only dependency.

To test an unreleased change, every push to ``main`` publishes a staging wheel
(``X.Y.Z.devN``) to `TestPyPI <https://test.pypi.org/project/treeweave/>`_;
install it with TestPyPI as the primary index and real PyPI for the dependencies:

.. code-block:: bash

   pip install --index-url https://test.pypi.org/simple/ \
               --extra-index-url https://pypi.org/simple/ treeweave

Minimal example
---------------

.. literalinclude:: ../../bindings/python/examples/simple_1d.py
   :language: python

``fit`` infers the input and output dimensions by probing the callable, so the
common case is ``fit(f, a, b, tol=...)``. Call the fitted object directly, with
a single point or with a batch. A failing C++ fit raises a Python exception.

Decorator form
--------------

Omit the callable and ``fit`` returns a decorator, the ``functools.cache``
spelling. The decorated name becomes the fitted approximation:

.. literalinclude:: ../../bindings/python/examples/decorator_1d.py
   :language: python

``fit(a, b, tol)`` and ``fit(a, b, tol=...)`` are both accepted, and every
keyword option below applies unchanged::

   @treeweave.fit([0.0, 0.0], [1.0, 1.0], tol=1e-6, dtype="f32")
   def surface(x):
       return math.cos(x[0] - x[1])

The decorated object is an ordinary ``TreeweaveFunction``, so ``sorted=`` /
``transposed=`` and the properties all work on it. ``__name__`` and ``__doc__``
come from the original function, which stays reachable as ``__wrapped__``.
Fitting happens at decoration time, so an import-time failure raises there.

Evaluation routes
-----------------

Calling the fitted object dispatches on the shape of its argument, and two
keyword flags select the fast paths:

.. code-block:: python

   approx(3.5)                       # single point  -> scalar (or (out_dim,))
   approx(xs)                        # batch (N,)     -> (N,) / (N, out_dim)
   approx(xs, sorted=True)           # promise xs is non-decreasing, xs[i] <= xs[i+1] (dim == 1)
   approx(xs, transposed=True)       # batch -> (out_dim, N)  (requires out_dim > 1)

``sorted=True`` skips treeweave's internal counting-sort and is ~3-4x faster
when the caller can promise ``xs`` is ascending, which covers ``linspace``
grids, quadrature nodes and time series. Nothing checks the promise, and
unsorted input gives wrong values, so use the plain batch path when the order is
unknown. ``transposed=True`` returns each output component in its own contiguous
row.

Every path handles out-of-domain input the same way. A point exactly at ``b``
returns the boundary value. Points below ``a``, points above ``b``, and ``NaN``
or ±Inf inputs all return ``NaN``.

Multi-dimensional fits
----------------------

Pass sequence corners; the callback receives a length-``dim`` row and returns a
scalar or a length-``out_dim`` sequence:

.. code-block:: python

   def bump(x):
       return math.exp(-100 * (x[0] - 0.5) ** 2 - (x[1] - 0.5) ** 2)

   approx = treeweave.fit(bump, [0.0, 0.0], [1.0, 1.0], tol=1e-8)
   y = approx(np.array([[0.4, 0.6]]))   # shape (N, dim) -> (N, out_dim)

Options
-------

Pass keyword arguments to ``fit`` to override defaults:

.. list-table::
   :header-rows: 1
   :widths: 28 15 57

   * - Kwarg
     - Default
     - Meaning
   * - ``tol_kind``
     - ``'relative_max'``
     - Tolerance interpretation. One of ``'relative_max'``, ``'absolute_max'``,
       ``'relative_l2'``, ``'absolute_l2'``, ``'relative_tail'``, ``'absolute_tail'``.
   * - ``max_depth``
     - ``50``
     - Tree-depth ceiling; raises ``RuntimeError`` with ``MaxDepthExceeded`` if hit.
   * - ``max_memory_mib``
     - ``-1`` (auto)
     - Memory budget in MiB. ``-1`` = auto (4/8/16 MiB for dim 1/2/3); ``0`` = no cap.
   * - ``allow_max_depth_leaves``
     - ``False``
     - Keep non-converged panels at max depth instead of raising.
   * - ``min_uniform_depth``
     - ``0``
     - Force uniform refinement to this depth before adaptivity.
   * - ``dim``
     - inferred
     - Input dimension; inferred from ``len(a)`` (scalar corners → 1).
   * - ``out_dim``
     - inferred
     - Output dimension; inferred by probing ``f`` at the box midpoint.
   * - ``dtype``
     - ``'f64'``
     - Floating-point precision: ``'f64'`` or ``'f32'``.

See :doc:`options` for a full description of each option and the tolerance kinds.

Further
-------

.. code-block:: bash

   cmake --preset bindings-python
   cmake --build build/bindings-python -j
   ctest --test-dir build/bindings-python -R python_treeweave

Examples:
`bindings/python/examples/ <https://github.com/DiamonDinoia/treeweave/tree/main/bindings/python/examples>`_.
