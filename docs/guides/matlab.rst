MATLAB / Octave
===============

One source builds for both MATLAB (R2019b+) and GNU Octave (>= 6).
`mwrap <https://github.com/zgimbutas/mwrap>`_ generates the binding from a
single ``treeweave.mw`` source. The user-facing ``treeweave.m`` is a thin
``classdef`` that holds the opaque C handle.

Install (MATLAB)
----------------

With `mip <https://mip.sh/>`_, from the `mip-org/labs
<https://github.com/mip-org/mip-labs>`_ channel:

.. not-run-in-ci: installs a published channel package; matlab.yml builds and tests the MEX bundle these commands install.

.. code-block:: matlab

   mip install --channel mip-org/labs treeweave
   mip load treeweave

Or download the bundle directly. Each release attaches a self-contained
``treeweave-matlab-<version>-<platform>`` bundle (``linux-x64``, ``windows-x64``,
``macos-arm64``, ``macos-x64``) on the `Releases page
<https://github.com/DiamonDinoia/treeweave/releases>`_. The MEX statically links
the C ABI, so the bundle has no runtime dependencies. Download it, extract it,
and ``addpath`` the extracted directory (which holds ``treeweave.m``, the generated
``tw_*.m`` stubs, and ``treeweave_mex.<ext>``).

.. not-run-in-ci: no workflow downloads the published MATLAB bundle, so these two lines are unverified; release.yml build-mex builds the bundle and matlab.yml tests its contents. Testing the download needs a MATLAB job for a curl and a tar.

.. code-block:: bash

   PLATFORM=linux-x64
   URL="https://github.com/DiamonDinoia/treeweave/releases/latest/download/treeweave-matlab-${PLATFORM}.tar.gz"
   curl -fLO "$URL"
   mkdir -p treeweave-matlab
   tar xzf "treeweave-matlab-${PLATFORM}.tar.gz" --strip-components=1 -C treeweave-matlab

.. not-run-in-ci: path setup for an extracted release bundle, which no workflow downloads; matlab.yml runs the same treeweave.m against a freshly built MEX.

.. code-block:: matlab

   addpath('treeweave-matlab')

Install (Octave)
----------------

Octave has no stable MEX ABI across versions, so there is no prebuilt Octave
MEX. Download the source release and build against your local Octave:

.. not-run-in-ci: a full Octave build of the released source tarball, on every pull request, to check a curl and a cd. octave.yml runs the same two cmake lines on the checkout, via the octave-dev recipe below.

.. code-block:: bash

   URL="https://github.com/DiamonDinoia/treeweave/archive/refs/heads/stable.tar.gz"
   curl -fL "$URL" -o treeweave-source.tar.gz
   tar xzf treeweave-source.tar.gz
   cd treeweave-stable
   cmake --preset bindings-octave
   cmake --build build/bindings-octave -j

Build from source
-----------------

Building is an opt-in CMake option, ``TREEWEAVE_BUILD_MATLAB``. With
``mkoctfile`` (or MATLAB) on ``PATH``,
CMake fetches the mwrap generator, generates the gateway + ``tw_*.m`` stubs, and
compiles the MEX:

.. literalinclude:: ../../tools/ci/docs-recipes.sh
   :language: bash
   :start-after: # BEGIN DOCS_OCTAVE_DEV
   :end-before: # END DOCS_OCTAVE_DEV
   :dedent: 4

Requirements: MATLAB R2019b+ (tested on R2025a) or GNU Octave with
``mkoctfile``, plus a C++20 toolchain and CMake >= 3.25.

Minimal example
---------------

Add the ``treeweave.m`` class directory and the generated MEX directory to the
path, then fit and evaluate:

.. literalinclude:: ../../bindings/matlab/examples/example_1d.m
   :language: matlab

The callback receives a ``1 x dim`` row and returns a scalar or a
``out_dim x 1`` column. The fit domain is ``[a, b)``; evaluating exactly at the
upper corner ``b`` returns the boundary value (the last cell's polynomial).
Every other out-of-domain point returns ``NaN``, below ``a``, above ``b``, and
``NaN``/±Inf inputs alike, uniformly across the scalar, batch and sorted paths.

Evaluation routes
-----------------

The object is callable directly (or via ``obj.eval``), and two name/value flags
select the fast paths:

.. literalinclude:: ../../bindings/matlab/examples/example_routes.m
   :language: matlab
   :start-after: % BEGIN DOCS_ROUTES
   :end-before: % END DOCS_ROUTES

``'sorted', true`` skips treeweave's internal counting-sort and is ~3-4x faster
when the caller can promise the column is ascending, which covers ``linspace``
grids, quadrature nodes and time series. Nothing checks the promise, and
unsorted input gives wrong values, so use the plain batch call when the order is
unknown.

Multi-dimensional fits
----------------------

Pass row-vector corners; the callback takes a ``1 x dim`` row and returns an
``out_dim x 1`` column:

.. literalinclude:: ../../bindings/matlab/examples/example_vector.m
   :language: matlab
   :start-after: % BEGIN DOCS_MULTIDIM
   :end-before: % END DOCS_MULTIDIM

Options
-------

Pass name-value pairs after ``tol`` to ``treeweave(...)`` to override defaults:

.. literalinclude:: ../../bindings/matlab/examples/example_1d.m
   :language: matlab
   :start-after: % BEGIN DOCS_OPTIONS
   :end-before: % END DOCS_OPTIONS

Available options:

.. list-table::
   :header-rows: 1
   :widths: 28 15 57

   * - Name
     - Default
     - Meaning
   * - ``'tol_kind'``
     - ``2`` (relative max)
     - Tolerance interpretation, as the numeric ``treeweave_tol_kind_t``:
       ``0`` relative tail, ``1`` absolute tail, ``2`` relative max,
       ``3`` absolute max, ``4`` relative L2, ``5`` absolute L2.
   * - ``'max_depth'``
     - ``50``
     - Tree-depth ceiling.
   * - ``'max_memory_mib'``
     - ``-1`` (auto)
     - Memory budget in MiB. ``-1`` = auto (4/8/16 MiB for dim 1/2/3); ``0`` = no cap.
   * - ``'allow_max_depth_leaves'``
     - ``false``
     - Keep non-converged panels at max depth instead of raising.
   * - ``'min_uniform_depth'``
     - ``0``
     - Force uniform refinement to this depth before adaptivity.

See `the options guide <https://diamondinoia.github.io/treeweave/guides/options.html>`_ for full descriptions.

Further
-------

Examples: `bindings/matlab/examples/
<https://github.com/DiamonDinoia/treeweave/tree/main/bindings/matlab/examples>`_
(``example_1d.m``, ``example_2d.m``, ``example_vector.m``).
