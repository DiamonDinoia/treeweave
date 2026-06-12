C
=

The C ABI (``libtreeweave_c``, header ``treeweave.h``) is the stable, installable
surface that every non-C++ binding sits on. Precision lives in the prefix,
FINUFFT/FFTW style: ``treeweave_*`` operates on ``double``, ``treeweavef_*`` on
``float``.

Minimal example
---------------

.. code-block:: c

   #include <treeweave.h>
   #include <math.h>
   #include <stdio.h>

   /* zeta_N(s) = sum_{k=1..N} k^-s — expensive; fit once, eval a polynomial. */
   static void kernel(const double *x, double *y, void *context) {
       (void)context;
       double acc = 0.0;
       for (int k = 1; k <= 1000; ++k) acc += pow(k, -x[0]);
       y[0] = acc;
   }

   int main(void) {
       double a = 2.0, b = 10.0;
       treeweave_t fn = treeweave_fit(kernel, /*input_dim=*/1, /*output_dim=*/1,
                                      &a, &b, /*tol=*/1e-10,
                                      /*context=*/NULL, /*opts=*/NULL);
       if (!fn) { fprintf(stderr, "fit failed: %s\n", treeweave_last_error()); return 1; }

       double x = 3.5, y;
       treeweave_eval(fn, &x, &y);             /* pointer scalar eval     */
       double y2 = treeweave_eval_1d(fn, 3.5); /* by-value:  y2 = f(3.5)  */
       printf("zeta_N(3.5) ~= %.12f (%.12f)\n", y, y2);

       fn = treeweave_free(fn);
       return 0;
   }

Evaluation entry points
-----------------------

- ``treeweave_eval(fn, x, y)`` — one point.
- ``treeweave_batch(fn, x, res, n)`` — ``n`` points, array-of-structs layout.
- ``treeweave_sorted(fn, x, res, n)`` — 1-D fast path for ascending inputs.
- ``treeweave_transposed(fn, x, soa, n)`` — struct-of-arrays output for
  multi-output fits.
- ``treeweave_eval_1d/2d/3d(fn, x0, ...)`` — by-value convenience for a
  scalar-output (``output_dim == 1``) handle: pass coordinates by value, get the
  result back directly. Each has a ``treeweavef_*`` ``float`` twin.

``context`` carries optional callback state (the C analogue of a C++ lambda
capture); ``opts`` is ``NULL`` for ``treeweave_default_opts``. Errors return
``NULL`` / write nothing and set the thread-local ``treeweave_last_error()``.

Evaluating exactly at the upper corner ``b`` returns the boundary value (a
convenience over the half-open fit domain ``[a, b)``); inputs below ``a`` yield
``NaN``.

Building
--------

Link the shared or static C library:

.. code-block:: cmake

   find_package(treeweave REQUIRED)
   target_link_libraries(my_app PRIVATE treeweave::treeweave_c)

See :doc:`c-abi` for the full entry-point list, the float twins, and multi-arch
dispatch. Runnable C sources:
`examples/C/ <https://github.com/DiamonDinoia/treeweave/tree/main/examples/C>`_.
