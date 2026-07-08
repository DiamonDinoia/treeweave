! example.f90 — fit exp(x) and evaluate from Fortran via the treeweave C ABI.
! Mirrors examples/C/simple.c: plain bind(C) callback, scalar eval, batch eval.
! For the context pattern see README.md § "The context pattern".

! Callbacks must be bind(C) procedures so c_funloc() yields a C-callable
! pointer; module procedures are the cleanest way to provide them.
module example_kernels
    use, intrinsic :: iso_c_binding
    implicit none
contains
    subroutine kernel_exp(x, y, context) bind(C)
        real(c_double), intent(in)  :: x(*)
        real(c_double), intent(out) :: y(*)
        type(c_ptr),    value       :: context
        y(1) = exp(x(1))
    end subroutine kernel_exp
end module example_kernels


program treeweave_example
    use, intrinsic :: iso_c_binding
    use treeweave
    use example_kernels
    implicit none

    type(c_ptr)    :: h
    real(c_double) :: a(1), b(1), tol
    real(c_double) :: x(1), y(1)
    real(c_double) :: xs(11), res(11)
    real(c_double) :: exact, err, max_err
    integer        :: i

    a(1) = 0.0_c_double
    b(1) = 1.0_c_double
    tol  = 1.0e-10_c_double

    ! Fit exp(x) on [0, 1] syntax is
    ! treeweave_fit(callback, input_dim, output_dim, lower, upper, tolerance, context, options).
    h = treeweave_fit(c_funloc(kernel_exp), 1_c_int, 1_c_int, a, b, tol, &
                   c_null_ptr, c_null_ptr)
    if (.not. c_associated(h)) then
        write (*, '(2A)') "treeweave_fit failed: ", treeweave_error_message()
        error stop 1
    end if

    write (*, '(A,I0,A,I0,A,I0,A)') "fit exp(x): input_dim=", treeweave_input_dim(h), &
        " output_dim=", treeweave_output_dim(h), " memory=", treeweave_memory_usage(h), " bytes"

    x(1) = 0.5_c_double
    ! Evaluate h on (0.5) and print the result.
    call treeweave_eval(h, x, y)
    write (*, '(A,F0.12,A,F0.12)') "exp(0.5) approx=", y(1), " exact=", exp(0.5_c_double)

    ! Batch eval over 11 points on [0, 1].
    do i = 1, 11
        xs(i) = real(i - 1, c_double) / 10.0_c_double
    end do
    call treeweave_batch(h, xs, res, int(11, c_size_t))
    max_err = 0.0_c_double
    do i = 1, 11
        exact = exp(xs(i))
        err   = abs(res(i) - exact)
        if (err > max_err) max_err = err
    end do
    write (*, '(A,ES10.3)') "batch exp(x): max |approx - exact| = ", max_err

    if (max_err > 1.0e-8_c_double) then
        write (*, '(A)') "error too large"
        error stop 1
    end if

    h = treeweave_free(h)
    write (*, '(A)') "OK"
end program treeweave_example
