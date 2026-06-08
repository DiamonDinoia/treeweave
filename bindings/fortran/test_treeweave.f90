! test_treeweave.f90 — self-checking conformance test for the Fortran binding.
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

end module test_kernels


program test_treeweave
    use, intrinsic :: iso_c_binding
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

    ! 1-D scalar fit; closed-form accuracy at interior points.
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
            call treeweave_eval(h, x, y)
            exact = exp(0.5_c_double * xx) + sin(3.0_c_double * xx)
            if (abs(y(1) - exact) > max_err) max_err = abs(y(1) - exact)
        end do
        call check(max_err < 1.0e-5_c_double, "1D accuracy < 1e-5", failures)
        h = treeweave_free(h)
        call check(.not. c_associated(h), "treeweave_free returns NULL", failures)
    end subroutine test_1d_scalar

    ! 2-D -> 1 fit; accuracy at an interior point.
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
        call treeweave_eval(h, x, y)
        exact = exp(0.3_c_double * x(1)) + sin(2.0_c_double * x(2))
        call check(abs(y(1) - exact) < 1.0e-4_c_double, "2D->1 accuracy < 1e-4", failures)
        h = treeweave_free(h)
    end subroutine test_2d_to_1

    ! 1-D -> 2 vector-valued fit.
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
        call treeweave_eval(h, x, y)
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
            call treeweave_eval(h, x, y)
            if (res(i) /= y(1)) same = .false.
        end do
        call check(same, "batch == per-point eval (bit-exact)", failures)
        h = treeweave_free(h)
    end subroutine test_batch_scalar_parity

    ! float32 path: treeweavef_fit / treeweavef_eval.
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

    ! context pointer forwarded to the callback.
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
            call treeweave_eval(h, x, y)
            exact = params%amplitude * sin(params%frequency * x(1))
            if (abs(y(1) - exact) > max_err) max_err = abs(y(1) - exact)
        end do
        call check(max_err < 1.0e-7_c_double, "context kernel accurate", failures)
        h = treeweave_free(h)
    end subroutine test_context

end program test_treeweave
