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

.. include:: ../_shared/sorted.src

``sorted = true`` is 1-D only. ``transposed = true`` returns each output
component in its own contiguous row.

.. include:: ../_shared/domain.src

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

``TreeweaveOptions`` takes every shared fit option as a keyword, spelled in
snake case: ``tol_kind``, ``max_depth``, ``max_memory_mib``,
``allow_max_depth_leaves`` and ``min_uniform_depth``. ``tol_kind`` takes one of
the exported constants ``TREEWEAVE_RELATIVE_MAX``, ``TREEWEAVE_ABSOLUTE_MAX``,
``TREEWEAVE_RELATIVE_L2``, ``TREEWEAVE_ABSOLUTE_L2``,
``TREEWEAVE_RELATIVE_TAIL`` or ``TREEWEAVE_ABSOLUTE_TAIL``.

Every keyword defaults to ``nothing``, meaning the library's own default;
``Treeweave.default_opts()`` returns those values, read from the C ABI, so no
default is written down twice.

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

.. not-run-in-ci: developer install; bindings.yml builds the same sibling target and runs the suite.

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
