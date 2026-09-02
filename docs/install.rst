.. _install:

Installation and quick start
============================

Most languages have a precompiled binary. Prefer it: a source build of the C
library runs no faster, because the shipped binary already picks the best SIMD
variant for the host CPU at runtime (:doc:`guides/dispatch`).

Each section below is the shortest path to a running program. Every code block
on this page is a file in the repository that CI compiles and runs, so a recipe
that stops working is a failed build, not a stale page. The language guides
cover the other install routes, the options and the larger examples.

The program
-----------

Every C and C++ section on this page builds the same program. It fits a
1000-term series on ``[2, 10]`` to ten digits, then evaluates the approximation:

.. literalinclude:: ../examples/quickstart/main.cpp
   :language: cpp
   :lines: 8-

C++
---

The C++ API is header-only, so there is nothing to link. ``FetchContent`` comes
with CMake and needs no bootstrap:

.. literalinclude:: ../examples/quickstart/cpp-fetchcontent/CMakeLists.txt
   :language: cmake
   :lines: 4-

.. code-block:: bash

   cmake -S . -B build && cmake --build build && ./build/app

``stable`` is a branch that always points at the newest release. Use a tag
(``GIT_TAG v0.0.6``) to pin one.

No CMake? Download the header bundle and pass one ``-I``:

.. code-block:: bash

   curl -fLO https://github.com/DiamonDinoia/treeweave/releases/latest/download/treeweave-cxx-headers.tar.gz
   tar xzf treeweave-cxx-headers.tar.gz          # -> ./include/
   c++ -std=c++20 -O3 -march=native main.cpp -Iinclude -o app && ./app

:doc:`guides/cpp` covers CPM, ``find_package``, several dimensions, vector
output and the degree template parameter.

C
-

The C library ships as a tarball per platform, containing ``include/``,
``lib/`` and a ``find_package(treeweave)`` CMake package. Here is the same
program in C:

.. literalinclude:: ../examples/quickstart/main.c
   :language: c
   :lines: 7-

Download, then compile against it directly:

.. code-block:: bash

   PLATFORM=linux-x86_64      # or linux-aarch64, macos-arm64, macos-x86_64, windows-x64
   URL="https://github.com/DiamonDinoia/treeweave/releases/latest/download/treeweave-${PLATFORM}.tar.gz"
   curl -fLO "$URL" && tar xzf "treeweave-${PLATFORM}.tar.gz"
   cc main.c -Iinclude -Llib -ltreeweave_c -lm -o app
   LD_LIBRARY_PATH=lib ./app

``latest/download`` always resolves to the newest release. Pin one by naming
its tag instead: ``releases/download/v0.0.6/treeweave-0.0.6-linux-x86_64.tar.gz``.

Or let CMake find the extracted tarball:

.. literalinclude:: ../examples/quickstart/c-find_package/CMakeLists.txt
   :language: cmake
   :lines: 5-

.. code-block:: bash

   cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/extracted/tarball
   cmake --build build && ./build/app

.. warning::

   Windows has no ``RPATH``. The zip puts ``treeweave_c.dll`` in ``bin\`` and the
   import library in ``lib\``, so linking succeeds and the executable then fails
   to start. Put ``bin\`` on ``PATH``, copy the DLL next to the executable (the
   CMake file above does), or link ``treeweave::treeweave_c_static``.

:doc:`guides/c` documents every entry point, the ``float`` twins and the
options struct.

Python
------

.. code-block:: bash

   pip install treeweave

.. literalinclude:: ../bindings/python/examples/simple_1d.py
   :language: python

See :doc:`guides/python`.

Julia
-----

.. not-run-in-ci: fetches a published release; the same path is exercised by julia-smoke.yml.

.. code-block:: julia

   using Pkg
   Pkg.add(url="https://github.com/DiamonDinoia/treeweave",
           subdir="bindings/julia/Treeweave")

.. literalinclude:: ../bindings/julia/Treeweave/examples/example_1d.jl
   :language: julia

See :doc:`guides/julia`.

MATLAB and Octave
-----------------

With `mip <https://mip.sh/>`_, from the `mip-org/labs
<https://github.com/mip-org/mip-labs>`_ channel:

.. not-run-in-ci: installs a published channel package; the MEX bundle itself is built and tested by matlab.yml.

.. code-block:: matlab

   mip install --channel mip-org/labs treeweave
   mip load treeweave

.. literalinclude:: ../bindings/matlab/examples/example_1d.m
   :language: matlab

Octave has no prebuilt bundle; build the MEX from source with
``cmake --preset bindings-octave``. See :doc:`guides/matlab` for both, and for
the direct MATLAB bundle download.

Fortran
-------

.. code-block:: bash

   cmake --preset bindings-fortran
   cmake --build build/bindings-fortran -j

.. literalinclude:: ../bindings/fortran/example.f90
   :language: fortran

See :doc:`guides/fortran`.

JavaScript and TypeScript
-------------------------

.. code-block:: bash

   npm install @flatironinstitute/treeweave

.. literalinclude:: ../bindings/js/examples/simple_1d.mjs
   :language: js

See :doc:`guides/js`.

Building from source
--------------------

.. code-block:: bash

   git clone https://github.com/DiamonDinoia/treeweave.git
   cd treeweave
   cmake --preset dev-release
   cmake --build build/dev-release -j
   ctest --test-dir build/dev-release --output-on-failure

:doc:`guides/cmake` lists the presets, the build options, the exported targets
and the install layout.
