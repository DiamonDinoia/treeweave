Fortran
=======

The Fortran binding is a thin ``iso_c_binding`` module (``treeweave``) over the C
ABI. Callbacks are ``bind(C)`` procedures; ``c_funloc`` yields the C-callable
pointer and ``context`` carries runtime parameters via ``c_loc`` / ``c_f_pointer``.

Minimal example
---------------

.. code-block:: fortran

   module kernels
       use, intrinsic :: iso_c_binding
       implicit none
   contains
       subroutine kernel_exp(x, y, context) bind(C)
           real(c_double), intent(in)  :: x(*)
           real(c_double), intent(out) :: y(*)
           type(c_ptr),    value       :: context
           y(1) = exp(x(1))
       end subroutine kernel_exp
   end module kernels

   program demo
       use, intrinsic :: iso_c_binding
       use treeweave
       use kernels
       implicit none
       type(c_ptr)    :: h
       real(c_double) :: a(1), b(1), x(1), y(1)

       a(1) = 0.0_c_double
       b(1) = 1.0_c_double
       h = treeweave_fit(c_funloc(kernel_exp), 1_c_int, 1_c_int, a, b, &
                         1.0e-10_c_double, c_null_ptr, c_null_ptr)
       if (.not. c_associated(h)) error stop treeweave_error_message()

       x(1) = 0.5_c_double
       call treeweave_eval(h, x, y)
       write (*, '(A,F0.12)') "exp(0.5) ~= ", y(1)
       h = treeweave_free(h)
   end program demo

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

Build
-----

.. code-block:: bash

   cmake --preset bindings-fortran
   cmake --build build/bindings-fortran -j
   ctest --test-dir build/bindings-fortran -R fortran_treeweave

A missing Fortran compiler is skipped gracefully. Full example:
`bindings/fortran/example.f90 <https://github.com/DiamonDinoia/treeweave/blob/main/bindings/fortran/example.f90>`_.
