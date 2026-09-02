! example.f90: fit exp(x) and evaluate from Fortran via the treeweave C ABI.
! Mirrors examples/C/simple.c: plain bind(C) callback, scalar eval, batch eval.
! For the context pattern see README.md § "The context pattern".

! Callbacks must be bind(C) procedures so c_funloc() yields a C-callable
! pointer; module procedures are the cleanest way to provide them.
module example_kernels
    use, intrinsic :: iso_c_binding
    implicit none

    ! Runtime parameters reach a bind(C) callback through the context pointer.
    ! BEGIN DOCS_CONTEXT_TYPE
    type, bind(C) :: params_t
        real(c_double) :: amplitude, frequency
    end type params_t
    ! END DOCS_CONTEXT_TYPE
contains
    subroutine kernel_exp(x, y, context) bind(C)
        real(c_double), intent(in)  :: x(*)
        real(c_double), intent(out) :: y(*)
        type(c_ptr),    value       :: context
        y(1) = exp(x(1))
    end subroutine kernel_exp

    ! BEGIN DOCS_CONTEXT_KERNEL
    subroutine kernel_wave(x, y, context) bind(C)
        real(c_double), intent(in)  :: x(*)
        real(c_double), intent(out) :: y(*)
        type(c_ptr),    value       :: context
        type(params_t), pointer     :: p
        call c_f_pointer(context, p)
        y(1) = p%amplitude * sin(p%frequency * x(1))
    end subroutine kernel_wave
    ! END DOCS_CONTEXT_KERNEL
end module example_kernels


program treeweave_example
    use, intrinsic :: iso_c_binding
    use treeweave
    use example_kernels
    implicit none

    type(c_ptr)    :: h, h_wave, h_abs
    real(c_double) :: a(1), b(1), tol
    real(c_double) :: x(1), y(1)
    real(c_double) :: xs(11), res(11), res_sorted(11)
    real(c_double) :: exact, err, max_err
    integer        :: i
    type(params_t), target       :: params
    type(treeweave_opts), target :: opts

    a(1) = 0.0_c_double
    b(1) = 1.0_c_double
    tol  = 1.0e-10_c_double

    ! BEGIN DOCS_MINIMAL
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
    ! END DOCS_MINIMAL

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

    ! xs is ascending by construction, so the sorted path applies.
    ! BEGIN DOCS_SORTED
    call treeweave_batch(h, xs, res, int(11, c_size_t))         ! any order
    call treeweave_sorted(h, xs, res_sorted, int(11, c_size_t)) ! xs(i) <= xs(i+1), 1-D
    ! END DOCS_SORTED
    max_err = 0.0_c_double
    do i = 1, 11
        err = abs(res_sorted(i) - res(i))
        if (err > max_err) max_err = err
    end do
    write (*, '(A,ES10.3)') "sorted vs batch: max difference = ", max_err
    if (max_err /= 0.0_c_double) then
        write (*, '(A)') "sorted and batch disagree"
        error stop 1
    end if

    ! The context pointer carries the parameters into the callback.
    params%amplitude = 2.0_c_double
    params%frequency = 3.0_c_double
    ! BEGIN DOCS_CONTEXT_CALL
    h_wave = treeweave_fit(c_funloc(kernel_wave), 1_c_int, 1_c_int, a, b, tol, &
                           c_loc(params), c_null_ptr)
    ! END DOCS_CONTEXT_CALL
    if (.not. c_associated(h_wave)) then
        write (*, '(2A)') "treeweave_fit failed: ", treeweave_error_message()
        error stop 1
    end if
    x(1) = 0.5_c_double
    call treeweave_eval(h_wave, x, y)
    exact = params%amplitude * sin(params%frequency * x(1))
    write (*, '(A,ES10.3)') "context fit at x=0.5: |approx - exact| = ", abs(y(1) - exact)
    if (abs(y(1) - exact) > 1.0e-8_c_double) then
        write (*, '(A)') "context fit error too large"
        error stop 1
    end if
    h_wave = treeweave_free(h_wave)

    ! Options travel as a c_loc() of a `target` treeweave_opts.
    ! BEGIN DOCS_OPTIONS
    opts = treeweave_default_opts()
    opts%tol_kind       = TREEWEAVE_ABSOLUTE_MAX
    opts%max_memory_mib = 64
    h_abs = treeweave_fit(c_funloc(kernel_exp), 1_c_int, 1_c_int, a, b, tol, &
                          c_null_ptr, c_loc(opts))
    ! END DOCS_OPTIONS
    if (.not. c_associated(h_abs)) then
        write (*, '(2A)') "treeweave_fit failed: ", treeweave_error_message()
        error stop 1
    end if
    call treeweave_eval(h_abs, x, y)
    write (*, '(A,ES10.3)') "absolute-max fit at x=0.5: |approx - exact| = ", &
        abs(y(1) - exp(x(1)))
    if (abs(y(1) - exp(x(1))) > 1.0e-8_c_double) then
        write (*, '(A)') "absolute-max fit error too large"
        error stop 1
    end if
    h_abs = treeweave_free(h_abs)

    h = treeweave_free(h)
    write (*, '(A)') "OK"
end program treeweave_example
