# treeweave host-language bindings

Thin wrappers over the C ABI (`libtreeweave_c`, [`treeweave.h`](../include/treeweave.h)) for **Python**, **Julia**, **MATLAB/Octave**, and **Fortran**. All reuse the pre-instantiated C++ templates — no re-compilation.

Python, Julia, and MATLAB infer `dim` and `out_dim` from the callable and are called directly with a point or batch; `sorted` and `transposed` flags select fast paths. The Fortran binding is explicit: named procedures, `input_dim` / `output_dim` passed directly, no inference.

| Supported | values |
|-----------|--------|
| input dim  | 1, 2, 3 |
| output dim | 1, 2, 3 (a 1-D vector-valued fit is `dim=1, out_dim>1`) |
| dtype      | `f64` (double), `f32` (float) |

**Domain:** `[a, b)` — evaluating at `b` returns the boundary value; every other out-of-domain point returns `NaN`, branchless. **Exceptions:** a raising callback is re-thrown after the C ABI unwinds cleanly.

---

## Python (nanobind)

```bash
pip install ./bindings/python        # scikit-build-core fetches nanobind, builds the extension
pytest bindings/python/tests
```

```python
import numpy as np, treeweave

N = 1000  # zeta_N(s) = sum_{k=1..N} k**-s — expensive; fit once, eval a polynomial
f = treeweave.fit(lambda x: np.sum(np.arange(1.0, N + 1.0) ** (-x[0])), 2.0, 10.0, tol=1e-8)
f(3.5)                          # scalar
f(np.linspace(2, 10, 100))              # vectorized -> (100,)
f(np.linspace(2, 10, 100), sorted=True) # 1-D ascending fast path
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

```julia
using Treeweave
N = 1000                                              # zeta_N(s) = sum_{k=1..N} k^-s
b = fit(s -> sum(k -> k^(-s), 1:N), 2.0, 10.0, 1e-8)  # dim==1: f(scalar)
b(3.5)                                                # point
b(collect(range(2, 10, length=100)))                 # dim==1 batch -> length-100 vector

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

```matlab
N   = 1000;                           % zeta_N(s) = sum_{k=1..N} k^-s
f   = @(x) sum((1:N).^(-x(1)));       % expensive; fit once, eval a polynomial
obj = treeweave(f, 2, 10, 1e-8);      % dim & out_dim inferred
obj(linspace(2, 10, 100)')         % subsref syntax; or obj.eval(...)
obj(linspace(2, 10, 100)', 'sorted', true)  % 1-D ascending fast path

g = treeweave(@(x) [exp(0.3*x(1))+sin(2*x(2)), cos(x(1)*x(2))+2, x(1)^2+x(2)+1], ...
           [0.2 0.2], [1.5 1.5], 1e-7);     % out_dim inferred (=3); degree auto per CPU
g(rand(100,2)*1.3 + 0.2)                    % -> 100x3
g(rand(100,2)*1.3 + 0.2, 'transposed', true) % -> 3x100
```

A C++ fit failure raises `treeweave:fit`; a callback error raises `treeweave:callback`.
See [`matlab/README.md`](https://github.com/DiamonDinoia/treeweave/blob/main/bindings/matlab/README.md).

## Fortran (iso_c_binding)

```bash
cmake -S . -B build -DTREEWEAVE_BUILD_FORTRAN=ON
cmake --build build --target treeweave_fortran_test treeweave_fortran_example
ctest --test-dir build -R fortran_treeweave --output-on-failure
```

```fortran
use, intrinsic :: iso_c_binding
use treeweave
type(c_ptr)    :: h
real(c_double) :: a(1) = [2.0_c_double], b(1) = [10.0_c_double], x(1), y(1)
h = treeweave_fit(c_funloc(kernel), 1_c_int, 1_c_int, a, b, 1.0e-10_c_double, &
               c_null_ptr, c_null_ptr)   ! kernel: bind(C) zeta_N(s) = sum_{k=1..N} k^-s
x(1) = 3.5_c_double
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

Missing toolchains are detected and skipped. (The Julia CTest passes `LIBTREEWEAVE_C` pointing
at the freshly-built shared library.)

## Cross-language parity

[`parity/run_parity.sh`](parity/run_parity.sh) fits the same 2-D → 3-D kernel in
C (the reference), Python, and Julia, evaluates a fixed point set, and checks
that every language agrees with the C result.
