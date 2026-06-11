# treeweave host-language bindings

Thin wrappers that let you fit and evaluate a treeweave piecewise-polynomial
approximant from **Python**, **Julia**, **MATLAB/Octave**, or **Fortran**,
passing a *function handle written in the host language* into the fit and
getting C++ fit failures back as native exceptions.

All sit on the same C ABI (`libtreeweave_c`, see [`../include/treeweave.h`](../include/treeweave.h))
and reuse its pre-instantiated shapes — they do **not** re-instantiate the C++
templates. The mechanism is identical in spirit across languages: the wrapper
hands the C fit a **C function pointer** (a *trampoline*) plus an opaque
`context` pointer carrying the host callable; the trampoline re-enters the host
language once per Chebyshev sample.

In the Python, Julia, and MATLAB wrappers the fitted object is simply *called*
— with a point or a batch — and `fit` infers `dim` and `out_dim` (the latter by
probing your function once at the box midpoint), so the common call is just
`fit(f, a, b, tol)`. Two optional flags, `sorted` (1-D ascending fast path) and
`transposed` (struct-of-arrays `out_dim × N` output), select the alternate
batch modes. The Fortran binding is deliberately faithful instead: explicit
named procedures with explicit `input_dim` / `output_dim`, no call operator.

| Supported | values |
|-----------|--------|
| input dim  | 1, 2, 3 |
| output dim | 1, 2, 3 (a 1-D vector-valued fit is `dim=1, out_dim>1`) |
| dtype      | `f64` (double), `f32` (float) |

