! test_treeweave.f90: self-checking conformance test for the Fortran binding.
!
! Like tests/test_c_abi.c, correctness is checked two ways: closed-form (eval
! within a generous margin of the exact kernel) and self-consistency (two paths
! on one handle agree bit-for-bit). Failures are counted and, if any, the
! program ends with `error stop <count>` so the process exit code is nonzero.

module test_kernels
    use, intrinsic :: iso_c_binding
    implicit none

    type, bind(C) :: params_t
        real(c_double) :: amplitude
        real(c_double) :: frequency
    end type params_t

contains

    ! 1-D scalar: exp(0.5 x) + sin(3 x)
    subroutine k_1d(x, y, context) bind(C)
        real(c_double), intent(in)  :: x(*)
        real(c_double), intent(out) :: y(*)
        type(c_ptr),    value       :: context
        y(1) = exp(0.5_c_double * x(1)) + sin(3.0_c_double * x(1))
    end subroutine k_1d

    ! 2-D -> 1: exp(0.3 x0) + sin(2 x1)
    subroutine k_2d1(x, y, context) bind(C)
        real(c_double), intent(in)  :: x(*)
        real(c_double), intent(out) :: y(*)
        type(c_ptr),    value       :: context
        y(1) = exp(0.3_c_double * x(1)) + sin(2.0_c_double * x(2))
    end subroutine k_2d1

    ! 1-D -> 2 vector: [sin x, cos x]
    subroutine k_1d2(x, y, context) bind(C)
        real(c_double), intent(in)  :: x(*)
        real(c_double), intent(out) :: y(*)
        type(c_ptr),    value       :: context
        y(1) = sin(x(1))
        y(2) = cos(x(1))
    end subroutine k_1d2

    ! 1-D scalar, float32: exp(x)
    subroutine k_1d_f32(x, y, context) bind(C)
        real(c_float), intent(in)  :: x(*)
        real(c_float), intent(out) :: y(*)
        type(c_ptr),   value       :: context
        y(1) = exp(x(1))
    end subroutine k_1d_f32

    ! 1-D scalar parameterized through context: amplitude * sin(frequency x)
    subroutine k_ctx(x, y, context) bind(C)
        real(c_double), intent(in)  :: x(*)
        real(c_double), intent(out) :: y(*)
        type(c_ptr),    value       :: context
        type(params_t), pointer     :: p
        call c_f_pointer(context, p)
        y(1) = p%amplitude * sin(p%frequency * x(1))
    end subroutine k_ctx

    ! 2-D -> 2 vector: [sin(x0)*cos(x1), x0+x1]   (for transposed / SoA test)
    subroutine k_2d2(x, y, context) bind(C)
        real(c_double), intent(in)  :: x(*)
        real(c_double), intent(out) :: y(*)
        type(c_ptr),    value       :: context
        y(1) = sin(x(1)) * cos(x(2))
        y(2) = x(1) + x(2)
    end subroutine k_2d2

    ! 2-D scalar, float32: exp(x0 + x1)
    subroutine k_2d1_f32(x, y, context) bind(C)
        real(c_float), intent(in)  :: x(*)
        real(c_float), intent(out) :: y(*)
        type(c_ptr),   value       :: context
        y(1) = exp(x(1) + x(2))
    end subroutine k_2d1_f32

    ! 3-D scalar: x0 + x1 + x2
    subroutine k_3d1(x, y, context) bind(C)
        real(c_double), intent(in)  :: x(*)
        real(c_double), intent(out) :: y(*)
        type(c_ptr),    value       :: context
        y(1) = x(1) + x(2) + x(3)
    end subroutine k_3d1

    ! 3-D scalar, float32: x0 * x1 * x2
    subroutine k_3d1_f32(x, y, context) bind(C)
        real(c_float), intent(in)  :: x(*)
        real(c_float), intent(out) :: y(*)
        type(c_ptr),   value       :: context
        y(1) = x(1) * x(2) * x(3)
    end subroutine k_3d1_f32

