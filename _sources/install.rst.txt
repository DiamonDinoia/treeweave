Installation
============

Start with the one-liner for your language. If you need a source build, the last two sections cover it.

Prebuilt binaries
-----------------

**Python:**

.. code-block:: bash

   pip install treeweave

The x86-64 wheel bundles a runtime ISA dispatcher (SSE4.2 / AVX2 / AVX-512). The C ABI is linked statically — no shared library to vendor.

To test an unreleased change, every push to ``main`` publishes a staging wheel to `TestPyPI <https://test.pypi.org/project/treeweave/>`_:

.. code-block:: bash

   pip install --index-url https://test.pypi.org/simple/ \
               --extra-index-url https://pypi.org/simple/ treeweave

**Julia:**

.. code-block:: julia

   using Pkg
   Pkg.add(url="https://github.com/DiamonDinoia/treeweave", subdir="bindings/julia/Treeweave")

On first build, this downloads the matching ``libtreeweave_c`` from the GitHub Release automatically. Override the source repo with the ``TREEWEAVE_REPO`` environment variable.

**MATLAB** — prebuilt MEX bundles ship with each release. Download
``treeweave-matlab-<version>-<platform>`` (``linux-x64``, ``windows-x64``,
``macos-arm64``, ``macos-x64``) from the
`Releases page <https://github.com/DiamonDinoia/treeweave/releases>`_, extract,
and ``addpath`` the directory — the MEX statically links the C ABI and needs no build step.

**Octave** — no prebuilt MEX (Octave has no stable MEX ABI across versions). Build from source:

.. code-block:: bash

   cmake --preset bindings-octave
   cmake --build build/bindings-octave -j

See :doc:`guides/matlab` for the full recipe.

**C / Fortran** — download a ``treeweave-<version>-<platform>`` archive from the
`Releases page <https://github.com/DiamonDinoia/treeweave/releases>`_. Each
archive contains ``include/treeweave.h``, ``libtreeweave_c``, and a
``find_package(treeweave)`` CMake package.

.. note::

   The prebuilt ``libtreeweave_c`` binary dispatches SSE4.2/AVX2/AVX-512 at
   runtime — the downloaded binary already runs near-native speed on any
   x86-64 host. Compiling from source does not add performance for C, Fortran,
   Python, Julia, or MATLAB consumers. The exception is the header-only C++
   API, which compiles into your translation unit with your own flags (e.g.
   ``-march=native``). See :doc:`guides/dispatch` for details.

**C++ header-only (no CMake)** — the headers are platform-independent. Download, extract, compile:

.. code-block:: bash

   # Latest stable release (floating URL — never needs bumping):
   wget https://github.com/DiamonDinoia/treeweave/releases/latest/download/treeweave-cxx-headers.tar.gz
   tar xzf treeweave-cxx-headers.tar.gz   # -> ./include/treeweave/..., ./include/polyfit/..., ...
   g++ -std=c++20 -O3 -march=native demo.cpp -Iinclude -o demo

Pick the URL for the channel you want — the tarball layout is identical:

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

``unstable`` is refreshed from every green main CI — no stability promise.
The bundle carries every transitive header (treeweave, polyfit, POET, xsimd, mdspan) under one ``include/``.
See the runnable `examples/standalone/ <https://github.com/DiamonDinoia/treeweave/tree/main/examples/standalone>`_ example.

Use it in your CMake project
----------------------------

**CPM.cmake (one-liner):**

.. code-block:: cmake

   CPMAddPackage("gh:DiamonDinoia/treeweave@0.0.0")
   target_link_libraries(my_app PRIVATE treeweave::treeweave)   # header-only C++

**FetchContent:**

.. code-block:: cmake

   include(FetchContent)
   FetchContent_Declare(treeweave
     GIT_REPOSITORY https://github.com/DiamonDinoia/treeweave.git
     GIT_TAG v0.0.0)   # pin a release tag for reproducibility
   FetchContent_MakeAvailable(treeweave)

   target_link_libraries(my_app PRIVATE treeweave::treeweave)        # header-only C++
   # or, for the C ABI:
   target_link_libraries(my_app PRIVATE treeweave::treeweave_c)      # shared
   target_link_libraries(my_app PRIVATE treeweave::treeweave_c_static)  # static

**Installed package:**

.. code-block:: bash

   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
   cmake --build build --parallel
   cmake --install build --prefix /your/prefix

.. code-block:: cmake

   find_package(treeweave REQUIRED)
   target_link_libraries(my_app PRIVATE treeweave::treeweave_c)

.. note::

   The header-only C++ API is **not** part of the installed ``find_package(treeweave)`` package
   (it depends on FetchContent-only headers). Consume it in-tree via FetchContent or
   ``add_subdirectory``. The installable surface is the C ABI
   (``treeweave::treeweave_c`` / ``treeweave::treeweave_c_static``).

**Vendored:**

.. code-block:: cmake

   add_subdirectory(extern/treeweave)
   target_link_libraries(my_app PRIVATE treeweave::treeweave)

Building from source
--------------------

Requirements: a **C++20** compiler and **CMake ≥ 3.25**. Dependencies (polyfit, POET, Catch2) are fetched automatically.

.. code-block:: bash

   git clone https://github.com/DiamonDinoia/treeweave.git
   cmake -S treeweave -B build -DTREEWEAVE_BUILD_TESTS=ON
   cmake --build build -j
   ctest --test-dir build

For a non-CMake C++ build, configure once to consolidate headers into ``<build>/include``:

.. code-block:: bash

   cmake -S treeweave -B build -DTREEWEAVE_BUILD_EXAMPLES=ON
   cmake --build build
   g++ -std=c++20 -O3 -march=native examples/c++/simple1d.cpp -Ibuild/include -o simple1d

The build writes ``build/make.inc`` (``CXX``, ``CXXFLAGS``, ``TREEWEAVE_INC``);
``examples/c++/Makefile`` includes it, so ``cd examples/c++ && make`` builds every example.

.. _julia-from-source:

**Julia from source.** Build the C ABI, then ``develop`` the package against the sibling build:

.. code-block:: bash

   git clone https://github.com/DiamonDinoia/treeweave.git
   cmake --preset bindings-julia
   cmake --build build/bindings-julia --target treeweave_c

.. code-block:: julia

   using Pkg
   Pkg.develop(path="treeweave/bindings/julia/Treeweave")
   Pkg.build("Treeweave")   # finds the sibling build/bindings-julia/libtreeweave_c

**Python from source:**

.. code-block:: bash

   git clone https://github.com/DiamonDinoia/treeweave.git
   pip install ./treeweave/bindings/python
