# treeweave Fortran binding

A thin **Fortran 2008** wrapper over the treeweave C ABI
([`../../include/treeweave.h`](../../include/treeweave.h)), built on the intrinsic
`iso_c_binding` module. Every C entry point is exposed as a Fortran procedure of
the same name, so the mapping is one-to-one and explicit.

Unlike the Python / Julia / MATLAB wrappers, this binding is deliberately
*faithful* rather than ergonomic: there is no call operator, no keyword
arguments, and no dimension inference. The caller passes `input_dim` /
`output_dim` explicitly, exactly as a C consumer would.

## Using it

```fortran
use, intrinsic :: iso_c_binding
use treeweave
```

1. Write your target as a `bind(C)` procedure matching the C callback
   `void f(const double *x, double *y, void *context)`:

   ```fortran
   subroutine kernel(x, y, context) bind(C)
       use, intrinsic :: iso_c_binding
       real(c_double), intent(in)  :: x(*)   ! input_dim coordinates
       real(c_double), intent(out) :: y(*)   ! output_dim results
       type(c_ptr),    value       :: context
       y(1) = exp(x(1))
   end subroutine
   ```

   Callbacks must be `bind(C)` so `c_funloc()` yields a C-callable pointer;
   module procedures are the cleanest way to provide them. The `treeweavef_*`
   twins use `real(c_float)` for `x`/`y`.

2. Fit, passing `c_funloc(kernel)`. Pass `c_null_ptr` for `opts` to use
   `treeweave_default_opts`, or `c_loc(my_opts)` with a `target` `treeweave_opts`:

   ```fortran
   real(c_double) :: a(1) = [0.0_c_double], b(1) = [1.0_c_double]
   type(c_ptr)    :: h
   h = treeweave_fit(c_funloc(kernel), 1_c_int, 1_c_int, a, b, 1.0e-10_c_double, &
                  c_null_ptr, c_null_ptr)
   if (.not. c_associated(h)) then
       write (*,'(2A)') "fit failed: ", treeweave_error_message()
       error stop 1
   end if
   ```

3. Evaluate, then free:

   ```fortran
   real(c_double) :: x(1) = [0.5_c_double], y(1)
   call treeweave_eval(h, x, y)            ! single point
   call treeweave_batch(h, xs, res, n)     ! n points, AoS (n: integer(c_size_t))
   call treeweave_sorted(h, xs, res, n)    ! 1-D ascending fast path (~3-4x faster)
   call treeweave_transposed(h, xs, soa, n)! n points, SoA output (out_dim > 1)
   h = treeweave_free(h)                   ! returns c_null_ptr
   ```

### The `context` pattern

A Fortran `bind(C)` procedure cannot capture state, so `context` is the
stand-in for a closure's captures (mirroring
[`examples/C/with_context.c`](../../examples/C/with_context.c)). Put the state
in a `bind(C)` derived type, pass `c_loc()` of a `target` instance, and recover
it inside the callback with `c_f_pointer`:

```fortran
type, bind(C) :: params_t
    real(c_double) :: amplitude
    real(c_double) :: frequency
end type

subroutine kernel_ctx(x, y, context) bind(C)
    real(c_double), intent(in)  :: x(*)
    real(c_double), intent(out) :: y(*)
    type(c_ptr),    value       :: context
    type(params_t), pointer     :: p
    call c_f_pointer(context, p)
    y(1) = p%amplitude * sin(p%frequency * x(1))
end subroutine

! ... in the caller:
type(params_t), target :: params
params%amplitude = 2.5_c_double; params%frequency = 7.0_c_double
h = treeweave_fit(c_funloc(kernel_ctx), 1_c_int, 1_c_int, a, b, tol, &
               c_loc(params), c_null_ptr)
```

## iso_c_binding mapping

| C (`treeweave.h`)                                   | Fortran (`module treeweave`)                                  |
|--------------------------------------------------|------------------------------------------------------------|
| `treeweave_t` (opaque `struct *`)                   | `type(c_ptr)`                                              |
| `treeweave_func_t` / `treeweavef_func_t`               | `bind(C)` subroutine, passed as `c_funloc(...)` → `type(c_funptr)` |
| `int input_dim, output_dim`                      | `integer(c_int), value`                                   |
| `const double *a` / `const float *a`             | `real(c_double)` / `real(c_float)`, `intent(in) :: a(*)`  |
| `double tol`                                     | `real(c_double), value`                                   |
| `void *context`                                  | `type(c_ptr), value`                                      |
| `const treeweave_opts *opts`                        | `type(c_ptr), value` (`c_null_ptr` ⇒ defaults, or `c_loc(opts)`) |
| `treeweave_opts` struct                             | `type, bind(C) :: treeweave_opts` (five `integer(c_int)`)    |
| `size_t n`                                       | `integer(c_size_t), value`                                |
| `double *const *soa`                             | `type(c_ptr), intent(in) :: soa(*)`                       |
| `treeweave_dtype_t` / `treeweave_tol_kind_t`           | `integer(c_int)` parameters (`TREEWEAVE_F64`, `TREEWEAVE_RELATIVE_MAX`, …) |
| `const char *treeweave_last_error(void)`            | `type(c_ptr)`; `treeweave_error_message()` → Fortran string  |

The `treeweave_*` procedures operate on `double`; the `treeweavef_*` twins (fit + the
eval family) on `float`. Introspection and lifetime (`treeweave_dtype`,
`treeweave_input_dim`, `treeweave_output_dim`, `treeweave_memory_usage`,
`treeweave_print_stats`, `treeweave_free`) are dtype-independent — no `treeweavef_` twin.

## Build

The binding is wired into the top-level CMake behind `TREEWEAVE_BUILD_FORTRAN`
(default OFF). It builds a self-checking test (`treeweave_fortran_test`) and an
example (`treeweave_fortran_example`), both linking the shared `treeweave_c`:

```bash
cmake -S . -B build -DTREEWEAVE_BUILD_FORTRAN=ON
cmake --build build --target treeweave_fortran_test treeweave_fortran_example
ctest --test-dir build -R fortran_treeweave --output-on-failure
```

A missing Fortran compiler degrades to a STATUS message and a skipped test.

## Files

| File              | Purpose                                                        |
|-------------------|----------------------------------------------------------------|
| `treeweave.f90`      | The `module treeweave` binding (interfaces, `treeweave_opts`, parameters, `treeweave_error_message`) |
| `example.f90`     | Fit + single/batch eval + the `context` pattern                |
| `test_treeweave.f90` | Self-checking test (`error stop` on failure → nonzero exit)    |
