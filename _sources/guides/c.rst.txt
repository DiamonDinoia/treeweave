C
=

``libtreeweave_c`` is the installable C surface: ``treeweave.h`` header plus a shared
and static library. The prebuilt binary dispatches across the x86 SIMD ladder at runtime —
compiling from source does not add meaningful performance for C consumers.
Prefer the binary, if it does not work for your platform, build from source.
Feel free to open an issue if you need a prebuilt binary for your platform.
See :doc:`dispatch`.

Install
-------

Direct tarball
^^^^^^^^^^^^^^

Download the Linux x86-64 tarball from the `Releases page
<https://github.com/DiamonDinoia/treeweave/releases>`_, then compile:

.. code-block:: bash

   VER=stable
   PLATFORM=linux-x86_64
   URL="https://github.com/DiamonDinoia/treeweave/releases/download/${VER}/treeweave-${VER}-${PLATFORM}.tar.gz"
   curl -fLO "$URL" || wget "$URL"
   tar xzf "treeweave-${VER}-${PLATFORM}.tar.gz"   # extracts include/ lib/ into ./
   gcc examples/C/simple.c -Iinclude -Llib -ltreeweave_c -lm -o simple
   LD_LIBRARY_PATH=lib ./simple

Other platforms: ``linux-aarch64``, ``macos-arm64``, ``macos-x86_64``, ``windows-x64``
(zip). The tarball includes ``include/treeweave.h``, ``lib/libtreeweave_c``, and a
``find_package(treeweave)`` CMake package.

CMake package
^^^^^^^^^^^^^

.. code-block:: cmake

   find_package(treeweave REQUIRED)
   target_link_libraries(my_app PRIVATE treeweave::treeweave_c)

Source build
^^^^^^^^^^^^

.. code-block:: bash

   git clone https://github.com/DiamonDinoia/treeweave.git
   cd treeweave
   cmake --preset dev-release
   cmake --build build/dev-release --target treeweave_c -j

Minimal example
---------------

.. literalinclude:: ../../examples/C/simple.c
   :language: c

Evaluation entry points
-----------------------

``libtreeweave_c`` is the stable C interface every non-C++ binding builds on.
Precision lives in the prefix, FINUFFT/FFTW style: ``treeweave_*`` entry points
operate on ``double`` and ``treeweavef_*`` twins operate on ``float``.
Type-erased queries (dtype, dims, memory, free) take the opaque handle and stay
single-prefix.

Handle and callbacks
^^^^^^^^^^^^^^^^^^^^

.. code-block:: c

   typedef struct treeweave_function *treeweave_t;
   typedef void (*treeweave_func_t)(const double *x, double *y, void *context);
   typedef void (*treeweavef_func_t)(const float *x, float *y, void *context);

Fit
^^^

.. code-block:: c

   treeweave_t treeweave_fit (treeweave_func_t  f, int input_dim, int output_dim,
                              const double *a, const double *b, double tol,
                              void *context, const treeweave_opts *opts);
   treeweave_t treeweavef_fit(treeweavef_func_t f, int input_dim, int output_dim,
                              const float *a, const float *b, double tol,
                              void *context, const treeweave_opts *opts);

``context`` carries optional callback state; ``opts`` is ``NULL`` for defaults.
Options take no degree argument; the library auto-selects a register-optimal
leaf degree for the detected CPU. On failure, fit returns ``NULL`` and sets the
thread-local ``treeweave_last_error()``.

Evaluate
^^^^^^^^

.. code-block:: c

   void treeweave_eval      (treeweave_t f, const double *x, double *y);
   void treeweave_batch     (treeweave_t f, const double *x, double *res, size_t n);
   void treeweave_sorted    (treeweave_t f, const double *x, double *res, size_t n);
   void treeweave_transposed(treeweave_t f, const double *x, double *const *soa, size_t n);

