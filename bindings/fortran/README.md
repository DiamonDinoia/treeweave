# treeweave Fortran binding

A thin Fortran 2018 wrapper over the treeweave C ABI
([`../../include/treeweave.h`](../../include/treeweave.h)), built on the intrinsic
`iso_c_binding` module. The module exposes every C entry point as a Fortran
procedure of the same name, so the mapping is one-to-one and explicit.

The API, the `context` pattern, the options and the worked examples live in the
[Fortran guide](https://diamondinoia.github.io/treeweave/guides/fortran.html).

## Compiler requirement

The binding uses `error stop <integer-expression>`, which is Fortran 2018.
gfortran >= 10 supports it. gfortran 8 (the RHEL 8 system `/usr/bin/f95`)
accepts the syntax without a diagnostic but may produce wrong stop-code
behaviour.

`enable_language(Fortran)` picks the first `gfortran` or `f95` on `PATH`, which
on RHEL 8 resolves to gfortran 8.5. Name the compiler at configure time, with
`-DCMAKE_Fortran_COMPILER=$(which gfortran)` or `FC=$(which gfortran)`, after
loading a modern toolchain.

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
| `double treeweave_eval_1d(h, double x0)`            | `real(c_double) function treeweave_eval_1d(f, x0)`, `x0` by value |
| `double treeweave_eval_2d(h, x0, x1)`               | `real(c_double) function treeweave_eval_2d(f, x0, x1)`, all by value |
| `double treeweave_eval_3d(h, x0, x1, x2)`           | `real(c_double) function treeweave_eval_3d(f, x0, x1, x2)` |
| `float treeweavef_eval_1d(h, float x0)`             | `real(c_float) function treeweavef_eval_1d(f, x0)` |
| `float treeweavef_eval_2d(h, x0, x1)`               | `real(c_float) function treeweavef_eval_2d(f, x0, x1)` |
| `float treeweavef_eval_3d(h, x0, x1, x2)`           | `real(c_float) function treeweavef_eval_3d(f, x0, x1, x2)` |
| `const char *treeweave_last_error(void)`            | `type(c_ptr)`; `treeweave_error_message()` → Fortran string  |

The `treeweave_*` procedures operate on `double`; the `treeweavef_*` twins (fit + the
eval family) on `float`. Introspection and lifetime (`treeweave_dtype`,
`treeweave_input_dim`, `treeweave_output_dim`, `treeweave_memory_usage`,
`treeweave_print_stats`, `treeweave_free`) are dtype-independent, with no `treeweavef_` twin.

## Build from source

The preset builds `treeweave_fortran_test` and `treeweave_fortran_example`,
both linking the shared `treeweave_c`. Without a Fortran compiler, CMake prints
a STATUS message and skips them.

<!-- literalinclude: tools/ci/docs-recipes.sh start-after: # BEGIN DOCS_FORTRAN_DEV end-before: # END DOCS_FORTRAN_DEV dedent: 4 -->
```bash
cmake --preset bindings-fortran
cmake --build build/bindings-fortran -j
```

<!-- literalinclude: tools/ci/docs-recipes.sh start-after: # BEGIN DOCS_FORTRAN_TEST end-before: # END DOCS_FORTRAN_TEST dedent: 4 -->
```bash
ctest --test-dir build/bindings-fortran -R fortran_treeweave --output-on-failure
```

## Files

| File | Purpose |
|------|---------|
| `treeweave.f90` | The `module treeweave` binding: interfaces, `treeweave_opts`, parameters, `treeweave_error_message` |
| `example.f90` | Fit, single and batch eval, and the `context` pattern; the source the guide embeds |
| `test_treeweave.f90` | Self-checking test (`error stop` on failure); covers scalar eval, batch, sorted, transposed, print_stats, f32 and context |
