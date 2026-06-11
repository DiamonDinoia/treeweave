Julia
=====

Install
-------

The package resolves ``libtreeweave_c`` in this order: the ``LIBTREEWEAVE_C``
environment variable, then a sibling ``build*/`` directory, then a prebuilt
download from the GitHub Release. That gives two install paths.

**From source (works today — no release needed).** Build the C ABI in a
checkout, then ``develop`` the package against the sibling build:

.. code-block:: bash

   git clone https://github.com/DiamonDinoia/treeweave.git
   cmake --preset bindings-julia
   cmake --build build/bindings-julia --target treeweave_c

.. code-block:: julia

   using Pkg
   Pkg.develop(path="treeweave/bindings/julia/Treeweave")
   Pkg.build("Treeweave")   # finds the sibling build/bindings-julia/libtreeweave_c

**From a release (prebuilt).** Once a ``v*`` release is published, add the
package directly; on first build it downloads the matching ``libtreeweave_c``
from the GitHub Release and caches it:

.. code-block:: julia

   using Pkg
   Pkg.add(url="https://github.com/DiamonDinoia/treeweave", subdir="bindings/julia/Treeweave")

Override the release's source repo with the ``TREEWEAVE_REPO`` environment
variable, or point ``LIBTREEWEAVE_C`` at a local build.

Minimal example
---------------

.. code-block:: julia

   using Treeweave

   f = x -> exp(0.5x) + sin(3x)
   approx = fit(f, 0.0, 2.0, 1e-10)
   println(approx)   # show() prints dtype, dim, out_dim and bytes

   y = approx(1.0)                     # single point

   # Batch eval — the handle is called directly. The fit domain is [a, b);
   # evaluating exactly at the upper corner b is allowed (returns the boundary
   # value), so the endpoint can be kept.
   xs = collect(range(0.0, 2.0; length = 1001))
   ys = approx(xs)

``fit`` infers the dimensions from the callable, so the common case is
``fit(f, a, b, tol)``. The fitted object is called directly for a point or a
batch; tune the fit with ``TreeweaveOptions`` (see :doc:`options`).

Evaluation routes
-----------------

Calling the handle dispatches on its argument, and two keyword flags select the
fast paths:

.. code-block:: julia

   approx(1.0)                       # single point  -> scalar (or Vector)
   approx(xs)                        # batch (Vector) -> Vector / n×out_dim
   approx(xs; sorted = true)         # promise xs is non-decreasing, xs[i] <= xs[i+1] (dim == 1)
   approx(xs; transposed = true)     # batch -> out_dim×n  (requires out_dim > 1)

``sorted = true`` skips treeweave's internal counting-sort and is ~3–4× faster
when you can promise ``xs`` is ascending — common for ``range`` grids, quadrature
nodes, and time series. The promise is unchecked: unsorted input gives wrong
values, so use the plain batch path when unsure. ``transposed = true`` returns
each output component in its own contiguous row. ``sorted`` is 1-D only.
Out-of-domain handling is uniform across paths: evaluating exactly at ``b``
returns the boundary value, and every other point outside ``[a, b]`` — below
``a``, above ``b``, or ``NaN``/±Inf — returns ``NaN``.

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

Build / test against a local library
------------------------------------

.. code-block:: bash

   cmake --preset bindings-julia
   cmake --build build/bindings-julia -j --target treeweave_c
   ctest --test-dir build/bindings-julia -R julia_treeweave

Examples:
`bindings/julia/Treeweave/examples/ <https://github.com/DiamonDinoia/treeweave/tree/main/bindings/julia/Treeweave/examples>`_.
