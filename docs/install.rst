.. _install:

Installation and quick start
============================

Most languages have a precompiled binary. Prefer it: a source build of the C
library runs no faster, because the shipped binary already picks the best SIMD
variant for the host CPU at runtime (:doc:`guides/dispatch`).

Each section below is the shortest path to a running program. Every code block
on this page, program and shell recipe alike, is a marked region of a file that
CI compiles or executes, so a recipe that stops working is a failed build, not a
stale page. The programs live under ``examples/quickstart/``; the shell recipes
live in ``tools/ci/docs-recipes.sh``. Two blocks are exempt because they install
a published package rather than build one, the Julia ``Pkg.add`` and the MATLAB
``mip install``; each carries the workflow that covers it instead. The language
guides cover the other install routes, the options and the larger examples.

The program
-----------

Every C and C++ section on this page builds the same program. It fits a
1000-term series on ``[2, 10]`` to ten digits, then evaluates the approximation:

.. literalinclude:: ../examples/quickstart/main.cpp
   :language: cpp
   :start-after: // BEGIN DOCS_PROGRAM

C++
---

The C++ API is header-only, so there is nothing to link. ``FetchContent`` comes
with CMake and needs no bootstrap:

.. literalinclude:: ../examples/quickstart/cpp-fetchcontent/CMakeLists.txt
   :language: cmake
   :start-after: # BEGIN DOCS_PROJECT

.. literalinclude:: ../tools/ci/docs-recipes.sh
   :language: bash
   :start-after: # BEGIN DOCS_QUICKSTART_BUILD
   :end-before: # END DOCS_QUICKSTART_BUILD
   :dedent: 4

``stable`` is a branch that always points at the newest release. Use a tag
(``GIT_TAG vX.Y.Z``) to pin one.

No CMake? Download the header bundle:

.. literalinclude:: ../tools/ci/docs-recipes.sh
   :language: bash
   :start-after: # BEGIN DOCS_DOWNLOAD_CXX_HEADERS
   :end-before: # END DOCS_DOWNLOAD_CXX_HEADERS
   :dedent: 4

Save the program above as ``main.cpp``, then compile it with one ``-I``:

.. literalinclude:: ../tools/ci/docs-recipes.sh
   :language: bash
   :start-after: # BEGIN DOCS_CXX_HEADERS
   :end-before: # END DOCS_CXX_HEADERS
   :dedent: 4

:doc:`guides/cpp` covers CPM, ``find_package``, several dimensions, vector
output and the degree template parameter.

C
-

The C library ships as a tarball per platform, containing ``include/``,
``lib/`` and a ``find_package(treeweave)`` CMake package. Here is the same
program in C:

.. literalinclude:: ../examples/quickstart/main.c
   :language: c
   :start-after: /* BEGIN DOCS_PROGRAM */

Download the tarball for the platform:

.. literalinclude:: ../tools/ci/docs-recipes.sh
   :language: bash
   :start-after: # BEGIN DOCS_DOWNLOAD_C_TARBALL
   :end-before: # END DOCS_DOWNLOAD_C_TARBALL
   :dedent: 4

Save the program above as ``main.c``, then compile against the tarball
directly:

.. literalinclude:: ../tools/ci/docs-recipes.sh
   :language: bash
   :start-after: # BEGIN DOCS_C_TARBALL
   :end-before: # END DOCS_C_TARBALL
   :dedent: 4

``latest/download`` always resolves to the newest release. Pin one by naming
its tag instead: ``releases/download/vX.Y.Z/treeweave-X.Y.Z-linux-x86_64.tar.gz``.
Windows ships the same tree as ``treeweave-windows-x64.zip``.

Or let CMake find the extracted tarball:

.. literalinclude:: ../examples/quickstart/c-find_package/CMakeLists.txt
   :language: cmake
   :start-after: # BEGIN DOCS_PROJECT

.. literalinclude:: ../tools/ci/docs-recipes.sh
   :language: bash
   :start-after: # BEGIN DOCS_QUICKSTART_PREFIX
   :end-before: # END DOCS_QUICKSTART_PREFIX
   :dedent: 4

.. warning::

   Windows has no ``RPATH``. The zip puts ``treeweave_c.dll`` in ``bin\`` and the
   import library in ``lib\``, so linking succeeds and the executable then fails
   to start. Put ``bin\`` on ``PATH``, copy the DLL next to the executable (the
   CMake file above does), or link ``treeweave::treeweave_c_static``.

:doc:`guides/c` documents every entry point, the ``float`` twins and the
options struct.

Python
------

.. literalinclude:: ../tools/ci/docs-recipes.sh
   :language: bash
   :start-after: # BEGIN DOCS_PIP_PYPI
   :end-before: # END DOCS_PIP_PYPI
   :dedent: 4

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

.. literalinclude:: ../tools/ci/docs-recipes.sh
   :language: bash
   :start-after: # BEGIN DOCS_FORTRAN_DEV
   :end-before: # END DOCS_FORTRAN_DEV
   :dedent: 4

.. literalinclude:: ../bindings/fortran/example.f90
   :language: fortran

See :doc:`guides/fortran`.

JavaScript and TypeScript
-------------------------

.. literalinclude:: ../tools/ci/docs-recipes.sh
   :language: bash
   :start-after: # BEGIN DOCS_NPM
   :end-before: # END DOCS_NPM
   :dedent: 4

.. literalinclude:: ../bindings/js/examples/simple_1d.mjs
   :language: js

See :doc:`guides/js`.

Building from source
--------------------

Clone the repository:

.. literalinclude:: ../tools/ci/docs-recipes.sh
   :language: bash
   :start-after: # BEGIN DOCS_CLONE
   :end-before: # END DOCS_CLONE
   :dedent: 4

Configure and build:

.. literalinclude:: ../tools/ci/docs-recipes.sh
   :language: bash
   :start-after: # BEGIN DOCS_DEV_BUILD
   :end-before: # END DOCS_DEV_BUILD
   :dedent: 4

Then run the suite:

.. literalinclude:: ../tools/ci/docs-recipes.sh
   :language: bash
   :start-after: # BEGIN DOCS_DEV_TEST
   :end-before: # END DOCS_DEV_TEST
   :dedent: 4

:doc:`guides/cmake` lists the presets, the build options, the exported targets
and the install layout. :doc:`releasing` is the maintainer's release procedure.
