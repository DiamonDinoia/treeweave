! zeta_bench.f90: treeweave vs a fair brute-force Riemann-zeta eval, via the C ABI.
! See benchmarks/zeta_bench.cpp for the rationale. ζ(s) = Σ_k k^-s summed until
! the tail is negligible (rel 1e-10, ≤160 terms) yet smooth on [2,10]: fit once.
! Times single/multi/sorted; the native rate is sampled over n_native and reused.
! TREEWEAVE_BENCH_YAML=path emits YAML.

module zeta_bench_kernels
    use, intrinsic :: iso_c_binding
    implicit none
    ! Fair baseline: sum k^-s until a term is below zeta_eps relative to the
    ! running total, capped at zeta_max_terms, a competent zeta stops early.
    real(c_double), parameter :: zeta_eps = 1.0e-10_c_double
    integer,        parameter :: zeta_max_terms = 160
contains
    ! ζ(s) ≈ Σ_k k^-s (early stop). Used both as the treeweave fit callback and
    ! as the native baseline (apples-to-apples).
    function zeta_partial(s) result(acc)
        real(c_double), intent(in) :: s
        real(c_double)             :: acc, term
        integer                    :: k
        acc = 0.0_c_double
        do k = 1, zeta_max_terms
            term = real(k, c_double)**(-s)
            acc = acc + term
            if (term < zeta_eps * acc) exit
        end do
    end function zeta_partial

    subroutine kernel_zeta(x, y, context) bind(C)
        real(c_double), intent(in)  :: x(*)
        real(c_double), intent(out) :: y(*)
        type(c_ptr),    value       :: context
        y(1) = zeta_partial(x(1))
    end subroutine kernel_zeta

    ! In-place ascending quicksort (Hoare partition), builds the sorted input
    ! for the sorted-eval mode. The sample is random, so recursion depth stays
    ! O(log n); the sort itself is untimed.
    recursive subroutine quicksort(arr, lo, hi)
        real(c_double), intent(inout) :: arr(:)
        integer,        intent(in)    :: lo, hi
        integer        :: i, j
        real(c_double) :: pivot, tmp
        if (lo >= hi) return
        pivot = arr((lo + hi) / 2)
        i = lo
        j = hi
        do
            do while (arr(i) < pivot)
                i = i + 1
            end do
            do while (arr(j) > pivot)
                j = j - 1
            end do
            if (i <= j) then
                tmp = arr(i); arr(i) = arr(j); arr(j) = tmp
                i = i + 1
                j = j - 1
            end if
            if (i > j) exit
        end do
        if (lo < j) call quicksort(arr, lo, j)
        if (i < hi) call quicksort(arr, i, hi)
    end subroutine quicksort

    ! Write one eval-mode block of the YAML document to unit u. The ES edit
    ! descriptor always emits "d.dddE±dd" (leading dot + exponent), so a YAML 1.1
    ! parser reads each value as a float rather than a string.
    subroutine emit_block(u, name, tw, nat)
        integer,          intent(in) :: u
        character(len=*), intent(in) :: name
        real(c_double),   intent(in) :: tw, nat
        write (u, '(A,A)')       name, ':'
        write (u, '(A,ES24.16)') '  treeweave_mevals_s: ', tw
        write (u, '(A,ES24.16)') '  native_mevals_s: ', nat
        write (u, '(A,ES24.16)') '  speedup: ', tw / nat
    end subroutine emit_block
end module zeta_bench_kernels


