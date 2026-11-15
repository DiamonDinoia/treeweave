Julia
=====

Install
-------

The package resolves ``libtreeweave_c`` in this order: the ``LIBTREEWEAVE_C``
environment variable, then a sibling ``build*/`` directory for developers, then
a prebuilt download from the GitHub Release.

The normal install uses the prebuilt release binary:

.. code-block:: julia

   using Pkg
   Pkg.add(url="https://github.com/DiamonDinoia/treeweave", subdir="bindings/julia/Treeweave")

On first build, Julia downloads the matching ``libtreeweave_c`` from the GitHub
Release and caches it. Override the release source repo with ``TREEWEAVE_REPO``,
or point ``LIBTREEWEAVE_C`` at a local C ABI build.

Minimal example
---------------

.. literalinclude:: ../../bindings/julia/Treeweave/examples/example_1d.jl
   :language: julia

``fit`` infers the dimensions from the callable, so the common case is
``fit(f, a, b, tol)``. Call the fitted object directly, with a point or with a
batch. Tune the fit with ``TreeweaveOptions`` (see :doc:`options`).

``fit`` takes the callable first, so ``do``-block syntax fits a function
defined at the call site. That is the Julia counterpart of the Python
decorator:

.. code-block:: julia

   approx = fit(2.0, 10.0, 1e-10) do s
       sum(k -> k^(-s), 1:1000)
   end

Evaluation routes
-----------------

Calling the handle dispatches on its argument, and two keyword flags select the
fast paths:

.. code-block:: julia

   approx(3.5)                       # single point  -> scalar (or Vector)
   approx(xs)                        # batch (Vector) -> Vector / n×out_dim
   approx(xs; sorted = true)         # promise xs is non-decreasing, xs[i] <= xs[i+1] (dim == 1)
   approx(xs; transposed = true)     # batch -> out_dim×n  (requires out_dim > 1)

``sorted = true`` skips treeweave's internal bin-sort and is ~3-4x faster when
the caller can promise ``xs`` is ascending, which covers ``range`` grids,
quadrature nodes and time series. Nothing checks the promise, and unsorted input
gives wrong values, so use the plain batch path when the order is unknown.
``sorted`` is 1-D only. ``transposed = true`` returns each output component in
its own contiguous row.

Every path handles out-of-domain input the same way. A point exactly at ``b``
returns the boundary value. Points below ``a``, points above ``b``, and ``NaN``
or ±Inf inputs all return ``NaN``.

Multi-dimensional fits
----------------------

Pass vector corners; the callback takes ``dim`` scalar arguments and returns a
scalar or a length-``out_dim`` tuple:

.. code-block:: julia

   # 2-D input -> 3-D vector output (out_dim inferred by probing the midpoint)
   approx = fit((x, y) -> (sin(x) * cos(y), x + y, x * y), [0.0, 0.0], [1.0, 1.0], 1e-8)

   approx([0.3, 0.7])                # single point -> length-3 Vector
   X = rand(100, 2)
   approx(X)                         # batch -> 100×3 Matrix
   approx(X; transposed = true)      # batch -> 3×100 Matrix

Options
-------

Pass a ``TreeweaveOptions`` as the fifth argument to ``fit``:

.. code-block:: julia

   using Treeweave
   opts = TreeweaveOptions(tol_kind="absolute_max", max_depth=30, max_memory_mib=64)
   approx = fit(f, 2.0, 10.0, 1e-10, opts)

Available fields (all keyword-only, all have defaults):

.. list-table::
   :header-rows: 1
   :widths: 28 15 57

   * - Field
     - Default
     - Meaning
   * - ``tol_kind``
     - ``"relative_max"``
     - Tolerance interpretation. One of ``"relative_max"``, ``"absolute_max"``,
       ``"relative_l2"``, ``"absolute_l2"``, ``"relative_tail"``, ``"absolute_tail"``,
       ``"relative_pointwise"``.
   * - ``max_depth``
     - ``0`` (auto)
     - Tree-depth ceiling. ``0`` = auto, 50; ``-1`` = the root panel only.
   * - ``max_memory_mib``
     - ``0`` (auto)
     - Memory budget in MiB. ``0`` = auto (4/32/256 MiB for dim 1/2/3); ``-1`` = no cap.
   * - ``allow_max_depth_leaves``
     - ``false``
     - Keep non-converged panels at max depth instead of throwing.
   * - ``min_uniform_depth``
     - ``0``
     - Force uniform refinement to this depth before adaptivity.

Fit file
--------

The ``filename`` keyword of ``fit`` names a plain-text fit file. If the file
exists and its header matches the dimensions, domain, ``tol``, options and
dtype, ``fit`` loads the file and does not call ``f``. Otherwise ``fit`` runs
and writes the file. A malformed file with a matching header makes ``fit``
throw. The file does not identify ``f``, so delete it when ``f`` changes. Pass
``out_dim`` explicitly, because the default probe calls ``f`` once.

.. code-block:: julia

   approx = fit(f, 2.0, 10.0, 1e-10; out_dim = 1, filename = "f.tw")  # fits and writes
   approx = fit(f, 2.0, 10.0, 1e-10; out_dim = 1, filename = "f.tw")  # loads, 0 calls of f

See :doc:`options` for a full description of each option and the tolerance kinds.

Further
-------

Build from source only when working on the binding or testing an unreleased
change. Build the C ABI in a checkout, then ``develop`` the package against the
sibling build:

.. code-block:: bash

   git clone https://github.com/DiamonDinoia/treeweave.git
   cd treeweave
   cmake --preset bindings-julia
   cmake --build build/bindings-julia -j --target treeweave_c

.. code-block:: julia

   using Pkg
   Pkg.develop(path="bindings/julia/Treeweave")
   Pkg.build("Treeweave")

.. code-block:: bash

   ctest --test-dir build/bindings-julia -R julia_treeweave

Examples:
`bindings/julia/Treeweave/examples/ <https://github.com/DiamonDinoia/treeweave/tree/main/bindings/julia/Treeweave/examples>`_.
