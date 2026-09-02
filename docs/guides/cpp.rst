C++
===

treeweave's C++ API is header-only. Include ``treeweave/treeweave.hpp`` and
call ``treeweave::fit``.

Install
-------

Download headers, no CMake
^^^^^^^^^^^^^^^^^^^^^^^^^^

.. literalinclude:: ../../tools/ci/docs-recipes.sh
   :language: bash
   :start-after: # BEGIN DOCS_DOWNLOAD_CXX_HEADERS
   :end-before: # END DOCS_DOWNLOAD_CXX_HEADERS
   :dedent: 4

Save the 1-D fit below as ``main.cpp``, then compile it with one ``-I``. The
bundle extracts ``include/treeweave/``, ``include/polyfit/`` and their
dependencies into the current directory:

.. literalinclude:: ../../tools/ci/docs-recipes.sh
   :language: bash
   :start-after: # BEGIN DOCS_CXX_HEADERS
   :end-before: # END DOCS_CXX_HEADERS
   :dedent: 4

The floating URL always resolves to the newest release; use
``.../releases/download/vX.Y.Z/treeweave-cxx-headers.tar.gz`` to pin a version.

CMake, FetchContent
^^^^^^^^^^^^^^^^^^^

.. literalinclude:: ../../examples/quickstart/cpp-fetchcontent/CMakeLists.txt
   :language: cmake
   :start-after: # BEGIN DOCS_PROJECT

CMake, CPM
^^^^^^^^^^

.. literalinclude:: ../../examples/quickstart/cpp-cpm/CMakeLists.txt
   :language: cmake
   :start-after: # BEGIN DOCS_PROJECT

CMake, find_package
^^^^^^^^^^^^^^^^^^^

``find_package(treeweave)`` against an installed prefix (or an extracted release
tarball) also gives the header-only C++ target:

.. literalinclude:: ../../examples/quickstart/cpp-find_package/CMakeLists.txt
   :language: cmake
   :start-after: # BEGIN DOCS_PROJECT

Every file above is a complete project under ``examples/quickstart/``.
``tools/ci/install-test.sh`` configures, builds and runs all of them on every
pull request, so what is printed here is what CI proved.

1-D fit
-------

.. literalinclude:: ../../examples/c++/simple1d.cpp
   :language: cpp

Multi-dimensional fit
---------------------

Inputs and outputs are ``std::array``. treeweave deduces the dimensions from
the callable:

.. literalinclude:: ../../examples/c++/simple2d.cpp
   :language: cpp
   :start-after: // BEGIN DOCS_MULTIDIM
   :end-before: // END DOCS_MULTIDIM
   :dedent: 4

``float`` works the same way: pass ``float`` corners and a ``float``-returning
callable.

Options
-------

Pass a :doc:`treeweave::options <options>` as the trailing argument:

.. literalinclude:: ../../examples/c++/with_options.cpp
   :language: cpp
   :start-after: // BEGIN DOCS_OPTIONS
   :end-before: // END DOCS_OPTIONS
   :dedent: 4

The leaf polynomial degree is a template parameter (default 7):

.. literalinclude:: ../../examples/c++/with_options.cpp
   :language: cpp
   :start-after: // BEGIN DOCS_DEGREE
   :end-before: // END DOCS_DEGREE
   :dedent: 4

Thread safety
-------------

A fitted ``Function`` is immutable; ``operator()`` is safe to call concurrently
from many threads. treeweave does not parallelize internally.

Build from source
-----------------

.. literalinclude:: ../../tools/ci/docs-recipes.sh
   :language: bash
   :start-after: # BEGIN DOCS_CLONE
   :end-before: # END DOCS_CLONE
   :dedent: 4

The ``dev-release`` preset consolidates every header the C++ API needs into
``build/dev-release/include``, so a non-CMake build needs one ``-I``:

.. literalinclude:: ../../tools/ci/docs-recipes.sh
   :language: bash
   :start-after: # BEGIN DOCS_ONEFLAG_CXX
   :end-before: # END DOCS_ONEFLAG_CXX
   :dedent: 4

After ``cmake --install build/dev-release --prefix P``, use ``-IP/include`` (or
nothing for a standard prefix). ``cd examples/c++ && make`` uses the generated
``build/dev-release/make.inc``.

The prebuilt C-ABI binary dispatches across the x86 SIMD ladder at runtime. The
header-only C++ path instead compiles into the consuming translation unit under
its own ``-march``. See :doc:`dispatch`.

Runnable sources:
`examples/c++/ <https://github.com/DiamonDinoia/treeweave/tree/main/examples/c%2B%2B>`_.

Experimental: the guru interface
--------------------------------

``<treeweave/guru.hpp>`` re-exposes the batch pipeline's stages (classify,
counting sort, per-run SIMD evaluation, gather) over caller-owned buffers, so
several fits can share one sort. It exists for functions that need several
fits stitched together, one per asymptotic regime for instance. It is
experimental and unstable; see :doc:`guru`.