program zeta_bench
    use, intrinsic :: iso_c_binding
    use treeweave
    use zeta_bench_kernels
    implicit none

    integer, parameter          :: n = 1000000        ! batch / sorted points
    integer, parameter          :: n_scalar = 100000  ! scalar-API points
    integer, parameter          :: n_native = 256     ! brute-force sample (<=160 powers each)
    type(c_ptr)                 :: fn
    real(c_double)              :: a(1), b(1), tol
    real(c_double), allocatable :: xs(:), res(:), xs_sorted(:)
    real(c_double)              :: approx, exact, rel, max_rel
    real(c_double)              :: nat_s, nat_rate
    real(c_double)              :: tw_single_s, tw_multi_s, tw_sorted_s
    real(c_double)              :: tw_single, tw_multi, tw_sorted
    real(c_double), volatile    :: sink          ! defeats dead-code elimination
    integer(c_int64_t)          :: c0, c1, rate
    integer                     :: i, u, ios, yaml_len
    character(len=4096)         :: yaml_path

    a(1) = 2.0_c_double
    b(1) = 10.0_c_double
    tol  = 1.0e-10_c_double

    ! c_null_ptr opts => defaults, whose tol_kind is RELATIVE_MAX (the right
    ! measure for this zero-free, monotone function).
    fn = treeweave_fit(c_funloc(kernel_zeta), 1_c_int, 1_c_int, a, b, tol, &
                      c_null_ptr, c_null_ptr)
    if (.not. c_associated(fn)) then
        write (*, '(2A)') "treeweave_fit failed: ", treeweave_error_message()
        error stop 1
    end if

    allocate (xs(n), res(n), xs_sorted(n))
    call random_seed()
    call random_number(xs)
    xs = a(1) + (b(1) - a(1)) * xs

    call system_clock(count_rate=rate)

    max_rel = 0.0_c_double
    do i = 1, n_native
        exact  = zeta_partial(xs(i))
        approx = treeweave_eval_1d(fn, xs(i))
        rel    = abs(approx - exact) / abs(exact)
        if (rel > max_rel) max_rel = rel
    end do

    sink = 0.0_c_double

    ! --- native rate: brute-force sum over the small sample (mode-independent).
    ! Measured once and reused as the baseline in all three eval modes.
    do i = 1, n_native
        sink = sink + zeta_partial(xs(i))            ! warm-up (untimed)
    end do
    call system_clock(c0)
    do i = 1, n_native
        sink = sink + zeta_partial(xs(i))
    end do
    call system_clock(c1)
    nat_s    = real(c1 - c0, c_double) / real(rate, c_double)
    nat_rate = real(n_native, c_double) / (nat_s * 1.0e6_c_double)   ! Mevals/s, all modes

    do i = 1, n_scalar
        sink = sink + treeweave_eval_1d(fn, xs(i))    ! warm-up (untimed)
    end do
    call system_clock(c0)
    do i = 1, n_scalar
        sink = sink + treeweave_eval_1d(fn, xs(i))
    end do
    call system_clock(c1)
    tw_single_s = real(c1 - c0, c_double) / real(rate, c_double)

    call treeweave_batch(fn, xs, res, int(n, c_size_t))   ! warm-up (untimed)
    sink = res(1)
    call system_clock(c0)
    call treeweave_batch(fn, xs, res, int(n, c_size_t))
    call system_clock(c1)
    tw_multi_s = real(c1 - c0, c_double) / real(rate, c_double)
    sink = res(1)

    ! --- sorted-eval: the 1-D ascending fast path --------------------------
    ! Sort once, untimed.
    xs_sorted = xs
    call quicksort(xs_sorted, 1, n)
    call treeweave_sorted(fn, xs_sorted, res, int(n, c_size_t))   ! warm-up (untimed)
    sink = res(1)
    call system_clock(c0)
    call treeweave_sorted(fn, xs_sorted, res, int(n, c_size_t))
    call system_clock(c1)
    tw_sorted_s = real(c1 - c0, c_double) / real(rate, c_double)
    sink = res(1)

    tw_single = real(n_scalar, c_double) / (tw_single_s * 1.0e6_c_double)
    tw_multi  = real(n, c_double) / (tw_multi_s * 1.0e6_c_double)
    tw_sorted = real(n, c_double) / (tw_sorted_s * 1.0e6_c_double)

    write (*, '(A,I0,A,F0.1,A,F0.1,A,ES8.1)') "zeta(s) = sum k^-s (<=", zeta_max_terms, &
        " terms, stop at 1e-10), fit on [", a(1), ", ", b(1), "], relative tol ", tol
    write (*, '(A,ES10.3)') "  max rel err: ", max_rel
    write (*, '(A,F0.1,A,F0.4,A,F0.1,A)') "  single-eval  treeweave ", tw_single, "  native ", nat_rate, &
        " Mevals/s  speedup ", tw_single / nat_rate, "x"
    write (*, '(A,F0.1,A,F0.4,A,F0.1,A)') "  multi-eval   treeweave ", tw_multi, "  native ", nat_rate, &
        " Mevals/s  speedup ", tw_multi / nat_rate, "x"
    write (*, '(A,F0.1,A,F0.4,A,F0.1,A)') "  sorted-eval  treeweave ", tw_sorted, "  native ", nat_rate, &
        " Mevals/s  speedup ", tw_sorted / nat_rate, "x"

    call get_environment_variable("TREEWEAVE_BENCH_YAML", yaml_path, yaml_len)
    if (yaml_len > 0) then
        open (newunit=u, file=trim(yaml_path), status="replace", action="write", iostat=ios)
        if (ios == 0) then
            write (u, '(A)')                     'language: "fortran"'
            write (u, '(A,ES24.16,A,ES24.16,A)') 'domain: [', a(1), ', ', b(1), ']'
            write (u, '(A,ES24.16)')             'tol: ', tol
            write (u, '(A,I0)')                  'n_pts: ', n
            write (u, '(A,ES24.16)')             'max_rel_err: ', max_rel
            call emit_block(u, 'single_eval', tw_single, nat_rate)
            call emit_block(u, 'multi_eval', tw_multi, nat_rate)
            call emit_block(u, 'sorted_eval', tw_sorted, nat_rate)
            close (u)
        end if
    end if

    deallocate (xs, res, xs_sorted)
    fn = treeweave_free(fn)

    if (max_rel >= 1.0e-7_c_double) error stop 2
end program zeta_bench
