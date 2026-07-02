Installation
============

The fastest way to get treeweave depends on the language. Every language offers
two paths: **from a binary release** (prebuilt, least effort) and **from source**.

Prebuilt / release binaries
---------------------------

**Python** — install the latest release from PyPI:

.. code-block:: bash

   pip install treeweave

The x86-64 wheel bundles a runtime ISA dispatcher (SSE4.2 / AVX2 / AVX-512), so
one wheel runs on any x86-64 CPU. The C ABI is linked statically into the
extension — there is no shared library to vendor.

To test an unreleased change, every push to ``main`` publishes a staging wheel
(``X.Y.Z.devN``) to `TestPyPI <https://test.pypi.org/project/treeweave/>`_;
install it with TestPyPI as the primary index and real PyPI for the dependencies:

.. code-block:: bash

   pip install --index-url https://test.pypi.org/simple/ \
               --extra-index-url https://pypi.org/simple/ treeweave

**Julia (from a release)** — add the package; on first build it downloads the
matching ``libtreeweave_c`` from the GitHub Release and caches it:

.. code-block:: julia

   using Pkg
   Pkg.add(url="https://github.com/DiamonDinoia/treeweave", subdir="bindings/julia/Treeweave")

Override the release's source repo with the ``TREEWEAVE_REPO`` environment
variable. (See :ref:`Julia from source <julia-from-source>` to build without a
release.)

**MATLAB / Octave** — built from source via CMake; there is no prebuilt MEX
(Octave has no stable MEX ABI across versions). See :doc:`guides/matlab`.

**C / Fortran** — download a relocatable C-ABI archive
(``treeweave-<version>-<platform>``) from the
`Releases page <https://github.com/DiamonDinoia/treeweave/releases>`_. Each
archive contains ``include/treeweave.h``, ``libtreeweave_c``, and a
``find_package(treeweave)`` CMake package.

**C++ header-only drop-in (no CMake)** — the simplest path if you just want the
C++ API and don't use CMake. The headers are platform-independent, so one
arch-independent archive covers every OS. Download, extract, ``-Iinclude``:

.. code-block:: bash

   # Latest stable release (floating URL — never needs bumping):
   wget https://github.com/DiamonDinoia/treeweave/releases/latest/download/treeweave-cxx-headers.tar.gz
   tar xzf treeweave-cxx-headers.tar.gz             # -> ./include/treeweave/..., ./include/polyfit/..., ...
   g++ -std=c++20 -O3 -march=native demo.cpp -Iinclude -o demo

The asset name is unversioned, so pick the URL for the channel you want — the
tarball layout is identical:

.. list-table::
   :header-rows: 1
   :widths: 20 80

   * - Channel
     - URL
   * - Latest stable
     - ``.../releases/latest/download/treeweave-cxx-headers.tar.gz``
   * - Pinned version
     - ``.../releases/download/vX.Y.Z/treeweave-cxx-headers.tar.gz``
   * - Unstable (bleeding edge)
     - ``.../releases/download/unstable/treeweave-cxx-headers.tar.gz``

``latest`` always resolves to the newest tagged release; pin ``vX.Y.Z`` for
reproducibility. ``unstable`` is a rolling prerelease refreshed from every green
main CI (bleeding edge, no stability promise) — it stays out of ``latest``.

The bundle carries every transitive header (treeweave, polyfit, POET, xsimd,
mdspan) under one ``include/`` — no dependency hunting. See the runnable
`examples/standalone/ <https://github.com/DiamonDinoia/treeweave/tree/main/examples/standalone>`_
example. (Already grabbing the per-platform ``treeweave-<version>-<platform>``
archive for the C ABI? It carries the identical ``include/`` — no separate
download needed.)

Use it in your CMake project
----------------------------

**FetchContent (easiest source path).** No install step — CMake fetches and
builds treeweave (and its header-only deps) for you:

.. code-block:: cmake

   include(FetchContent)
   FetchContent_Declare(
     treeweave
     GIT_REPOSITORY https://github.com/DiamonDinoia/treeweave.git
     GIT_TAG v0.0.0  # pin a release tag or commit SHA for reproducibility
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

Non-CMake C++ builds: the header-only C++ API pulls in polyfit, POET, xsimd and
mdspan. Rather than track down four include paths, let CMake consolidate them
into a single tree. Configure once — it fetches the deps and merges every header
(treeweave's own plus the four deps) into ``<build>/include``:

.. code-block:: bash

   cmake -S treeweave -B build -DTREEWEAVE_BUILD_EXAMPLES=ON
   cmake --build build

Then compile with one include flag, xsimd-style:

.. code-block:: bash

   g++ -std=c++20 -O3 -march=native examples/c++/simple1d.cpp -Ibuild/include -o simple1d

The build also writes a FINUFFT-style ``build/make.inc`` (``CXX``, ``CXXFLAGS``,
``TREEWEAVE_INC``); ``examples/c++/Makefile`` includes it, so
``cd examples/c++ && make`` builds every example (``make -n simple1d`` prints the
exact command).

**Against an installed treeweave.** ``cmake --install`` ships the same
consolidated headers, so a build against the install prefix is just
``-I<prefix>/include`` — or nothing, for a standard prefix already on the
compiler's search path:

.. code-block:: bash

   cmake --install build --prefix /your/prefix
   g++ -std=c++20 -O3 -march=native simple1d.cpp -I/your/prefix/include -o simple1d

.. _julia-from-source:

**Julia (from source).** Build the C ABI in a checkout, then ``develop`` the
package against it — no release needed. ``Pkg.build`` resolves
``libtreeweave_c`` from a sibling ``build*/`` directory automatically (or from
the ``LIBTREEWEAVE_C`` environment variable if you point it at any local build):

.. code-block:: bash

   git clone https://github.com/DiamonDinoia/treeweave.git
   cmake --preset bindings-julia
   cmake --build build/bindings-julia --target treeweave_c

.. code-block:: julia

   using Pkg
   Pkg.develop(path="treeweave/bindings/julia/Treeweave")
   Pkg.build("Treeweave")   # finds the sibling build/bindings-julia/libtreeweave_c

**Python (from source).** ``pip install`` the binding directory; scikit-build-core
builds and statically links the C ABI into the extension:

.. code-block:: bash

   git clone https://github.com/DiamonDinoia/treeweave.git
   pip install ./treeweave/bindings/python
