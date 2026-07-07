C
=

``libtreeweave_c`` is the installable C surface: ``treeweave.h`` header plus a shared
and static library. The prebuilt binary dispatches SSE4.2/AVX2/AVX-512 at runtime —
compiling from source does not add speed for C consumers. See :doc:`dispatch`.

Install
-------

Download the Linux x86-64 tarball from the `Releases page
<https://github.com/DiamonDinoia/treeweave/releases>`_, then compile:

.. code-block:: bash

   VER=0.0.0   # replace with the desired release version
   wget "https://github.com/DiamonDinoia/treeweave/releases/download/v${VER}/treeweave-${VER}-linux-x86_64.tar.gz"
   tar xzf "treeweave-${VER}-linux-x86_64.tar.gz"   # extracts include/ lib/ into ./
   gcc examples/C/simple.c -Iinclude -Llib -ltreeweave_c -lm -o simple
   LD_LIBRARY_PATH=lib ./simple

Other platforms: ``linux-aarch64``, ``macos-arm64``, ``macos-x86_64``, ``windows-x64``
(zip). The tarball includes ``include/treeweave.h``, ``lib/libtreeweave_c``, and a
``find_package(treeweave)`` CMake package.

Via CMake (see :doc:`../install`):

.. code-block:: cmake

   find_package(treeweave REQUIRED)
   target_link_libraries(my_app PRIVATE treeweave::treeweave_c)

Minimal example
---------------

.. literalinclude:: ../../examples/C/simple.c
   :language: c

Evaluation entry points
-----------------------

- ``treeweave_eval(fn, x, y)`` — one point.
- ``treeweave_batch(fn, x, res, n)`` — ``n`` points, array-of-structs layout.
- ``treeweave_sorted(fn, x, res, n)`` — 1-D fast path for ascending inputs.
- ``treeweave_transposed(fn, x, soa, n)`` — struct-of-arrays output for
  multi-output fits.
- ``treeweave_eval_1d/2d/3d(fn, x0, ...)`` — by-value convenience for a
  scalar-output handle; each has a ``treeweavef_*`` float twin.

``context`` carries optional callback state; ``opts`` is ``NULL`` for defaults.
Errors return ``NULL`` / write nothing and set ``treeweave_last_error()``.

Evaluating exactly at the upper corner ``b`` returns the boundary value; inputs
below ``a`` yield ``NaN``.

Options
-------

Pass a ``treeweave_opts*`` as the last argument (or ``NULL`` for defaults):

.. code-block:: c

   treeweave_opts opts = treeweave_default_opts();
   opts.tol_kind       = TREEWEAVE_ABSOLUTE_MAX;
   opts.max_memory_mib = 64;
   treeweave_t fn = treeweave_fit(kernel, 1, 1, &a, &b, 1e-10, NULL, &opts);

``treeweave_tol_kind_t`` values: ``TREEWEAVE_RELATIVE_MAX`` (default),
``TREEWEAVE_ABSOLUTE_MAX``, ``TREEWEAVE_RELATIVE_L2``, ``TREEWEAVE_ABSOLUTE_L2``,
``TREEWEAVE_RELATIVE_TAIL``, ``TREEWEAVE_ABSOLUTE_TAIL``.

See :doc:`options` for the full description of each field.

Further
-------

See :doc:`c-abi` for the full entry-point list, float twins, and multi-arch
dispatch. Runnable C sources:
`examples/C/ <https://github.com/DiamonDinoia/treeweave/tree/main/examples/C>`_.
