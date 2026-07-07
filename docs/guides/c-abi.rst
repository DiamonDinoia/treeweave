C ABI reference
===============

``libtreeweave_c`` (header ``treeweave.h``) is the stable C interface every
non-C++ binding builds on. Precision lives in the prefix, FINUFFT/FFTW style:
the ``treeweave_*`` entry points operate on ``double`` and the ``treeweavef_*``
twins on ``float``. Type-erased queries (dtype, dims, memory, free) take the
opaque handle and stay single (``treeweave_`` prefix only).

Handle and callbacks
--------------------

.. code-block:: c

   typedef struct treeweave_function *treeweave_t;
   typedef void (*treeweave_func_t)(const double *x, double *y, void *context);
   typedef void (*treeweavef_func_t)(const float *x, float *y, void *context);

Fit
---

.. code-block:: c

   treeweave_t treeweave_fit (treeweave_func_t  f, int input_dim, int output_dim,
                              const double *a, const double *b, double tol,
                              void *context, const treeweave_opts *opts);
   treeweave_t treeweavef_fit(treeweavef_func_t f, int input_dim, int output_dim,
                              const float *a, const float *b, double tol,
                              void *context, const treeweave_opts *opts);

``context`` carries optional callback state (NULL if unused). ``opts`` is NULL
for defaults, or points at a ``treeweave_opts`` initialized with
``treeweave_default_opts(&opts)``. Options take **no degree argument** — the
library auto-selects a register-optimal leaf degree for the detected CPU. On
failure it returns ``NULL`` and sets the thread-local ``treeweave_last_error()``.

Evaluate
--------

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

By-value scalar eval (C convenience)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

For the common ``y = f(x)`` case on a **scalar-output** handle
(``output_dim == 1``), thin by-value wrappers take the coordinates by value and
return the result — no output pointer. The ``_1d``/``_2d``/``_3d`` suffix is the
call arity and must match the handle's ``input_dim``:

.. code-block:: c

   double treeweave_eval_1d(treeweave_t f, double x0);
   double treeweave_eval_2d(treeweave_t f, double x0, double x1);
   double treeweave_eval_3d(treeweave_t f, double x0, double x1, double x2);
   float  treeweavef_eval_1d(treeweave_t f, float x0);
   float  treeweavef_eval_2d(treeweave_t f, float x0, float x1);
   float  treeweavef_eval_3d(treeweave_t f, float x0, float x1, float x2);

On a dimension mismatch (the handle's ``input_dim``/``output_dim`` does not match
the call arity / scalar output) they set ``treeweave_last_error()`` and return
``NaN``. Vector-output handles keep the pointer API (``treeweave_eval`` /
``treeweave_transposed``).

Introspection and teardown
--------------------------

.. code-block:: c

   treeweave_dtype_t treeweave_dtype       (treeweave_t f);  /* TREEWEAVE_F64 / TREEWEAVE_F32 */
   int               treeweave_input_dim   (treeweave_t f);
   int               treeweave_output_dim  (treeweave_t f);
   size_t            treeweave_memory_usage(treeweave_t f);
   void              treeweave_print_stats (treeweave_t f);  /* print fit/eval stats to stdout */
   treeweave_t       treeweave_free        (treeweave_t f);  /* returns NULL */
   const char       *treeweave_last_error  (void);           /* thread-local */

Multi-arch dispatch
-------------------

On x86, build with ``-DTREEWEAVE_C_MULTIARCH=ON`` and a baseline of
``-DTREEWEAVE_ARCH=x86-64``: the library compiles SSE4.2 / AVX2 / AVX-512
variants and selects one at runtime via CPU detection, so a single binary runs
everywhere. The prebuilt x86-64 wheels and release archives ship this way.

Thread safety
-------------

Once ``treeweave_fit`` returns a handle, its eval functions are safe to call
concurrently from many threads (each call allocates its own scratch).
``treeweave_last_error()`` is thread-local.
