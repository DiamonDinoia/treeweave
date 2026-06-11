! lgamma_bench.f90 — treeweave vs the Fortran intrinsic log_gamma, via the C ABI.
!
! The Fortran member of the cross-language lgamma benchmark family (see
! examples/c++/lgamma_bench.cpp for the rationale). log-Gamma is fit on [3, 50)
! — smooth, positive, monotone, so relative error is well defined — with
! treeweave's default RelativeMax tolerance, then compared to the F2008 intrinsic
! log_gamma on max relative error, throughput, and speedup.

module lgamma_bench_kernels
    use, intrinsic :: iso_c_binding
    implicit none
contains
    ! f(x) = log_gamma(x); context unused.
    subroutine kernel_lgamma(x, y, context) bind(C)
        real(c_double), intent(in)  :: x(*)
        real(c_double), intent(out) :: y(*)
        type(c_ptr),    value       :: context
        y(1) = log_gamma(x(1))
    end subroutine kernel_lgamma
end module lgamma_bench_kernels


program lgamma_bench
    use, intrinsic :: iso_c_binding
    use treeweave
    use lgamma_bench_kernels
    implicit none

    integer, parameter        :: n = 1000000
    type(c_ptr)               :: h
    real(c_double)            :: a(1), b(1), tol
    real(c_double), allocatable :: xs(:), res(:)
    real(c_double)            :: approx, exact, rel, max_rel
    real(c_double)            :: tw_s, lib_s
    real(c_double), volatile  :: sink         ! defeats dead-code elimination
    integer(c_int64_t)        :: c0, c1, rate
    integer                   :: i

    a(1) = 3.0_c_double
    b(1) = 50.0_c_double
    tol  = 1.0e-10_c_double

    ! c_null_ptr opts => defaults, whose tol_kind is RELATIVE_MAX (the right
    ! measure for this zero-free, monotone function).
    h = treeweave_fit(c_funloc(kernel_lgamma), 1_c_int, 1_c_int, a, b, tol, &
                      c_null_ptr, c_null_ptr)
    if (.not. c_associated(h)) then
        write (*, '(2A)') "treeweave_fit failed: ", treeweave_error_message()
        error stop 1
    end if

    allocate (xs(n), res(n))
    call random_seed()
    call random_number(xs)                 ! xs in [0,1)
    xs = a(1) + (b(1) - a(1)) * xs         ! map into [3, 50)

    ! ---- accuracy vs the intrinsic ----------------------------------------
    max_rel = 0.0_c_double
    do i = 1, n
        exact  = log_gamma(xs(i))
        approx = treeweave_eval_1d(h, xs(i))
        rel    = abs(approx - exact) / abs(exact)
        if (rel > max_rel) max_rel = rel
    end do

    ! ---- throughput: treeweave batch vs the intrinsic ----------------------
    call system_clock(count_rate=rate)

    call treeweave_batch(h, xs, res, int(n, c_size_t))   ! warm-up (untimed)
    sink = res(1)

    call system_clock(c0)
    call treeweave_batch(h, xs, res, int(n, c_size_t))   ! timed
    call system_clock(c1)
    sink = res(1)
    tw_s = real(c1 - c0, c_double) / real(rate, c_double)

    call system_clock(c0)
    do i = 1, n
        sink = sink + log_gamma(xs(i))
    end do
    call system_clock(c1)
    lib_s = real(c1 - c0, c_double) / real(rate, c_double)

    write (*, '(A,F0.1,A,F0.1,A,ES8.1)') "lgamma fit on [", a(1), ", ", b(1), "), relative tol ", tol
    write (*, '(A,ES10.3)')  "  max rel err: ", max_rel
    write (*, '(A,F0.1,A)')  "  treeweave:  ", real(n, c_double) / (tw_s * 1.0e6_c_double), " Mevals/s"
    write (*, '(A,F0.1,A)')  "  library: ", real(n, c_double) / (lib_s * 1.0e6_c_double), " Mevals/s"
    write (*, '(A,F0.2,A)')  "  speedup: ", lib_s / tw_s, "x"

    deallocate (xs, res)
    h = treeweave_free(h)

    if (max_rel >= 1.0e-7_c_double) error stop 2
end program lgamma_bench
