! treeweave.f90: Fortran 2018 binding for the treeweave C ABI (see ../../include/treeweave.h).
!
! This is a faithful, thin binding over `iso_c_binding`: every C entry point is
! exposed as a Fortran procedure of the same name via an `interface` block, so
! the mapping is one-to-one and explicit. Unlike the Python / Julia / MATLAB
! wrappers there is no call operator, no keyword arguments, and no inference,
! the caller passes `input_dim` / `output_dim` explicitly, exactly as a C
! consumer would.
!
! Precision lives in the symbol prefix, FINUFFT/FFTW style: the `treeweave_*`
! procedures operate on `real(c_double)`, the `treeweavef_*` twins on
! `real(c_float)`. The introspection / lifetime procedures are dtype-independent
! and operate on the opaque handle, so they have no `treeweavef_` twin.
!
! The user callback has the C prototype
!     void f(const double *x, double *y, void *context)   ! (float for treeweavef_*)
! and must be a `bind(C)` procedure; pass `c_funloc(f)` as the `f` argument.
! `context` is forwarded untouched to every invocation, the C stand-in for a
! closure's captures (recover it with `c_f_pointer`); pass `c_null_ptr` when
! unused. Pass `c_null_ptr` for `opts` to use the default options, or
! initialize a `target` `treeweave_opts` with `treeweave_default_opts()`
! and pass `c_loc(my_opts)`.
module treeweave
    use, intrinsic :: iso_c_binding
    implicit none
    public

    ! ---- tolerance interpretation (treeweave_tol_kind_t) --------------------
    integer(c_int), parameter :: TREEWEAVE_RELATIVE_TAIL = 0_c_int
    integer(c_int), parameter :: TREEWEAVE_ABSOLUTE_TAIL = 1_c_int
    integer(c_int), parameter :: TREEWEAVE_RELATIVE_MAX  = 2_c_int
    integer(c_int), parameter :: TREEWEAVE_ABSOLUTE_MAX  = 3_c_int
    integer(c_int), parameter :: TREEWEAVE_RELATIVE_L2   = 4_c_int
    integer(c_int), parameter :: TREEWEAVE_ABSOLUTE_L2   = 5_c_int

    ! ---- value type carried by a handle (treeweave_dtype_t) -----------------
    integer(c_int), parameter :: TREEWEAVE_F64 = 0_c_int
    integer(c_int), parameter :: TREEWEAVE_F32 = 1_c_int

    ! ---- fit knobs: interoperable mirror of `treeweave_opts` ---------------
    ! Pass c_loc() of a `target` instance as the `opts` argument, or c_null_ptr
    ! to fall back to default options.
    type, bind(C) :: treeweave_opts
        integer(c_int) :: tol_kind
        integer(c_int) :: max_depth
        integer(c_int) :: max_memory_mib
        integer(c_int) :: allow_max_depth_leaves
        integer(c_int) :: min_uniform_depth
    end type treeweave_opts

    interface
        ! ---- fit ---------------------------------------------------------
        function treeweave_fit(f, input_dim, output_dim, a, b, tol, context, opts) &
                bind(C, name="treeweave_fit") result(handle)
            import :: c_funptr, c_int, c_double, c_ptr
            type(c_funptr), value      :: f
            integer(c_int), value      :: input_dim, output_dim
            real(c_double), intent(in) :: a(*), b(*)
            real(c_double), value      :: tol
            type(c_ptr),    value      :: context
            type(c_ptr),    value      :: opts
            type(c_ptr)                :: handle
        end function treeweave_fit

        function treeweavef_fit(f, input_dim, output_dim, a, b, tol, context, opts) &
                bind(C, name="treeweavef_fit") result(handle)
            import :: c_funptr, c_int, c_float, c_double, c_ptr
            type(c_funptr), value      :: f
            integer(c_int), value      :: input_dim, output_dim
            real(c_float),  intent(in) :: a(*), b(*)
            real(c_double), value      :: tol
            type(c_ptr),    value      :: context
            type(c_ptr),    value      :: opts
            type(c_ptr)                :: handle
        end function treeweavef_fit

        ! ---- eval: single point -----------------------------------------
        subroutine treeweave_eval(f, x, y) bind(C, name="treeweave_eval")
            import :: c_ptr, c_double
            type(c_ptr),    value       :: f
            real(c_double), intent(in)  :: x(*)
            real(c_double), intent(out) :: y(*)
        end subroutine treeweave_eval

        subroutine treeweavef_eval(f, x, y) bind(C, name="treeweavef_eval")
            import :: c_ptr, c_float
            type(c_ptr),   value       :: f
            real(c_float), intent(in)  :: x(*)
            real(c_float), intent(out) :: y(*)
        end subroutine treeweavef_eval

        ! ---- eval: AoS batch (n points) ----------------------------------
        subroutine treeweave_batch(f, x, res, n) bind(C, name="treeweave_batch")
            import :: c_ptr, c_double, c_size_t
            type(c_ptr),       value       :: f
            real(c_double),    intent(in)  :: x(*)
            real(c_double),    intent(out) :: res(*)
            integer(c_size_t), value       :: n
        end subroutine treeweave_batch

        subroutine treeweavef_batch(f, x, res, n) bind(C, name="treeweavef_batch")
            import :: c_ptr, c_float, c_size_t
            type(c_ptr),       value       :: f
            real(c_float),     intent(in)  :: x(*)
            real(c_float),     intent(out) :: res(*)
            integer(c_size_t), value       :: n
        end subroutine treeweavef_batch

        ! ---- eval: sorted 1-D batch (input_dim == 1) ---------------------
        subroutine treeweave_sorted(f, x, res, n) bind(C, name="treeweave_sorted")
            import :: c_ptr, c_double, c_size_t
            type(c_ptr),       value       :: f
            real(c_double),    intent(in)  :: x(*)
            real(c_double),    intent(out) :: res(*)
            integer(c_size_t), value       :: n
        end subroutine treeweave_sorted

        subroutine treeweavef_sorted(f, x, res, n) bind(C, name="treeweavef_sorted")
            import :: c_ptr, c_float, c_size_t
            type(c_ptr),       value       :: f
            real(c_float),     intent(in)  :: x(*)
            real(c_float),     intent(out) :: res(*)
            integer(c_size_t), value       :: n
        end subroutine treeweavef_sorted

        ! ---- eval: SoA / transposed (output_dim > 1) ---------------------
        ! `soa` is an array of `output_dim` C pointers; soa(d) must point at an
        ! n-element buffer that receives component d.
        subroutine treeweave_transposed(f, x, soa, n) bind(C, name="treeweave_transposed")
            import :: c_ptr, c_double, c_size_t
            type(c_ptr),       value      :: f
            real(c_double),    intent(in) :: x(*)
            type(c_ptr),       intent(in) :: soa(*)
            integer(c_size_t), value      :: n
        end subroutine treeweave_transposed

        subroutine treeweavef_transposed(f, x, soa, n) bind(C, name="treeweavef_transposed")
            import :: c_ptr, c_float, c_size_t
            type(c_ptr),       value      :: f
            real(c_float),     intent(in) :: x(*)
            type(c_ptr),       intent(in) :: soa(*)
            integer(c_size_t), value      :: n
        end subroutine treeweavef_transposed

        ! ---- by-value scalar eval (C convenience, output_dim == 1) ----------
        ! Coordinates are passed by value and the result is a function return.
        ! The `_1d`/`_2d`/`_3d` suffix gives the arity (== the handle's input_dim).
        ! A NULL handle, dtype mismatch, or dim/output_dim mismatch returns NaN
        ! and sets treeweave_last_error().
        function treeweave_eval_1d(f, x0) bind(C, name="treeweave_eval_1d") result(y)
            import :: c_ptr, c_double
            type(c_ptr),    value :: f
            real(c_double), value :: x0
            real(c_double)        :: y
        end function treeweave_eval_1d

        function treeweave_eval_2d(f, x0, x1) bind(C, name="treeweave_eval_2d") result(y)
            import :: c_ptr, c_double
            type(c_ptr),    value :: f
            real(c_double), value :: x0, x1
            real(c_double)        :: y
        end function treeweave_eval_2d

        function treeweave_eval_3d(f, x0, x1, x2) bind(C, name="treeweave_eval_3d") result(y)
            import :: c_ptr, c_double
            type(c_ptr),    value :: f
            real(c_double), value :: x0, x1, x2
            real(c_double)        :: y
        end function treeweave_eval_3d

        function treeweavef_eval_1d(f, x0) bind(C, name="treeweavef_eval_1d") result(y)
            import :: c_ptr, c_float
            type(c_ptr),   value :: f
            real(c_float), value :: x0
            real(c_float)        :: y
        end function treeweavef_eval_1d

        function treeweavef_eval_2d(f, x0, x1) bind(C, name="treeweavef_eval_2d") result(y)
            import :: c_ptr, c_float
            type(c_ptr),   value :: f
            real(c_float), value :: x0, x1
            real(c_float)        :: y
        end function treeweavef_eval_2d

        function treeweavef_eval_3d(f, x0, x1, x2) bind(C, name="treeweavef_eval_3d") result(y)
            import :: c_ptr, c_float
            type(c_ptr),   value :: f
            real(c_float), value :: x0, x1, x2
            real(c_float)        :: y
        end function treeweavef_eval_3d

        ! ---- introspection / lifetime (dtype-independent) ----------------
        function treeweave_dtype(f) bind(C, name="treeweave_dtype") result(dt)
            import :: c_ptr, c_int
            type(c_ptr), value :: f
            integer(c_int)     :: dt
        end function treeweave_dtype

        function treeweave_input_dim(f) bind(C, name="treeweave_input_dim") result(d)
            import :: c_ptr, c_int
            type(c_ptr), value :: f
            integer(c_int)     :: d
        end function treeweave_input_dim

        function treeweave_output_dim(f) bind(C, name="treeweave_output_dim") result(d)
            import :: c_ptr, c_int
            type(c_ptr), value :: f
            integer(c_int)     :: d
        end function treeweave_output_dim

        function treeweave_memory_usage(f) bind(C, name="treeweave_memory_usage") result(bytes)
            import :: c_ptr, c_size_t
            type(c_ptr), value :: f
            integer(c_size_t)  :: bytes
        end function treeweave_memory_usage

        subroutine treeweave_print_stats(f) bind(C, name="treeweave_print_stats")
            import :: c_ptr
            type(c_ptr), value :: f
        end subroutine treeweave_print_stats

        function treeweave_free(f) bind(C, name="treeweave_free") result(null_handle)
            import :: c_ptr
            type(c_ptr), value :: f
            type(c_ptr)        :: null_handle
        end function treeweave_free

        function treeweave_last_error() bind(C, name="treeweave_last_error") result(msg)
            import :: c_ptr
            type(c_ptr) :: msg
        end function treeweave_last_error

        ! libc strlen, used to size the treeweave_last_error() C string.
        function c_strlen(s) bind(C, name="strlen") result(n)
            import :: c_ptr, c_size_t
            type(c_ptr), value :: s
            integer(c_size_t)  :: n
        end function c_strlen
    end interface

contains

    function treeweave_default_opts() result(opts)
        type(treeweave_opts) :: opts
        interface
            subroutine treeweave_default_opts_c(opts) bind(C, name="treeweave_default_opts")
                import :: treeweave_opts
                type(treeweave_opts), intent(out) :: opts
            end subroutine treeweave_default_opts_c
        end interface
        call treeweave_default_opts_c(opts)
    end function treeweave_default_opts

    ! Return the thread-local treeweave_last_error() text as a Fortran string
    ! (empty when there is no message).
    function treeweave_error_message() result(msg)
        character(len=:), allocatable     :: msg
        type(c_ptr)                       :: p
        character(kind=c_char), pointer   :: buf(:)
        integer(c_size_t)                 :: n
        integer                           :: i

        p = treeweave_last_error()
        if (.not. c_associated(p)) then
            msg = ""
            return
        end if
        n = c_strlen(p)
        if (n == 0_c_size_t) then
            msg = ""
            return
        end if
        call c_f_pointer(p, buf, [n])
        allocate (character(len=int(n)) :: msg)
        do i = 1, int(n)
            msg(i:i) = buf(i)
        end do
    end function treeweave_error_message

end module treeweave
