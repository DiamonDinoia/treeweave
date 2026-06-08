C++
===

treeweave's C++ API is header-only. Include the umbrella header and call
``treeweave::fit``:

.. code-block:: cpp

   #include <treeweave/treeweave.hpp>

1-D fit
-------

.. code-block:: cpp

   auto runge = [](double x) { return 1.0 / (1.0 + 25.0 * x * x); };

   auto fn = treeweave::fit(runge, -1.0, 1.0, /*tol=*/1e-10);
   double y = fn(0.3);

Multi-dimensional fit
---------------------

Inputs and outputs are ``std::array``; the dimensions are deduced from the
callable:

.. code-block:: cpp

   auto bump = [](std::array<double, 2> x) -> std::array<double, 1> {
       return {std::exp(-100.0 * (x[0] - 0.5) * (x[0] - 0.5)
                               - (x[1] - 0.5) * (x[1] - 0.5))};
   };
   auto fn = treeweave::fit(bump, std::array{0.0, 0.0},
                            std::array{1.0, 1.0}, /*tol=*/1e-8);
   auto y  = fn(std::array{0.4, 0.6});   // std::array<double, 1>

``float`` works the same way — use ``float`` corners and a ``float``-returning
callable, and the approximant carries ``float`` throughout.

Options
-------

Pass a :doc:`treeweave::options <options>` as the trailing argument to override
the error metric, depth ceiling, memory budget, or uniform-refinement depth:

.. code-block:: cpp

   treeweave::options opts;
   opts.tol_kind       = treeweave::TolKind::AbsoluteMax;
   opts.max_memory_mib = 64;
   auto fn = treeweave::fit(runge, -1.0, 1.0, 1e-10, opts);

The leaf polynomial degree is a template parameter (default 7, the best across
the SIMD tuning campaign):

.. code-block:: cpp

   auto fn = treeweave::fit<5>(runge, -1.0, 1.0, 1e-8);  // degree-5 leaves

Thread safety
-------------

Once ``fit`` returns, the ``Function`` is immutable and ``operator()`` is safe
to call concurrently from many threads, provided each call writes to a disjoint
output slice. treeweave does not parallelize internally — chunk your inputs and
spawn threads yourself.

Build the examples
------------------

.. code-block:: bash

   cmake -S . -B build -DTREEWEAVE_BUILD_EXAMPLES=ON
   cmake --build build -j

Runnable sources live under
`examples/c++/ <https://github.com/DiamonDinoia/treeweave/tree/main/examples/c%2B%2B>`_.