end module test_kernels


program test_treeweave
    use, intrinsic :: iso_c_binding
    use, intrinsic :: ieee_arithmetic, only: ieee_is_nan
    use treeweave
    use test_kernels
    implicit none

    integer :: failures
    failures = 0

    call test_1d_scalar(failures)
    call test_2d_to_1(failures)
    call test_vector_output(failures)
    call test_batch_scalar_parity(failures)
    call test_float32(failures)
    call test_context(failures)
    call test_sorted_batch(failures)
    call test_transposed_soa(failures)
    call test_print_stats(failures)
    call test_scalar_eval_byvalue(failures)
    call test_generic_eval(failures)

    if (failures > 0) then
        write (*, '(A,I0,A)') "--- ", failures, " check(s) FAILED ---"
        error stop 1
    end if
    write (*, '(A)') "all Fortran treeweave tests passed"

contains

    subroutine check(cond, name, failures)
        logical,          intent(in)    :: cond
        character(len=*), intent(in)    :: name
        integer,          intent(inout) :: failures
        if (cond) then
            write (*, '(2A)') "PASS  ", name
        else
            write (*, '(2A)') "FAIL  ", name
            failures = failures + 1
        end if
    end subroutine check

    subroutine test_1d_scalar(failures)
        integer, intent(inout) :: failures
        type(c_ptr)    :: h
        real(c_double) :: a(1), b(1), x(1), y(1), xx, exact, max_err
        integer        :: i
        a(1) = 0.0_c_double; b(1) = 1.0_c_double
        h = treeweave_fit(c_funloc(k_1d), 1_c_int, 1_c_int, a, b, 1.0e-8_c_double, &
                       c_null_ptr, c_null_ptr)
        call check(c_associated(h), "1D fit returns a handle", failures)
        if (.not. c_associated(h)) return
        call check(treeweave_input_dim(h) == 1 .and. treeweave_output_dim(h) == 1, &
                   "1D introspection (dim, out_dim)", failures)
        call check(treeweave_memory_usage(h) > 0_c_size_t, "1D memory_usage > 0", failures)
        call check(treeweave_dtype(h) == TREEWEAVE_F64, "1D dtype is F64", failures)
        max_err = 0.0_c_double
        do i = 1, 50
            xx   = 0.02_c_double + 0.96_c_double * real(i - 1, c_double) / 49.0_c_double
            x(1) = xx
            call treeweave_eval_c(h, x, y)
            exact = exp(0.5_c_double * xx) + sin(3.0_c_double * xx)
            if (abs(y(1) - exact) > max_err) max_err = abs(y(1) - exact)
        end do
        call check(max_err < 1.0e-5_c_double, "1D accuracy < 1e-5", failures)
        h = treeweave_free(h)
        call check(.not. c_associated(h), "treeweave_free returns NULL", failures)
    end subroutine test_1d_scalar

    subroutine test_2d_to_1(failures)
        integer, intent(inout) :: failures
        type(c_ptr)    :: h
        real(c_double) :: a(2), b(2), x(2), y(1), exact
        a = [0.0_c_double, 0.0_c_double]
        b = [1.0_c_double, 1.0_c_double]
        h = treeweave_fit(c_funloc(k_2d1), 2_c_int, 1_c_int, a, b, 1.0e-7_c_double, &
                       c_null_ptr, c_null_ptr)
        call check(c_associated(h), "2D fit returns a handle", failures)
        if (.not. c_associated(h)) return
        x = [0.3_c_double, 0.4_c_double]
        call treeweave_eval_c(h, x, y)
        exact = exp(0.3_c_double * x(1)) + sin(2.0_c_double * x(2))
        call check(abs(y(1) - exact) < 1.0e-4_c_double, "2D->1 accuracy < 1e-4", failures)
        h = treeweave_free(h)
    end subroutine test_2d_to_1

    subroutine test_vector_output(failures)
        integer, intent(inout) :: failures
        type(c_ptr)    :: h
        real(c_double) :: a(1), b(1), x(1), y(2)
        a(1) = 0.0_c_double; b(1) = 1.0_c_double
        h = treeweave_fit(c_funloc(k_1d2), 1_c_int, 2_c_int, a, b, 1.0e-7_c_double, &
                       c_null_ptr, c_null_ptr)
        call check(c_associated(h), "vector-output fit returns a handle", failures)
        if (.not. c_associated(h)) return
        call check(treeweave_output_dim(h) == 2, "vector-output out_dim == 2", failures)
        x(1) = 0.5_c_double
        call treeweave_eval_c(h, x, y)
        call check(abs(y(1) - sin(0.5_c_double)) < 1.0e-5_c_double .and. &
                   abs(y(2) - cos(0.5_c_double)) < 1.0e-5_c_double, &
                   "vector-output components accurate", failures)
        h = treeweave_free(h)
    end subroutine test_vector_output

    ! AoS batch agrees bit-for-bit with per-point eval on the same handle.
    subroutine test_batch_scalar_parity(failures)
        integer, intent(inout) :: failures
        type(c_ptr)    :: h
        real(c_double) :: a(1), b(1), x(1), y(1)
        real(c_double) :: xs(64), res(64)
        logical        :: same
        integer        :: i
        a(1) = 0.0_c_double; b(1) = 1.0_c_double
        h = treeweave_fit(c_funloc(k_1d), 1_c_int, 1_c_int, a, b, 1.0e-9_c_double, &
                       c_null_ptr, c_null_ptr)
        if (.not. c_associated(h)) then
            call check(.false., "batch-parity fit returns a handle", failures)
            return
        end if
        do i = 1, 64
            xs(i) = real(i - 1, c_double) / 64.0_c_double
        end do
        call treeweave_batch(h, xs, res, int(64, c_size_t))
        same = .true.
        do i = 1, 64
            x(1) = xs(i)
            call treeweave_eval_c(h, x, y)
            if (res(i) /= y(1)) same = .false.
        end do
        call check(same, "batch == per-point eval (bit-exact)", failures)
        h = treeweave_free(h)
    end subroutine test_batch_scalar_parity

    subroutine test_float32(failures)
        integer, intent(inout) :: failures
        type(c_ptr)   :: h
        real(c_float) :: a(1), b(1), x(1), y(1)
        a(1) = 0.0_c_float; b(1) = 1.0_c_float
        h = treeweavef_fit(c_funloc(k_1d_f32), 1_c_int, 1_c_int, a, b, 1.0e-4_c_double, &
                        c_null_ptr, c_null_ptr)
        call check(c_associated(h), "f32 fit returns a handle", failures)
        if (.not. c_associated(h)) return
        call check(treeweave_dtype(h) == TREEWEAVE_F32, "f32 dtype is F32", failures)
        x(1) = 0.5_c_float
        call treeweavef_eval(h, x, y)
        call check(abs(y(1) - exp(0.5_c_float)) < 1.0e-3_c_float, "f32 accuracy < 1e-3", failures)
        h = treeweave_free(h)
    end subroutine test_float32

    subroutine test_context(failures)
        integer, intent(inout) :: failures
        type(c_ptr)            :: h
        real(c_double)         :: a(1), b(1), x(1), y(1), exact, max_err
        type(params_t), target :: params
        integer                :: i
        a(1) = 0.0_c_double; b(1) = 1.0_c_double
        params%amplitude = 2.5_c_double
        params%frequency = 7.0_c_double
        h = treeweave_fit(c_funloc(k_ctx), 1_c_int, 1_c_int, a, b, 1.0e-9_c_double, &
                       c_loc(params), c_null_ptr)
        call check(c_associated(h), "context fit returns a handle", failures)
        if (.not. c_associated(h)) return
        max_err = 0.0_c_double
        do i = 1, 20
            x(1) = real(i - 1, c_double) / 20.0_c_double
            call treeweave_eval_c(h, x, y)
            exact = params%amplitude * sin(params%frequency * x(1))
            if (abs(y(1) - exact) > max_err) max_err = abs(y(1) - exact)
        end do
        call check(max_err < 1.0e-7_c_double, "context kernel accurate", failures)
        h = treeweave_free(h)
    end subroutine test_context

    ! sorted 1-D batch: must agree bit-for-bit with the AoS batch on ascending input.
    subroutine test_sorted_batch(failures)
        integer, intent(inout) :: failures
        type(c_ptr)    :: h
        real(c_double) :: a(1), b(1)
        real(c_double) :: xs(64), res_batch(64), res_sorted(64)
        logical        :: same
        integer        :: i
        a(1) = 0.0_c_double; b(1) = 1.0_c_double
        h = treeweave_fit(c_funloc(k_1d), 1_c_int, 1_c_int, a, b, 1.0e-9_c_double, &
                       c_null_ptr, c_null_ptr)
        if (.not. c_associated(h)) then
            call check(.false., "sorted-batch fit returns a handle", failures)
            return
        end if
        ! Build strictly ascending input in [0, 1).
        do i = 1, 64
            xs(i) = real(i - 1, c_double) / 65.0_c_double
        end do
        call treeweave_batch(h, xs, res_batch, int(64, c_size_t))
        call treeweave_sorted(h, xs, res_sorted, int(64, c_size_t))
        same = .true.
        do i = 1, 64
            if (res_sorted(i) /= res_batch(i)) same = .false.
        end do
        call check(same, "sorted == batch (bit-exact, ascending)", failures)
        h = treeweave_free(h)
    end subroutine test_sorted_batch

    ! transposed SoA output: each component buffer must agree with the AoS batch.
    subroutine test_transposed_soa(failures)
        integer, intent(inout) :: failures
        type(c_ptr)    :: h
        real(c_double) :: a(2), b(2)
        real(c_double), target :: xs(128)       ! 64 points * 2 coords
        real(c_double), target :: res_aos(128)  ! 64 points * 2 outputs (AoS)
        real(c_double), target :: soa0(64), soa1(64)
        type(c_ptr)    :: soa_ptrs(2)
        logical        :: match
        integer        :: i
        a = [0.0_c_double, 0.0_c_double]
        b = [1.0_c_double, 1.0_c_double]
        h = treeweave_fit(c_funloc(k_2d2), 2_c_int, 2_c_int, a, b, 1.0e-7_c_double, &
                       c_null_ptr, c_null_ptr)
        if (.not. c_associated(h)) then
            call check(.false., "transposed-SoA fit returns a handle", failures)
            return
        end if
        call check(treeweave_output_dim(h) == 2, "transposed-SoA out_dim == 2", failures)
        do i = 1, 64
            xs(2*i - 1) = 0.1_c_double + 0.8_c_double * real(i - 1, c_double) / 63.0_c_double
            xs(2*i)     = 0.9_c_double - 0.8_c_double * real(i - 1, c_double) / 63.0_c_double
        end do
        call treeweave_batch(h, xs, res_aos, int(64, c_size_t))
        soa_ptrs(1) = c_loc(soa0(1))
        soa_ptrs(2) = c_loc(soa1(1))
        call treeweave_transposed(h, xs, soa_ptrs, int(64, c_size_t))
        match = .true.
        do i = 1, 64
            if (soa0(i) /= res_aos(2*i - 1)) match = .false.
            if (soa1(i) /= res_aos(2*i))     match = .false.
        end do
        call check(match, "transposed SoA == AoS batch (bit-exact)", failures)
        h = treeweave_free(h)
    end subroutine test_transposed_soa

    ! treeweave_print_stats: just call it, a crash or hang is a FAIL.
    ! The output goes to stdout; the check is only that the call completes.
    subroutine test_print_stats(failures)
        integer, intent(inout) :: failures
        type(c_ptr)    :: h
        real(c_double) :: a(1), b(1)
        a(1) = 0.0_c_double; b(1) = 1.0_c_double
        h = treeweave_fit(c_funloc(k_1d), 1_c_int, 1_c_int, a, b, 1.0e-8_c_double, &
                       c_null_ptr, c_null_ptr)
        if (.not. c_associated(h)) then
            call check(.false., "print_stats fit returns a handle", failures)
            return
        end if
        call treeweave_print_stats(h)
        call check(.true., "treeweave_print_stats (smoke-test, no crash)", failures)
        ! NULL handle: must be a no-op.
        call treeweave_print_stats(c_null_ptr)
        call check(.true., "treeweave_print_stats(NULL) is a no-op", failures)
        h = treeweave_free(h)
    end subroutine test_print_stats

    ! The generic treeweave_eval picks point vs batch from the rank of x, and
    ! must agree bit-for-bit with the explicit C entry points it wraps.
    subroutine test_generic_eval(failures)
        integer, intent(inout) :: failures
        type(c_ptr)    :: h, h2, hv, hf, hf2
        real(c_double) :: a(1), b(1), a2(2), b2(2)
        real(c_double) :: xs(64), ref(64), point(2), ref_point(1), ref_vec(2)
        real(c_double) :: xs2(2, 64), ref2(64), ref2v(2, 64)
        real(c_float)  :: a1f(1), b1f(1), xsf(8), reff(8)
        real(c_float)  :: af2(2), bf2(2), xf2(2, 8), ref2f(8)
        real(c_double), allocatable :: y(:), ybad(:)
        real(c_double), allocatable :: y2(:, :)
        real(c_float),  allocatable :: yf(:), yf2(:, :)
        real(c_float)               :: yf_s
        real(c_double)              :: yd_s
        character(len=64)           :: msg
        integer        :: i

        ! --- dim == 1, out_dim == 1: scalar and vector routes ---------------
        a(1) = 0.0_c_double; b(1) = 1.0_c_double
        h = treeweave_fit(c_funloc(k_1d), 1_c_int, 1_c_int, a, b, 1.0e-8_c_double, &
                          c_null_ptr, c_null_ptr)
        if (.not. c_associated(h)) then
            call check(.false., "generic-eval fit returns a handle", failures)
            return
        end if
        do i = 1, 64
            xs(i) = real(i - 1, c_double) / 64.0_c_double
        end do
        call treeweave_batch(h, xs, ref, int(64, c_size_t))
        y = treeweave_eval(h, xs)
        call check(size(y) == 64, "generic eval x(:) returns size(x) values", failures)
        call check(all(y == ref), "generic eval x(:) == treeweave_batch (bit-exact)", failures)
        call check(treeweave_eval(h, 0.5_c_double) == treeweave_eval_1d(h, 0.5_c_double), &
                   "generic eval scalar == treeweave_eval_1d (bit-exact)", failures)

        ! A rank-1 argument of the wrong length is neither a point nor a batch.
        ybad = treeweave_eval(h, xs(1:0))
        call check(size(ybad) == 1 .and. ieee_is_nan(ybad(1)), &
                   "generic eval rejects an empty batch with NaN", failures)
        yf_s = treeweave_eval(h, 0.5_c_float)
        call check(ieee_is_nan(yf_s), "generic eval f32 scalar NaNs on an f64 handle", failures)
        h = treeweave_free(h)

        ! --- dim == 2: rank-1 is one point, rank-2 is a batch ---------------
        a2 = [0.1_c_double, 0.1_c_double]; b2 = [1.0_c_double, 1.0_c_double]
        h2 = treeweave_fit(c_funloc(k_2d1), 2_c_int, 1_c_int, a2, b2, 1.0e-7_c_double, &
                           c_null_ptr, c_null_ptr)
        if (.not. c_associated(h2)) then
            call check(.false., "generic-eval 2D fit returns a handle", failures)
            return
        end if
        point = [0.4_c_double, 0.6_c_double]
        call treeweave_eval_c(h2, point, ref_point)
        y = treeweave_eval(h2, point)
        call check(size(y) == 1 .and. y(1) == ref_point(1), &
                   "generic eval x(dim) is one point (bit-exact)", failures)
        ! Keep every point strictly inside the box: a NaN from an out-of-domain
        ! point would fail the bit-exact comparison below, since NaN /= NaN.
        do i = 1, 64
            xs2(1, i) = 0.2_c_double + 0.6_c_double * real(i - 1, c_double) / 63.0_c_double
            xs2(2, i) = 0.8_c_double - 0.6_c_double * real(i - 1, c_double) / 63.0_c_double
        end do
        call treeweave_batch(h2, xs2, ref2, int(64, c_size_t))
        y2 = treeweave_eval(h2, xs2)
        call check(size(y2, 1) == 1 .and. size(y2, 2) == 64, &
                   "generic eval x(dim, n) returns y(out_dim, n)", failures)
        call check(all(y2(1, :) == ref2), &
                   "generic eval x(dim, n) == treeweave_batch (bit-exact)", failures)
        y2 = treeweave_eval(h2, xs2(1:1, :))
        call check(ieee_is_nan(y2(1, 1)), &
                   "generic eval rejects a wrong column count with NaN", failures)
        h2 = treeweave_free(h2)

        ! --- out_dim == 2: the AoS result carries both components ----------
        hv = treeweave_fit(c_funloc(k_2d2), 2_c_int, 2_c_int, a2, b2, 1.0e-7_c_double, &
                           c_null_ptr, c_null_ptr)
        if (.not. c_associated(hv)) then
            call check(.false., "generic-eval vector-output fit returns a handle", failures)
            return
        end if
        call treeweave_eval_c(hv, point, ref_vec)
        y = treeweave_eval(hv, point)
        call check(size(y) == 2 .and. all(y == ref_vec), &
                   "generic eval point carries out_dim components", failures)
        y2 = treeweave_eval(hv, xs2)
        call check(size(y2, 1) == 2 .and. size(y2, 2) == 64, &
                   "generic eval batch returns y(2, n) for out_dim == 2", failures)
        call treeweave_batch(hv, xs2, ref2v, int(64, c_size_t))
        call check(all(y2 == ref2v), &
                   "generic eval x(dim, n) both components == treeweave_batch (bit-exact)", &
                   failures)
        hv = treeweave_free(hv)

        ! --- float32 handle: the real(c_float) specifics ------------------
        a1f(1) = 0.0_c_float; b1f(1) = 1.0_c_float
        hf = treeweavef_fit(c_funloc(k_1d_f32), 1_c_int, 1_c_int, a1f, b1f, 1.0e-5_c_double, &
                            c_null_ptr, c_null_ptr)
        if (.not. c_associated(hf)) then
            call check(.false., "generic-eval f32 fit returns a handle", failures)
            return
        end if
        do i = 1, 8
            xsf(i) = real(i - 1, c_float) / 8.0_c_float
        end do
        call treeweavef_batch(hf, xsf, reff, int(8, c_size_t))
        yf = treeweave_eval(hf, xsf)
        call check(size(yf) == 8 .and. all(yf == reff), &
                   "generic eval dispatches on kind (f32, bit-exact)", failures)
        ! The f32 scalar specific.
        yf_s = treeweave_eval(hf, 0.5_c_float)
        call check(yf_s == treeweavef_eval_1d(hf, 0.5_c_float), &
                   "generic eval f32 scalar == treeweavef_eval_1d (bit-exact)", failures)
        ! A double call on an f32 handle must not read the result buffer.
        y = treeweave_eval(hf, xs)
        call check(all(ieee_is_nan(y)), "generic eval NaNs on a dtype mismatch", failures)
        yd_s = treeweave_eval(hf, 0.5_c_double)
        call check(ieee_is_nan(yd_s), "generic eval f64 scalar NaNs on an f32 handle", failures)
        call check(treeweave_error_message() /= "", &
                   "a C rejection leaves a message for treeweave_error_message()", failures)
        hf = treeweave_free(hf)

        ! --- f32 matrix route ---------------------------------------------
        af2 = [0.0_c_float, 0.0_c_float]; bf2 = [1.0_c_float, 1.0_c_float]
        hf2 = treeweavef_fit(c_funloc(k_2d1_f32), 2_c_int, 1_c_int, af2, bf2, &
                             1.0e-5_c_double, c_null_ptr, c_null_ptr)
        if (.not. c_associated(hf2)) then
            call check(.false., "generic-eval f32 2D fit returns a handle", failures)
            return
        end if
        do i = 1, 8
            xf2(1, i) = 0.2_c_float + 0.6_c_float * real(i - 1, c_float) / 7.0_c_float
            xf2(2, i) = 0.8_c_float - 0.6_c_float * real(i - 1, c_float) / 7.0_c_float
        end do
        call treeweavef_batch(hf2, xf2, ref2f, int(8, c_size_t))
        yf2 = treeweave_eval(hf2, xf2)
        call check(size(yf2, 1) == 1 .and. size(yf2, 2) == 8, &
                   "generic eval f32 x(dim, n) returns y(out_dim, n)", failures)
        call check(all(yf2(1, :) == ref2f), &
                   "generic eval f32 matrix == treeweavef_batch (bit-exact)", failures)
        ! A wrapper shape error returns NaN and must not rewrite the C message.
        yf_s = treeweavef_eval_1d(c_null_ptr, 0.0_c_float)  ! stamps the C buffer
        msg = treeweave_error_message()
        yf2 = treeweave_eval(hf2, xf2(1:1, :))
        call check(ieee_is_nan(yf2(1, 1)), &
                   "generic eval f32 rejects a wrong column count with NaN", failures)
        call check(treeweave_error_message() == msg .and. msg /= "", &
                   "a wrapper shape error leaves the C message unchanged", failures)
        hf2 = treeweave_free(hf2)
    end subroutine test_generic_eval

    subroutine test_scalar_eval_byvalue(failures)
        integer, intent(inout) :: failures
        type(c_ptr)    :: h64, h64_2d, h64_3d
        type(c_ptr)    :: hf32, hf32_2d, hf32_3d
        real(c_double) :: a1(1), b1(1), a2(2), b2(2), a3(3), b3(3)
        real(c_float)  :: af1(1), bf1(1), af2(2), bf2(2), af3(3), bf3(3)
        real(c_double) :: y64, exact64
        real(c_float)  :: y32, exact32

        a1(1) = 0.0_c_double; b1(1) = 1.0_c_double
        h64 = treeweave_fit(c_funloc(k_1d), 1_c_int, 1_c_int, a1, b1, 1.0e-9_c_double, &
                         c_null_ptr, c_null_ptr)
        call check(c_associated(h64), "eval_1d: f64 fit handle", failures)
        if (c_associated(h64)) then
            y64    = treeweave_eval_1d(h64, 0.3_c_double)
            exact64 = exp(0.5_c_double * 0.3_c_double) + sin(3.0_c_double * 0.3_c_double)
            call check(abs(y64 - exact64) < 1.0e-5_c_double, "treeweave_eval_1d accuracy", failures)
            h64 = treeweave_free(h64)
        end if

        a2 = [0.0_c_double, 0.0_c_double]
        b2 = [1.0_c_double, 1.0_c_double]
        h64_2d = treeweave_fit(c_funloc(k_2d1), 2_c_int, 1_c_int, a2, b2, 1.0e-7_c_double, &
                            c_null_ptr, c_null_ptr)
        call check(c_associated(h64_2d), "eval_2d: f64 fit handle", failures)
        if (c_associated(h64_2d)) then
            y64    = treeweave_eval_2d(h64_2d, 0.3_c_double, 0.4_c_double)
            exact64 = exp(0.3_c_double * 0.3_c_double) + sin(2.0_c_double * 0.4_c_double)
            call check(abs(y64 - exact64) < 1.0e-4_c_double, "treeweave_eval_2d accuracy", failures)
            h64_2d = treeweave_free(h64_2d)
        end if

        a3 = [0.0_c_double, 0.0_c_double, 0.0_c_double]
        b3 = [1.0_c_double, 1.0_c_double, 1.0_c_double]
        h64_3d = treeweave_fit(c_funloc(k_3d1), 3_c_int, 1_c_int, a3, b3, 1.0e-7_c_double, &
                            c_null_ptr, c_null_ptr)
        call check(c_associated(h64_3d), "eval_3d: f64 fit handle", failures)
        if (c_associated(h64_3d)) then
            y64    = treeweave_eval_3d(h64_3d, 0.2_c_double, 0.3_c_double, 0.4_c_double)
            exact64 = 0.2_c_double + 0.3_c_double + 0.4_c_double
            call check(abs(y64 - exact64) < 1.0e-5_c_double, "treeweave_eval_3d accuracy", failures)
            h64_3d = treeweave_free(h64_3d)
        end if

        af1(1) = 0.0_c_float; bf1(1) = 1.0_c_float
        hf32 = treeweavef_fit(c_funloc(k_1d_f32), 1_c_int, 1_c_int, af1, bf1, 1.0e-4_c_double, &
                           c_null_ptr, c_null_ptr)
        call check(c_associated(hf32), "eval_1d f32: fit handle", failures)
        if (c_associated(hf32)) then
            y32    = treeweavef_eval_1d(hf32, 0.5_c_float)
            exact32 = exp(0.5_c_float)
            call check(abs(y32 - exact32) < 1.0e-3_c_float, "treeweavef_eval_1d accuracy", failures)
            hf32 = treeweave_free(hf32)
        end if

        af2 = [0.0_c_float, 0.0_c_float]
        bf2 = [1.0_c_float, 1.0_c_float]
        hf32_2d = treeweavef_fit(c_funloc(k_2d1_f32), 2_c_int, 1_c_int, af2, bf2, 1.0e-4_c_double, &
                              c_null_ptr, c_null_ptr)
        call check(c_associated(hf32_2d), "eval_2d f32: fit handle", failures)
        if (c_associated(hf32_2d)) then
            y32    = treeweavef_eval_2d(hf32_2d, 0.2_c_float, 0.3_c_float)
            exact32 = exp(0.2_c_float + 0.3_c_float)
            call check(abs(y32 - exact32) < 1.0e-3_c_float, "treeweavef_eval_2d accuracy", failures)
            hf32_2d = treeweave_free(hf32_2d)
        end if

        af3 = [0.0_c_float, 0.0_c_float, 0.0_c_float]
        bf3 = [1.0_c_float, 1.0_c_float, 1.0_c_float]
        hf32_3d = treeweavef_fit(c_funloc(k_3d1_f32), 3_c_int, 1_c_int, af3, bf3, 1.0e-3_c_double, &
                              c_null_ptr, c_null_ptr)
        call check(c_associated(hf32_3d), "eval_3d f32: fit handle", failures)
        if (c_associated(hf32_3d)) then
            y32    = treeweavef_eval_3d(hf32_3d, 0.2_c_float, 0.3_c_float, 0.4_c_float)
            exact32 = 0.2_c_float * 0.3_c_float * 0.4_c_float
            call check(abs(y32 - exact32) < 1.0e-3_c_float, "treeweavef_eval_3d accuracy", failures)
            hf32_3d = treeweave_free(hf32_3d)
        end if

    end subroutine test_scalar_eval_byvalue

end program test_treeweave
