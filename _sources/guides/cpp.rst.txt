C++
===

treeweave's C++ API is header-only. Include ``treeweave/treeweave.hpp`` and
call ``treeweave::fit``.

Install
-------

Download headers, no CMake
^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: bash

   wget https://github.com/DiamonDinoia/treeweave/releases/latest/download/treeweave-cxx-headers.tar.gz
   tar xzf treeweave-cxx-headers.tar.gz   # -> ./include/treeweave/, ./include/polyfit/, ...
   g++ -std=c++20 -O3 -march=native examples/c++/simple1d.cpp -Iinclude -o simple1d
   ./simple1d

The floating URL always resolves to the newest release; use
``.../releases/download/vX.Y.Z/treeweave-cxx-headers.tar.gz`` to pin a version.

CMake, FetchContent
^^^^^^^^^^^^^^^^^^^

.. code-block:: cmake

   include(FetchContent)
   FetchContent_Declare(treeweave
     GIT_REPOSITORY https://github.com/DiamonDinoia/treeweave.git
     GIT_TAG stable)
   FetchContent_MakeAvailable(treeweave)
   target_link_libraries(my_app PRIVATE treeweave::treeweave)

CMake, CPM
^^^^^^^^^^

.. code-block:: cmake

   CPMAddPackage("gh:DiamonDinoia/treeweave@stable")
   target_link_libraries(my_app PRIVATE treeweave::treeweave)

1-D fit
-------

.. literalinclude:: ../../examples/c++/simple1d.cpp
   :language: cpp

Multi-dimensional fit
---------------------

Inputs and outputs are ``std::array``. treeweave deduces the dimensions from
the callable:

.. code-block:: cpp

   auto bump = [](std::array<double, 2> x) -> std::array<double, 1> {
       return {std::exp(-100.0 * (x[0] - 0.5) * (x[0] - 0.5)
                               - (x[1] - 0.5) * (x[1] - 0.5))};
   };
   auto fn = treeweave::fit(bump, std::array{0.0, 0.0},
                            std::array{1.0, 1.0}, /*tol=*/1e-8);
   auto y  = fn(std::array{0.4, 0.6});   // std::array<double, 1>

``float`` works the same way: pass ``float`` corners and a ``float``-returning
callable.

Options
-------

Pass a :doc:`treeweave::options <options>` as the trailing argument:

.. code-block:: cpp

   treeweave::options opts;
   opts.tol_kind       = treeweave::TolKind::AbsoluteMax;
   opts.max_memory_mib = 64;
   auto fn = treeweave::fit(zeta, 2.0, 10.0, 1e-10, opts);

The leaf polynomial degree is a template parameter (default 7):

.. code-block:: cpp

   auto fn = treeweave::fit<5>(zeta, 2.0, 10.0, 1e-8);  // degree-5 leaves

Thread safety
-------------

A fitted ``Function`` is immutable; ``operator()`` is safe to call concurrently
from many threads. treeweave does not parallelize internally.

Build from source
-----------------

.. code-block:: bash

   git clone https://github.com/DiamonDinoia/treeweave.git
   cmake -S treeweave -B build -DTREEWEAVE_BUILD_EXAMPLES=ON
   cmake --build build -j
   # one-flag compile against the consolidated header tree:
   g++ -std=c++20 -O3 -march=native examples/c++/simple1d.cpp -Ibuild/include -o simple1d

After ``cmake --install build --prefix P``, use ``-IP/include`` (or nothing for a
standard prefix). ``cd examples/c++ && make`` uses the generated ``build/make.inc``.

The prebuilt C-ABI binary dispatches across the x86 SIMD ladder at runtime. The
header-only C++ path instead compiles into the consuming translation unit under
its own ``-march``. See :doc:`dispatch`.

Runnable sources:
`examples/c++/ <https://github.com/DiamonDinoia/treeweave/tree/main/examples/c%2B%2B>`_.

Lower-level: the guru interface
-------------------------------

``treeweave/guru.hpp`` — the guru interface, after FFTW's — re-exposes the
C++ batch pipeline's stages for caller-driven fusion: one classify sweep per
point, one combined-key counting sort across several fits, per-run SIMD eval
with each regime's post-processing fused while the data is still hot, and a
``gather`` writeback — all on caller-owned scratch, no per-call allocation.
The sorted run walk is the same one ``Function::sorted`` dispatches from, so
library and user code share one path. See :doc:`guru` for the recipe, the
key-layout rules, and the worked example.
