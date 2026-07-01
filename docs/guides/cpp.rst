C++
===

treeweave's C++ API is header-only. Include the umbrella header and call
``treeweave::fit``:

.. code-block:: cpp

   #include <treeweave/treeweave.hpp>
   #include <cmath>   // std::pow, for the zeta example below

1-D fit
-------

.. code-block:: cpp

   // zeta_N(s) = sum_{k=1..N} k^-s — expensive; fit once, eval a polynomial.
   auto zeta = [](double s) {
       double a = 0.0;
       for (int k = 1; k <= 1000; ++k)
           a += std::pow(k, -s);
       return a;
   };

   auto   fn = treeweave::fit(zeta, 2.0, 10.0, /*tol=*/1e-10);
   double y  = fn(3.5);

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

   auto zeta = [](double s) {
       double a = 0.0;
       for (int k = 1; k <= 1000; ++k)
           a += std::pow(k, -s);
       return a;
   };

   treeweave::options opts;
   opts.tol_kind       = treeweave::TolKind::AbsoluteMax;
   opts.max_memory_mib = 64;
   auto fn = treeweave::fit(zeta, 2.0, 10.0, 1e-10, opts);

The leaf polynomial degree is a template parameter (default 7, the best across
the SIMD tuning campaign):

.. code-block:: cpp

   auto zeta = [](double s) {
       double a = 0.0;
       for (int k = 1; k <= 1000; ++k)
           a += std::pow(k, -s);
       return a;
   };
   auto fn = treeweave::fit<5>(zeta, 2.0, 10.0, 1e-8);  // degree-5 leaves

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

Compile without CMake
---------------------

The header-only API pulls in polyfit, POET, xsimd and mdspan, so ``-Iinclude``
alone won't compile. The configure step above consolidates every header into a
single ``build/include`` tree, so one flag suffices — xsimd-style:

.. code-block:: bash

   g++ -std=c++20 -O3 -march=native examples/c++/simple1d.cpp -Ibuild/include -o simple1d

After ``cmake --install build --prefix P`` the same headers land in
``P/include``, so a build against the install prefix is ``-IP/include`` (or
nothing, for a standard prefix). ``cd examples/c++ && make`` does the same via
the generated ``build/make.inc`` — see :doc:`../install` for the full recipe.
