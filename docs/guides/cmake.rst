CMake
=====

Prefer presets for local source builds, and use CPM or FetchContent when
vendoring treeweave into another project.

Vendored C++
------------

CPM.cmake:

.. code-block:: cmake

   CPMAddPackage("gh:DiamonDinoia/treeweave#stable")
   add_executable(my_app example.cpp)
   target_link_libraries(my_app PRIVATE treeweave::treeweave)

FetchContent:

.. code-block:: cmake

   include(FetchContent)
   FetchContent_Declare(
       treeweave
       GIT_REPOSITORY https://github.com/DiamonDinoia/treeweave.git
       GIT_TAG stable
   )

   FetchContent_MakeAvailable(treeweave)
   add_executable(my_app example.cpp)
   target_link_libraries(my_app PRIVATE treeweave::treeweave)

Targets
-------

.. list-table::
   :header-rows: 1
   :widths: 16 32 52

   * - Language
     - CMake target
     - Notes
   * - C++
     - ``treeweave::treeweave``
     - Header/interface target for ``#include <treeweave/treeweave.hpp>``. In an
       installed prefix it carries ``<prefix>/include`` and nothing else, so it
       is the CMake spelling of ``-I<prefix>/include``.
   * - C
     - ``treeweave::treeweave_c``
     - Shared C ABI, enabled by ``TREEWEAVE_BUILD_C_API=ON``.
   * - C
     - ``treeweave::treeweave_c_static``
     - Static C ABI, enabled by ``TREEWEAVE_BUILD_C_API=ON``.
   * - Fortran
     - ``treeweave_fortran``
     - Local target when ``TREEWEAVE_BUILD_FORTRAN=ON``.

Profiles
--------

Use ``cmake --preset <name>`` from a treeweave checkout:

.. list-table::
   :header-rows: 1
   :widths: 24 76

   * - Preset
     - What it sets
   * - ``dev-release``
     - ``Release`` build, ``TREEWEAVE_ARCH=native``; examples/tests use their top-level defaults.
   * - ``dev-debug``
     - ``Debug`` build with ``-Og -g`` and ``TREEWEAVE_ARCH=native``.
   * - ``multiarch``
     - ``TREEWEAVE_C_MULTIARCH=ON``, ``TREEWEAVE_ARCH=x86-64``; examples off.
   * - ``multiarch-arm``
     - ``TREEWEAVE_C_MULTIARCH=ON``, ``TREEWEAVE_ARCH=armv8-a``; examples off.
   * - ``bindings-matlab``
     - ``TREEWEAVE_BUILD_MATLAB=ON``; examples/tests off via ``lib-release``.
   * - ``bindings-octave``
     - Same CMake options as ``bindings-matlab``; intended for ``mkoctfile``/Octave builds.
   * - ``bindings-fortran``
     - ``TREEWEAVE_BUILD_FORTRAN=ON``; examples/tests off via ``lib-release``.

Options
-------

These are the user-facing CMake cache options:

.. literalinclude:: ../../CMakeLists.txt
   :language: cmake
   :start-after: # BEGIN TREEWEAVE_CMAKE_OPTIONS_DOCS
   :end-before: # END TREEWEAVE_CMAKE_OPTIONS_DOCS

Installed package
-----------------

.. code-block:: bash

   cmake --preset dev-release
   cmake --build build/dev-release --parallel
   cmake --install build/dev-release --prefix /install/prefix

.. code-block:: cmake

   find_package(treeweave REQUIRED)
   target_link_libraries(my_app PRIVATE treeweave::treeweave_c)

The header-only C++ API is not part of the installed
``find_package(treeweave)`` package because it depends on FetchContent-only
headers. Consume it in-tree via FetchContent or ``add_subdirectory``. The
installable part is the C ABI: ``treeweave::treeweave_c`` and
``treeweave::treeweave_c_static``.

Source build
------------

Requirements: a C++20 compiler and CMake 3.25 or newer. CMake fetches the
dependencies automatically.

.. code-block:: bash

   git clone https://github.com/DiamonDinoia/treeweave.git
   cd treeweave
   cmake --preset dev-release
   cmake --build build/dev-release -j
   ctest --test-dir build/dev-release

For a non-CMake C++ build, configure once to consolidate headers into
``<build>/include``:

.. code-block:: bash

   cmake --preset dev-release
   cmake --build build/dev-release
   g++ -std=c++20 -O3 -march=native examples/c++/simple1d.cpp -Ibuild/dev-release/include -o simple1d

The build writes ``build/dev-release/make.inc`` with ``CXX``, ``CXXFLAGS``,
and ``TREEWEAVE_INC``. ``examples/c++/Makefile`` includes it, so
``cd examples/c++ && make`` builds every example.
