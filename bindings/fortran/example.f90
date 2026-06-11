! example.f90 — fit and evaluate from Fortran via the treeweave C ABI.
!
! Mirrors examples/C/simple.c and examples/C/with_context.c: fit exp(x) with a
! plain bind(C) callback, evaluate a single point and a batch, then show the
! `context` pattern — recovering a parameter struct inside the callback with
! c_f_pointer, the Fortran analogue of a C++ lambda's captures.

! Callbacks must be bind(C) procedures so c_funloc() yields a C-callable
! pointer; module procedures are the cleanest way to provide them.
module example_kernels
    use, intrinsic :: iso_c_binding
    implicit none

    ! Runtime parameters carried through `context` (cf. with_context.c).
    type, bind(C) :: params_t
        real(c_double) :: amplitude
        real(c_double) :: frequency
    end type params_t

contains

    ! f(x) = exp(x); context unused.
    subroutine kernel_exp(x, y, context) bind(C)
        real(c_double), intent(in)  :: x(*)
        real(c_double), intent(out) :: y(*)
        type(c_ptr),    value       :: context
        y(1) = exp(x(1))
    end subroutine kernel_exp

    ! f(x) = amplitude * sin(frequency * x); parameters recovered from context.
    subroutine kernel_ctx(x, y, context) bind(C)
        real(c_double), intent(in)  :: x(*)
        real(c_double), intent(out) :: y(*)
        type(c_ptr),    value       :: context
        type(params_t), pointer     :: p
        call c_f_pointer(context, p)
        y(1) = p%amplitude * sin(p%frequency * x(1))
    end subroutine kernel_ctx

end module example_kernels


program treeweave_example
    use, intrinsic :: iso_c_binding
    use treeweave
    use example_kernels
    implicit none

    type(c_ptr)            :: h
    real(c_double)         :: a(1), b(1), tol
    real(c_double)         :: x(1), y(1)
    real(c_double)         :: xs(11), res(11)
    real(c_double)         :: exact, err, max_err
    type(params_t), target :: params
    integer                :: i

    ! ---- 1. fit exp(x) on [0, 1] -------------------------------------------
    a(1) = 0.0_c_double
    b(1) = 1.0_c_double
    tol  = 1.0e-10_c_double

    ! c_null_ptr context (kernel ignores it), c_null_ptr opts (=> defaults).
    h = treeweave_fit(c_funloc(kernel_exp), 1_c_int, 1_c_int, a, b, tol, &
                   c_null_ptr, c_null_ptr)
    if (.not. c_associated(h)) then
        write (*, '(2A)') "treeweave_fit failed: ", treeweave_error_message()
        error stop 1
    end if

    write (*, '(A,I0,A,I0,A,I0,A)') "fit exp(x): input_dim=", treeweave_input_dim(h), &
        " output_dim=", treeweave_output_dim(h), " memory=", treeweave_memory_usage(h), " bytes"

    ! Single-point eval.
    x(1) = 0.5_c_double
    call treeweave_eval(h, x, y)
    write (*, '(A,F0.12,A,F0.12)') "exp(0.5) approx=", y(1), " exact=", exp(0.5_c_double)

    ! Batch eval over the fit domain [0, 1); the upper corner b is also
    ! evaluable as a convenience (it returns the boundary value).
    do i = 1, 11
        xs(i) = real(i - 1, c_double) / 11.0_c_double
    end do
    call treeweave_batch(h, xs, res, int(11, c_size_t))
    max_err = 0.0_c_double
    do i = 1, 11
        exact = exp(xs(i))
        err   = abs(res(i) - exact)
        if (err > max_err) max_err = err
    end do
    write (*, '(A,ES10.3)') "batch exp(x): max |approx - exact| = ", max_err

    h = treeweave_free(h)

    ! ---- 2. context demo: f(x) = amplitude * sin(frequency * x) ------------
    params%amplitude = 2.5_c_double
    params%frequency = 7.0_c_double

    h = treeweave_fit(c_funloc(kernel_ctx), 1_c_int, 1_c_int, a, b, tol, &
                   c_loc(params), c_null_ptr)
    if (.not. c_associated(h)) then
        write (*, '(2A)') "treeweave_fit (context) failed: ", treeweave_error_message()
        error stop 2
    end if

    max_err = 0.0_c_double
    do i = 1, 11
        x(1) = real(i - 1, c_double) / 11.0_c_double
        call treeweave_eval(h, x, y)
        exact = params%amplitude * sin(params%frequency * x(1))
        err   = abs(y(1) - exact)
        if (err > max_err) max_err = err
    end do
    write (*, '(A,F0.1,A,F0.1,A,ES10.3)') "fit ", params%amplitude, "*sin(", &
        params%frequency, "*x); max |approx - exact| = ", max_err

    h = treeweave_free(h)

    write (*, '(A)') "OK"
end program treeweave_example
