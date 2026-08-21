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

.. code-block:: bash

   VER=stable
   PLATFORM=linux-x86_64
   URL="https://github.com/DiamonDinoia/treeweave/releases/download/${VER}/treeweave-${VER}-${PLATFORM}.tar.gz"
   curl -fLO "$URL" || wget "$URL"
   tar xzf "treeweave-${VER}-${PLATFORM}.tar.gz"

Source build
^^^^^^^^^^^^

Build the ``treeweave_fortran`` target using the CMake preset:

.. code-block:: bash

   git clone https://github.com/DiamonDinoia/treeweave.git
   cd treeweave
   cmake --preset bindings-fortran
   cmake --build build/bindings-fortran -j

Without a Fortran compiler, CMake skips the Fortran targets. The build writes
the generated ``treeweave`` module into the build tree.

Minimal example
---------------

.. literalinclude:: ../../bindings/fortran/example.f90
   :language: fortran

Carrying parameters through ``context``:

.. code-block:: fortran

   type, bind(C) :: params_t
       real(c_double) :: amplitude, frequency
   end type params_t
   ! ... in the callback:
   type(params_t), pointer :: p
   call c_f_pointer(context, p)
   y(1) = p%amplitude * sin(p%frequency * x(1))
   ! ... at the call site: pass c_loc(params) as the context argument.

Batch & sorted eval
-------------------

.. code-block:: fortran

   real(c_double) :: xs(1000), res(1000)
   integer(c_size_t) :: n = 1000_c_size_t
   call treeweave_batch(h, xs, res, n)    ! batch: many points, any order
   call treeweave_sorted(h, xs, res, n)   ! promise xs(i) <= xs(i+1), 1-D; ~3-4x faster

``treeweave_sorted`` requires 1-D input (``input_dim == 1``). The caller
promises ascending order, and treeweave does not verify it.

Options
-------

Pass an options struct via the last argument (or ``c_null_ptr`` for defaults):

.. code-block:: fortran

   use treeweave
   type(treeweave_opts_t) :: opts
   opts = treeweave_default_opts()
   opts%tol_kind       = TREEWEAVE_ABSOLUTE_MAX
   opts%max_memory_mib = 64
   h = treeweave_fit(c_funloc(kernel), 1_c_int, 1_c_int, a, b, 1.0e-10_c_double, &
                     c_null_ptr, c_loc(opts))

See :doc:`options` for the full description of each field and tolerance kind.

Further
-------

.. code-block:: bash

   cmake --preset bindings-fortran
   cmake --build build/bindings-fortran -j
   ctest --test-dir build/bindings-fortran -R fortran_treeweave

Without a Fortran compiler, CMake skips the Fortran targets. Full example:
`bindings/fortran/example.f90 <https://github.com/DiamonDinoia/treeweave/blob/main/bindings/fortran/example.f90>`_.
