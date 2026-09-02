Fortran
=======

The Fortran binding is a thin ``iso_c_binding`` module (``treeweave``) over the C
ABI. Callbacks are ``bind(C)`` procedures; ``c_funloc`` yields the C-callable
pointer and ``context`` carries runtime parameters via ``c_loc`` / ``c_f_pointer``.

Install
-------

Prebuilt C ABI
^^^^^^^^^^^^^^

Fortran calls the C ABI, so the release C archive is enough alongside an
existing Fortran wrapper or build setup:

.. literalinclude:: ../../tools/ci/docs-recipes.sh
   :language: bash
   :start-after: # BEGIN DOCS_DOWNLOAD_C_TARBALL
   :end-before: # END DOCS_DOWNLOAD_C_TARBALL
   :dedent: 4

Extract it with ``tar xzf "treeweave-${PLATFORM}.tar.gz"``; the archive holds
``include/`` and ``lib/``. Windows ships ``treeweave-windows-x64.zip``.

Source build
^^^^^^^^^^^^

Build the ``treeweave_fortran`` target using the CMake preset:

.. literalinclude:: ../../tools/ci/docs-recipes.sh
   :language: bash
   :start-after: # BEGIN DOCS_CLONE
   :end-before: # END DOCS_CLONE
   :dedent: 4

.. literalinclude:: ../../tools/ci/docs-recipes.sh
   :language: bash
   :start-after: # BEGIN DOCS_FORTRAN_DEV
   :end-before: # END DOCS_FORTRAN_DEV
   :dedent: 4

Without a Fortran compiler, CMake skips the Fortran targets. The build writes
the generated ``treeweave`` module into the build tree.

Minimal example
---------------

.. literalinclude:: ../../bindings/fortran/example.f90
   :language: fortran

Carrying parameters through ``context``. Declare a ``bind(C)`` type:

.. literalinclude:: ../../bindings/fortran/example.f90
   :language: fortran
   :start-after: ! BEGIN DOCS_CONTEXT_TYPE
   :end-before: ! END DOCS_CONTEXT_TYPE
   :dedent: 4

Recover it inside the callback with ``c_f_pointer``:

.. literalinclude:: ../../bindings/fortran/example.f90
   :language: fortran
   :start-after: ! BEGIN DOCS_CONTEXT_KERNEL
   :end-before: ! END DOCS_CONTEXT_KERNEL
   :dedent: 4

At the call site, pass ``c_loc`` of a ``target`` instance as the context argument:

.. literalinclude:: ../../bindings/fortran/example.f90
   :language: fortran
   :start-after: ! BEGIN DOCS_CONTEXT_CALL
   :end-before: ! END DOCS_CONTEXT_CALL
   :dedent: 4

Batch & sorted eval
-------------------

.. literalinclude:: ../../bindings/fortran/example.f90
   :language: fortran
   :start-after: ! BEGIN DOCS_SORTED
   :end-before: ! END DOCS_SORTED
   :dedent: 4

``treeweave_sorted`` requires 1-D input (``input_dim == 1``). The caller
promises ascending order, and treeweave does not verify it.

Options
-------

Pass ``c_loc`` of a ``target`` ``treeweave_opts`` as the last argument (or
``c_null_ptr`` for the defaults):

.. literalinclude:: ../../bindings/fortran/example.f90
   :language: fortran
   :start-after: ! BEGIN DOCS_OPTIONS
   :end-before: ! END DOCS_OPTIONS
   :dedent: 4

See :doc:`options` for the full description of each field and tolerance kind.

Further
-------

.. literalinclude:: ../../tools/ci/docs-recipes.sh
   :language: bash
   :start-after: # BEGIN DOCS_FORTRAN_DEV
   :end-before: # END DOCS_FORTRAN_DEV
   :dedent: 4

.. literalinclude:: ../../tools/ci/docs-recipes.sh
   :language: bash
   :start-after: # BEGIN DOCS_FORTRAN_TEST
   :end-before: # END DOCS_FORTRAN_TEST
   :dedent: 4

Without a Fortran compiler, CMake skips the Fortran targets. Full example:
`bindings/fortran/example.f90 <https://github.com/DiamonDinoia/treeweave/blob/main/bindings/fortran/example.f90>`_.