**Domain note (all languages):** the fit domain is the half-open box `[a, b)` —
the target only needs to be defined there. As a convenience, evaluating exactly
at the upper corner `b` returns the boundary value (the last cell's polynomial),
not `NaN`. Every other out-of-domain point returns `NaN` uniformly across all
eval paths — points below `a`, points above `b`, and `NaN`/±Inf inputs — so the
scalar, batch, and sorted APIs agree point-for-point. The batch hot path stays
branchless (the domain test is a SIMD mask, not a branch).

**Reverse-exception safety (all languages):** if your host callback raises
*during* the fit, the trampoline catches it, writes `NaN`, short-circuits the
remaining samples (it never re-enters the host language again), lets the C fit
unwind cleanly, and then re-raises the original exception. A throwing callback
never crashes the interpreter / corrupts the C++ stack.

---

## Python (nanobind)

```bash
pip install ./bindings/python        # scikit-build-core fetches nanobind, builds the extension
pytest bindings/python/tests
```

```python
import numpy as np, treeweave

f = treeweave.fit(lambda x: np.exp(0.5*x[0]) + np.sin(3*x[0]), 0.0, 1.0, tol=1e-8)
f(0.5)                          # scalar
f(np.linspace(0, 1, 100))               # vectorized -> (100,)
f(np.linspace(0, 1, 100), sorted=True)  # 1-D ascending fast path
print(f)                        # dtype, dim, out_dim, memory_usage

# 2D -> 3D vector-valued; out_dim is inferred by probing the callable
g = treeweave.fit(lambda x: np.array([np.exp(0.3*x[0]) + np.sin(2*x[1]),
                                   np.cos(x[0]*x[1]) + 2.0,
                                   x[0]**2 + x[1] + 1.0]),
               [0.2, 0.2], [1.5, 1.5], tol=1e-7)
X = np.random.uniform([0.2, 0.2], [1.5, 1.5], size=(64, 2))
g(X)                   # -> (64, 3)
g(X, transposed=True)  # -> (3, 64)
```

A C++ fit failure raises `RuntimeError(treeweave_last_error())`; a too-tight `tol`
with a low `max_depth` raises with `MaxDepthExceeded` / `MemoryBudgetExceeded`
in the message. See [`python/README.md`](https://github.com/DiamonDinoia/treeweave/blob/main/bindings/python/README.md).

## Julia

```bash
# No env var needed: the package walks up to find a sibling build*/ tree.
julia --project=bindings/julia/Treeweave -e 'using Pkg; Pkg.test()'
```

The library is located in order: `LIBTREEWEAVE_C` (explicit path) → `deps/deps.jl`
(written by `Pkg.build("Treeweave")`) → the loader search path → a sibling CMake
`build*/libtreeweave_c.<ext>`. So an in-repo `Pkg.test()` works once the project
has been built; set `LIBTREEWEAVE_C=/path/to/libtreeweave_c.so` to override.

```julia
using Treeweave
b = fit(x -> exp(0.5x) + sin(3x), 0.0, 1.0, 1e-8)   # dim==1: f(scalar)
b(0.5)                                                # point
b(collect(range(0, 1, length=100)))                  # dim==1 batch -> length-100 vector

# 2D -> 3D: f(coords...) returns an indexable of length out_dim (inferred)
g = fit((x, y) -> (exp(0.3x) + sin(2y), cos(x*y) + 2, x^2 + y + 1),
        [0.2, 0.2], [1.5, 1.5], 1e-7)
g([0.5, 0.7])                                         # point -> length-3 Vector
g(rand(64, 2))                                        # batch -> 64×3
g(rand(64, 2); transposed=true)                       # -> 3×64
```

A `NULL` fit raises `error(treeweave_last_error())`; a raising closure is re-thrown
after the fit. See [`julia/Treeweave/README.md`](https://github.com/DiamonDinoia/treeweave/blob/main/bindings/julia/Treeweave/README.md).

## MATLAB / Octave (MEX)

Built entirely from CMake — no Makefile. [`treeweave.mw`](matlab/treeweave.mw) is
the mwrap source of truth; CMake fetches the
[mwrap](https://github.com/zgimbutas/mwrap) generator via CPM, generates the
gateway (`treeweave_mex_gen.cpp`) + `tw_*.m` stubs in the build tree, and
compiles the MEX (MATLAB via `matlab_add_mex`, Octave via `mkoctfile`):

```bash
# MATLAB and/or Octave — whichever is found is built (MATLAB links
# -static-libstdc++ so the .mexa64 is independent of MATLAB's libstdc++).
cmake --preset bindings-matlab
cmake --build build/bindings-matlab -j
ctest --test-dir build/bindings-matlab -R matlab_treeweave --output-on-failure

# License-free Octave path (selects the Octave backend when only mkoctfile is
# present; Octave is not shipped prebuilt — no stable MEX ABI across versions).
cmake --preset bindings-octave
cmake --build build/bindings-octave -j
ctest --test-dir build/bindings-octave -R matlab_treeweave --output-on-failure
```

One source serves both: Octave can pass a `function_handle` into a MEX and
`feval` it, so the trampoline needs no Octave-specific path. To change the
binding, edit `treeweave.mw` — the next build regenerates automatically. The
generated gateway and stubs live in the build tree and are never committed.

```matlab
f   = @(x) exp(0.5*x(1)) + sin(3*x(1));
obj = treeweave(f, 0, 1, 1e-8);       % dim & out_dim inferred
obj(linspace(0, 1, 100)')          % subsref syntax; or obj.eval(...)
obj(linspace(0, 1, 100)', 'sorted', true)   % 1-D ascending fast path

g = treeweave(@(x) [exp(0.3*x(1))+sin(2*x(2)), cos(x(1)*x(2))+2, x(1)^2+x(2)+1], ...
           [0.2 0.2], [1.5 1.5], 1e-7);     % out_dim inferred (=3); degree auto per CPU
g(rand(100,2)*1.3 + 0.2)                    % -> 100x3
g(rand(100,2)*1.3 + 0.2, 'transposed', true) % -> 3x100
```

A C++ fit failure raises `treeweave:fit`; a callback error raises `treeweave:callback`.
See [`matlab/README.md`](https://github.com/DiamonDinoia/treeweave/blob/main/bindings/matlab/README.md).

> **libstdc++ / GLIBCXX note (fallback only).** The canonical `Makefile` (and
> the CMake glue) link the C++ runtime statically (`-static-libstdc++
> -static-libgcc`), so the `.mexa64` does not depend on MATLAB's bundled
> libstdc++. Only if you build the mex some other way and MATLAB reports
> `version GLIBCXX_3.4.xx not found` do you need a workaround — e.g. launch
> MATLAB with `LD_PRELOAD=/usr/lib/x86_64-linux-gnu/libstdc++.so.6`.

## Fortran (iso_c_binding)

A faithful, thin Fortran 2008 binding: the `module treeweave` binds every C symbol
by name. Unlike the other wrappers it has no call operator and no inference —
you pass `input_dim` / `output_dim` explicitly and write the target as a
`bind(C)` callback, exactly as a C consumer would.

```bash
cmake -S . -B build -DTREEWEAVE_BUILD_FORTRAN=ON
cmake --build build --target treeweave_fortran_test treeweave_fortran_example
ctest --test-dir build -R fortran_treeweave --output-on-failure
```

```fortran
use, intrinsic :: iso_c_binding
use treeweave
type(c_ptr)    :: h
real(c_double) :: a(1) = [0.0_c_double], b(1) = [1.0_c_double], x(1), y(1)
h = treeweave_fit(c_funloc(kernel), 1_c_int, 1_c_int, a, b, 1.0e-10_c_double, &
               c_null_ptr, c_null_ptr)   ! kernel: bind(C) void(x, y, context)
x(1) = 0.5_c_double
call treeweave_eval(h, x, y)
h = treeweave_free(h)
```

State that a C++ lambda would capture goes through the `context` pointer
(`c_loc` of a `bind(C)` type, recovered with `c_f_pointer`). See
[`fortran/README.md`](https://github.com/DiamonDinoia/treeweave/blob/main/bindings/fortran/README.md)
for the full `iso_c_binding` mapping and the callback/context pattern.

---

## Building via the top-level CMake

The wrappers are also wired into CTest, each behind an option (all default OFF):

```bash
cmake -B build -DTREEWEAVE_BUILD_PYTHON=ON -DTREEWEAVE_BUILD_JULIA=ON \
    -DTREEWEAVE_BUILD_MATLAB=ON -DTREEWEAVE_BUILD_FORTRAN=ON
cmake --build build
ctest --test-dir build -R "python_treeweave|julia_treeweave|matlab_treeweave|fortran_treeweave"
```

Missing toolchains are detected and skipped with a STATUS message rather than
failing configuration. (The Python CTest additionally needs `numpy` + `pytest`
in the interpreter CMake selected; the Julia CTest passes `LIBTREEWEAVE_C` pointing
at the freshly-built shared library.)

## Cross-language parity

[`parity/run_parity.sh`](parity/run_parity.sh) fits the same 2-D → 3-D kernel in
C (the reference), Python, and Julia, evaluates a fixed point set, and checks
that every language agrees with the C result. See that directory for details.
