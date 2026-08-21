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

.. code-block:: bash

   VER=stable
   PLATFORM=linux-x64
   URL="https://github.com/DiamonDinoia/treeweave/releases/download/${VER}/treeweave-matlab-${VER}-${PLATFORM}.tar.gz"
   curl -fLO "$URL" || wget "$URL"
   tar xzf "treeweave-matlab-${VER}-${PLATFORM}.tar.gz"

.. code-block:: matlab

   addpath('treeweave-matlab-stable-linux-x64')

Install (Octave)
----------------

Octave has no stable MEX ABI across versions, so there is no prebuilt Octave
MEX. Download the source release and build against your local Octave:

.. code-block:: bash

   VER=stable
   URL="https://github.com/DiamonDinoia/treeweave/archive/refs/tags/${VER}.tar.gz"
   curl -fL "$URL" -o "treeweave-${VER}-source.tar.gz" || wget -O "treeweave-${VER}-source.tar.gz" "$URL"
   tar xzf "treeweave-${VER}-source.tar.gz"
   cd "treeweave-${VER}"
   cmake --preset bindings-octave
   cmake --build build/bindings-octave -j

Build from source
-----------------

Building is an opt-in CMake option, ``TREEWEAVE_BUILD_MATLAB``. With
``mkoctfile`` (or MATLAB) on ``PATH``,
CMake fetches the mwrap generator, generates the gateway + ``tw_*.m`` stubs, and
compiles the MEX:

.. code-block:: bash

   cmake --preset bindings-octave      # or bindings-matlab if building against MATLAB
   cmake --build build/bindings-octave -j
   ctest --test-dir build/bindings-octave -R matlab_treeweave --output-on-failure

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

.. code-block:: matlab

   X = linspace(2, 10, 1000)';           % N x dim

   obj(X)                                % batch:  N x dim -> N x out_dim
   obj.eval(X)                           % identical to obj(X)
   obj(X, 'sorted', true)                % promise X is non-decreasing, X(i) <= X(i+1) (dim == 1)
   obj(X, 'transposed', true)            % batch -> out_dim x N (out_dim > 1)

``'sorted', true`` skips treeweave's internal counting-sort and is ~3-4x faster
when the caller can promise the column is ascending, which covers ``linspace``
grids, quadrature nodes and time series. Nothing checks the promise, and
unsorted input gives wrong values, so use the plain batch call when the order is
unknown.

Multi-dimensional fits
----------------------

Pass row-vector corners; the callback takes a ``1 x dim`` row and returns an
``out_dim x 1`` column:

.. code-block:: matlab

   % 2-D input -> 3-D vector output (out_dim inferred by probing the midpoint)
   g    = @(x) [sin(x(1)+x(2)); cos(x(1)-x(2)); x(1)*x(2)];
   obj2 = treeweave(g, [-1 -1], [1 1], 1e-6, 'max_memory_mib', 64);

   [gx, gy] = meshgrid(linspace(-1, 1, 50));
   Y  = obj2([gx(:), gy(:)]);                    % 2500 x 3
   Yt = obj2([gx(:), gy(:)], 'transposed', true); % 3 x 2500 (struct-of-arrays)

   fprintf('Memory: %.1f KiB\n', obj2.memory_usage() / 1024);
   delete(obj2);

Options
-------

Pass name-value pairs after ``tol`` to ``treeweave(...)`` to override defaults:

.. code-block:: matlab

   obj = treeweave(f, 2, 10, 1e-10, 'tol_kind', 'absolute_max', 'max_memory_mib', 64);

Available options:

.. list-table::
   :header-rows: 1
   :widths: 28 15 57

   * - Name
     - Default
     - Meaning
   * - ``'tol_kind'``
     - ``'relative_max'``
     - Tolerance interpretation. One of ``'relative_max'``, ``'absolute_max'``,
       ``'relative_l2'``, ``'absolute_l2'``, ``'relative_tail'``, ``'absolute_tail'``.
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
