Installation
============

The fastest way to get treeweave depends on the language. Prebuilt binaries are
the easy path for Python and Julia; C/C++ projects integrate via CMake.

Prebuilt binaries (recommended)
-------------------------------

**Python** — treeweave is not on PyPI yet. Every push to ``main`` publishes a
staging wheel to `TestPyPI <https://test.pypi.org/project/treeweave/>`_; install
it with TestPyPI as the primary index and real PyPI for the dependencies:

.. code-block:: bash

   pip install --index-url https://test.pypi.org/simple/ \
               --extra-index-url https://pypi.org/simple/ treeweave

The x86-64 wheel bundles a runtime ISA dispatcher (SSE4.2 / AVX2 / AVX-512), so
one wheel runs on any x86-64 CPU. The C ABI is linked statically into the
extension — there is no shared library to vendor. Plain ``pip install treeweave``
will work once the first release is published to PyPI.

**Julia** — add the package; on first build it downloads the matching
``libtreeweave_c`` from the GitHub Release and caches it:

.. code-block:: julia

   using Pkg
   Pkg.add(url="https://github.com/DiamonDinoia/treeweave", subdir="bindings/julia/Treeweave")

Override the source repo with the ``TREEWEAVE_REPO`` environment variable, or
point ``LIBTREEWEAVE_C`` at a local build.

**C / Fortran** — download a relocatable C-ABI archive
(``treeweave-<version>-<platform>``) from the
`Releases page <https://github.com/DiamonDinoia/treeweave/releases>`_. Each
archive contains ``include/treeweave.h``, ``libtreeweave_c``, and a
``find_package(treeweave)`` CMake package.

**MATLAB / Octave** — built from source via CMake; there is no prebuilt MEX
(Octave has no stable MEX ABI across versions). See :doc:`guides/matlab`.

Use it in your CMake project
----------------------------

**FetchContent (easiest source path).** No install step — CMake fetches and
builds treeweave (and its header-only deps) for you:

.. code-block:: cmake

   include(FetchContent)
   FetchContent_Declare(
     treeweave
     GIT_REPOSITORY https://github.com/DiamonDinoia/treeweave.git
     GIT_TAG main
   )
   FetchContent_MakeAvailable(treeweave)

   target_link_libraries(my_app PRIVATE treeweave::treeweave)        # header-only C++
   # or, for the C ABI:
   target_link_libraries(my_app PRIVATE treeweave::treeweave_c)      # shared
   target_link_libraries(my_app PRIVATE treeweave::treeweave_c_static)  # static

**find_package (installed package).** Build and install once, then consume the
installable C-ABI package:

.. code-block:: bash

   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
   cmake --build build --parallel
   cmake --install build --prefix /your/prefix

.. code-block:: cmake

   find_package(treeweave REQUIRED)
   target_link_libraries(my_app PRIVATE treeweave::treeweave_c)

.. note::

   The header-only C++ template API (``treeweave::treeweave``) instantiates
   against FetchContent-only headers (polyfit / POET), so it is **not** part of
   the installed ``find_package(treeweave)`` package — consume it in-tree via
   FetchContent or ``add_subdirectory``. The installable, ``find_package``-able
   surface is the **C ABI** (``treeweave::treeweave_c`` /
   ``treeweave::treeweave_c_static``).

**add_subdirectory (vendored).**

.. code-block:: cmake

   add_subdirectory(extern/treeweave)
   target_link_libraries(my_app PRIVATE treeweave::treeweave)

Building from source
--------------------

Requirements: a **C++20** compiler and **CMake ≥ 3.25**. Dependencies (polyfit,
POET, Catch2) are fetched automatically.

.. code-block:: bash

   git clone https://github.com/DiamonDinoia/treeweave.git
   cmake -S treeweave -B build -DTREEWEAVE_BUILD_TESTS=ON
   cmake --build build -j
   ctest --test-dir build

Non-CMake C++ builds: add ``include/`` to your compiler include path and
``#include <treeweave/treeweave.hpp>``.
