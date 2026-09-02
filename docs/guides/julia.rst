Julia
=====

The Julia binding is a pure-Julia ``ccall`` wrapper over ``libtreeweave_c``,
shipped as the ``Treeweave`` package. ``fit`` takes a callable and returns a
callable object that evaluates scalars, vectors and matrices. Inputs are 1-D,
2-D or 3-D.

Install
-------

The package resolves ``libtreeweave_c`` in this order: the ``LIBTREEWEAVE_C``
environment variable, then a sibling ``build*/`` directory for developers, then
a prebuilt download from the GitHub Release.

The normal install uses the prebuilt release binary:

.. not-run-in-ci: fetches a published release; the same path is exercised by julia-smoke.yml.

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

.. literalinclude:: ../../bindings/julia/Treeweave/examples/example_1d.jl
   :language: julia
   :start-after: # BEGIN DOCS_DO_BLOCK
   :end-before: # END DOCS_DO_BLOCK

Evaluation routes
-----------------

Calling the handle dispatches on its argument, and two keyword flags select the
fast paths:

.. literalinclude:: ../../bindings/julia/Treeweave/examples/example_routes.jl
   :language: julia
   :start-after: # BEGIN DOCS_ROUTES
   :end-before: # END DOCS_ROUTES

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

.. literalinclude:: ../../bindings/julia/Treeweave/examples/example_2d_vector.jl
   :language: julia
   :start-after: # BEGIN DOCS_MULTIDIM
   :end-before: # END DOCS_MULTIDIM

Options
-------

Pass a ``TreeweaveOptions`` as the ``options`` keyword of ``fit``:

.. literalinclude:: ../../bindings/julia/Treeweave/examples/example_1d.jl
   :language: julia
   :start-after: # BEGIN DOCS_OPTIONS
   :end-before: # END DOCS_OPTIONS

Available fields (all keyword-only, all have defaults):

.. list-table::
   :header-rows: 1
   :widths: 28 15 57

   * - Field
     - Default
     - Meaning
   * - ``tol_kind``
     - ``TREEWEAVE_RELATIVE_MAX``
     - Tolerance interpretation. One of the exported constants
       ``TREEWEAVE_RELATIVE_MAX``, ``TREEWEAVE_ABSOLUTE_MAX``, ``TREEWEAVE_RELATIVE_L2``,
       ``TREEWEAVE_ABSOLUTE_L2``, ``TREEWEAVE_RELATIVE_TAIL``, ``TREEWEAVE_ABSOLUTE_TAIL``.
   * - ``max_depth``
     - ``50``
     - Tree-depth ceiling.
   * - ``max_memory_mib``
     - ``-1`` (auto)
     - Memory budget in MiB. ``-1`` = auto (4/8/16 MiB for dim 1/2/3); ``0`` = no cap.
   * - ``allow_max_depth_leaves``
     - ``false``
     - Keep non-converged panels at max depth instead of throwing.
   * - ``min_uniform_depth``
     - ``0``
     - Force uniform refinement to this depth before adaptivity.

See :doc:`options` for a full description of each option and the tolerance kinds.

Further
-------

Build from source only when working on the binding or testing an unreleased
change. Build the C ABI in a checkout, then ``develop`` the package against the
sibling build:

.. literalinclude:: ../../tools/ci/docs-recipes.sh
   :language: bash
   :start-after: # BEGIN DOCS_CLONE
   :end-before: # END DOCS_CLONE
   :dedent: 4

.. literalinclude:: ../../tools/ci/docs-recipes.sh
   :language: bash
   :start-after: # BEGIN DOCS_JULIA_DEV
   :end-before: # END DOCS_JULIA_DEV
   :dedent: 4

.. not-run-in-ci: developer install; julia.yml builds the same sibling target and runs the suite.

.. code-block:: julia

   using Pkg
   Pkg.develop(path="bindings/julia/Treeweave")
   Pkg.build("Treeweave")

.. literalinclude:: ../../tools/ci/docs-recipes.sh
   :language: bash
   :start-after: # BEGIN DOCS_JULIA_TEST
   :end-before: # END DOCS_JULIA_TEST
   :dedent: 4

Examples:
`bindings/julia/Treeweave/examples/ <https://github.com/DiamonDinoia/treeweave/tree/main/bindings/julia/Treeweave/examples>`_.