- ``eval`` — one point.
- ``batch`` — ``n`` points, array-of-structs (interleaved) layout.
- ``sorted`` — 1-D fast path for ascending inputs.
- ``transposed`` — struct-of-arrays output for multi-output fits.

Each has a ``treeweavef_*`` ``float`` twin.

Out-of-domain handling is uniform across all eval paths: evaluating exactly at
the upper corner ``b`` returns the boundary value, and every other point outside
``[a, b]`` — below ``a``, above ``b``, or ``NaN``/±Inf inputs — yields ``NaN``.
The batch hot path stays branchless (the domain test compiles to a SIMD mask).

By-value scalar eval
^^^^^^^^^^^^^^^^^^^^

For the common ``y = f(x)`` case on a scalar-output handle
(``output_dim == 1``), thin wrappers take coordinates by value and return the
result. The ``_1d``/``_2d``/``_3d`` suffix is the call arity and must match the
handle's ``input_dim``:

.. code-block:: c

   double treeweave_eval_1d(treeweave_t f, double x0);
   double treeweave_eval_2d(treeweave_t f, double x0, double x1);
   double treeweave_eval_3d(treeweave_t f, double x0, double x1, double x2);
   float  treeweavef_eval_1d(treeweave_t f, float x0);
   float  treeweavef_eval_2d(treeweave_t f, float x0, float x1);
   float  treeweavef_eval_3d(treeweave_t f, float x0, float x1, float x2);

On a dimension mismatch (the handle's ``input_dim``/``output_dim`` does not match
the call arity / scalar output), they set ``treeweave_last_error()`` and return
``NaN``. Vector-output handles keep the pointer API (``treeweave_eval`` /
``treeweave_transposed``).

Introspection and teardown
^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: c

   treeweave_dtype_t treeweave_dtype       (treeweave_t f);  /* TREEWEAVE_F64 / TREEWEAVE_F32 */
   int               treeweave_input_dim   (treeweave_t f);
   int               treeweave_output_dim  (treeweave_t f);
   size_t            treeweave_memory_usage(treeweave_t f);
   void              treeweave_print_stats (treeweave_t f);  /* print fit/eval stats to stdout */
   treeweave_t       treeweave_free        (treeweave_t f);  /* returns NULL */
   const char       *treeweave_last_error  (void);           /* thread-local */

Options
-------

Pass a ``treeweave_opts*`` as the last argument (or ``NULL`` for defaults):

.. code-block:: c

   treeweave_opts opts;
   treeweave_default_opts(&opts);
   opts.tol_kind       = TREEWEAVE_ABSOLUTE_MAX;
   opts.max_memory_mib = 64;
   treeweave_t fn = treeweave_fit(kernel, 1, 1, &a, &b, 1e-10, NULL, &opts);

``treeweave_tol_kind_t`` values: ``TREEWEAVE_RELATIVE_MAX`` (default),
``TREEWEAVE_ABSOLUTE_MAX``, ``TREEWEAVE_RELATIVE_L2``, ``TREEWEAVE_ABSOLUTE_L2``,
``TREEWEAVE_RELATIVE_TAIL``, ``TREEWEAVE_ABSOLUTE_TAIL``.

See :doc:`options` for the full description of each field.

Multi-arch dispatch
-------------------

On x86, build with ``-DTREEWEAVE_C_MULTIARCH=ON`` and a baseline of
``-DTREEWEAVE_ARCH=x86-64``. The library compiles a portable baseline plus
wider variants and selects one at runtime via CPU detection, so a single binary
runs everywhere. GCC/Clang use SSE4.2 / AVX2 / AVX-512 variants; MSVC uses AVX /
AVX2 / AVX-512.

Thread safety
-------------

Once ``treeweave_fit`` returns a handle, its eval functions are safe to call
concurrently from many threads. ``treeweave_last_error()`` is thread-local.

Further
-------

Runnable C sources:
`examples/C/ <https://github.com/DiamonDinoia/treeweave/tree/main/examples/C>`_.
