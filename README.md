# treeweave

[![CI](https://github.com/DiamonDinoia/treeweave/actions/workflows/ci.yml/badge.svg)](https://github.com/DiamonDinoia/treeweave/actions/workflows/ci.yml)
[![Bindings](https://github.com/DiamonDinoia/treeweave/actions/workflows/bindings.yml/badge.svg)](https://github.com/DiamonDinoia/treeweave/actions/workflows/bindings.yml)
[![codecov](https://codecov.io/gh/DiamonDinoia/treeweave/branch/main/graph/badge.svg)](https://codecov.io/gh/DiamonDinoia/treeweave)
[![docs](https://img.shields.io/badge/docs-treeweave-1f6feb.svg)](https://diamondinoia.github.io/treeweave/)
[![release](https://img.shields.io/github/v/release/DiamonDinoia/treeweave?display_name=tag&sort=semver)](https://github.com/DiamonDinoia/treeweave/releases)
[![License: BSD-3-Clause](https://img.shields.io/badge/License-BSD_3--Clause-blue.svg)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)

**Evaluate an expensive function millions of times, fast.**

treeweave takes a function that is slow to compute — a special function, a kernel,
the output of a costly model — and builds a drop-in replacement that returns the
same answer to your chosen accuracy, far more cheaply. You pay the cost once, at
fit time; every evaluation afterward is a quick polynomial lookup. It trades a
little memory for a lot of speed.

Internally it fits your function piecewise with low-order polynomials (monomial
basis, via [polyfit](https://github.com/DiamonDinoia/polyfit), sampled at
Chebyshev nodes and evaluated by Horner) on an adaptive tree, refining only where
the function is hard to approximate. It is conceptually like `chebfun`, but
panelled — so it reaches the same tolerance at lower polynomial order, which is
faster on modern SIMD hardware.

Use it from **C++** (header-only), or from **C**, **Fortran**, **Python**,
**Julia**, and **MATLAB/Octave** through a stable C ABI.

## Why treeweave

- **Fit once, evaluate forever.** Amortize an expensive function into a cheap
  polynomial lookup; trade a little memory for a lot of speed.
- **Adaptive paneling.** Low-order polynomial panels on an adaptive tree reach
  the target tolerance at lower order than a single global fit — SIMD-friendly.
- **Six languages, one core.** A stable C ABI (`libtreeweave_c`) underpins the
  Python, Julia, MATLAB/Octave, and Fortran wrappers; C++ is header-only.
- **Thread-safe by construction.** A fitted object is immutable and its
  `operator()` is safe to call concurrently.

## Include

```cpp
#include <treeweave/treeweave.hpp>
```

## Quick start

The everyday API is one call — `fit(f, lo, hi, tol)` — then you call the result
like the original function. The domain is the semi-open interval `[lo, hi)`;
as a convenience, evaluating exactly at `hi` returns the boundary value (the
last cell's polynomial), not `NaN`. Everything strictly outside `[lo, hi]`
returns `NaN` — points below `lo`, points above `hi`, and `NaN`/±Inf inputs
alike — on every eval path (scalar, batch, and sorted).

Every binding offers the same three eval routes: a **single point**, an
unordered **batch**, and — for 1-D inputs — a **`sorted` batch** fast path that
skips the internal sort when you can promise ascending `x` (see
[Sorted batches](#sorted-batches), ~3–4× faster). Each example below shows all
three.

### Python

```python
import numpy as np, treeweave

approx = treeweave.fit(lambda x: np.exp(x[0]), 0.0, 1.0, tol=1e-10)

approx(0.5)                                          # single point
xs = np.linspace(0.0, 1.0, 1000, endpoint=False)
approx(xs)                                           # batch: many points, any order
approx(xs, sorted=True)                              # promise xs is non-decreasing (1-D); ~3-4x faster
```

### Julia

```julia
using Treeweave

approx = fit(x -> exp(0.5x) + sin(3x), 0.0, 2.0, 1e-10)

approx(0.5)                                          # single point
xs = collect(range(0.0, 2.0; length = 1001))[1:end-1]
approx(xs)                                           # batch: many points, any order
approx(xs; sorted = true)                            # promise xs is non-decreasing (1-D); ~3-4x faster
```

### MATLAB / Octave

```matlab
% One source builds for both MATLAB (R2019b+) and GNU Octave (>= 6).
f   = @(x) exp(0.5*x(1)) + sin(3*x(1));
obj = treeweave(f, 0, 1, 1e-8);          % 1-D scalar fit (dims inferred)

y  = obj([0.5]);                          % single point  (1xdim -> 1xout_dim)
X  = linspace(0, 1, 1000)';               % N x dim
Y  = obj(X);                              % batch: many points, any order (or obj.eval(X))
Ys = obj(X, 'sorted', true);              % promise X is non-decreasing (1-D); ~3-4x faster
delete(obj);
```

### C++

```cpp
#include <treeweave/treeweave.hpp>

auto runge = [](double x) { return 1.0 / (1.0 + 25.0 * x * x); };
auto fn    = treeweave::fit(runge, -1.0, 1.0, /*tol=*/1e-10);

double y = fn(0.3);                              // evaluate at one point

std::vector<double> xs{/* ... */}, ys(xs.size());
fn(xs.data(), ys.data(), xs.size());             // batch: many points in one call, any order
fn.sorted(xs.data(), ys.data(), xs.size());      // batch you promise is non-decreasing, x[i] <= x[i+1] (1-D only); ~3-4x faster
```

### C

```c
#include <treeweave.h>

static void kernel(const double *x, double *y, void *context) { y[0] = exp(x[0]); }

double a = 0.0, b = 1.0, x = 0.5, y;
treeweave_t fn = treeweave_fit(kernel, /*input_dim=*/1, /*output_dim=*/1,
                               &a, &b, /*tol=*/1e-10, /*context=*/NULL, /*opts=*/NULL);
treeweave_eval(fn, &x, &y);            /* single point: y[] = f(x[])  */
y = treeweave_eval_1d(fn, x);          /* single point, by value (1/2/3-D, scalar out) */

double xs[1000], ys[1000];             /* batch: many points in one call         */
treeweave_batch(fn, xs, ys, 1000);     /* any order                              */
treeweave_sorted(fn, xs, ys, 1000);    /* you promise xs[i] <= xs[i+1], 1-D; ~3-4x */
fn = treeweave_free(fn);
```

### Fortran

```fortran
use, intrinsic :: iso_c_binding
use treeweave
type(c_ptr)        :: h
real(c_double)     :: a(1) = [0.0d0], b(1) = [1.0d0], x(1) = [0.5d0], y(1)
real(c_double)     :: xs(1000), res(1000)
integer(c_size_t)  :: n = 1000
h = treeweave_fit(c_funloc(kernel_exp), 1_c_int, 1_c_int, a, b, 1.0d-10, &
                  c_null_ptr, c_null_ptr)
call treeweave_eval(h, x, y)               ! single point
call treeweave_batch(h, xs, res, n)        ! batch: many points, any order
call treeweave_sorted(h, xs, res, n)       ! you promise xs(i) <= xs(i+1), 1-D; ~3-4x faster
h = treeweave_free(h)

! ... the kernel is a bind(C) subroutine, passed via c_funloc above:
subroutine kernel_exp(x, y, context) bind(C)
    real(c_double), intent(in)  :: x(*)
    real(c_double), intent(out) :: y(*)
    type(c_ptr),    value       :: context
    y(1) = exp(x(1))
end subroutine kernel_exp
```

### Sorted batches

The general batch path first **counting-sorts** the inputs by leaf so each leaf's
points are contiguous, runs one vectorized Horner stream per leaf, then permutes
the results back to the caller's order. The `sorted` route exists because that
sort is pure overhead when the inputs are *already* ascending: it skips the sort
and the permute-back and streams straight through the leaves, ~3–4× faster on 1-D
batches.

It pays off more often than it looks, because a lot of workloads hand you
ascending `x` for free:

- **Regular grids / `linspace`** — plotting, lookup tables, and resampling sweep
  a domain left-to-right.
- **Quadrature** — Gauss/Chebyshev/Clenshaw–Curtis abscissae are generated in
  ascending order.
- **Time marching** — ODE/PDE integrators and signal processing advance
  monotonically in `t`.
- **Parameter sweeps** — continuation methods and line searches step one
  coordinate monotonically.

The caller *promises* `x[i] <= x[i+1]`; treeweave does not verify it, so an
unsorted input on this path yields wrong values (not an error). When in doubt,
use the plain batch path — it sorts internally and is always correct. Both paths
NaN-fill out-of-domain points. `sorted` requires `input_dim == 1` — in higher
dimensions there's no cheap "already sorted" guarantee and the per-point Horner
cost dominates the sort anyway, so the gain would be marginal.

### Multi-dimensional

The examples above are 1-D for brevity. The same API fits 2-D/3-D inputs and
vector outputs — corners become arrays, the callback takes/returns arrays, and
the single/batch/`sorted`/transposed routes all carry over (`sorted` stays 1-D
only). The bindings mirror this shape (list corners in Python/Julia, `input_dim`
/ `output_dim` in C/Fortran).

```cpp
// 2-D input, scalar output
auto bump = [](std::array<double, 2> x) -> std::array<double, 1> {
  return {std::exp(-100.0 * (x[0]-0.5)*(x[0]-0.5) - (x[1]-0.5)*(x[1]-0.5))};
};
auto fn2  = treeweave::fit(bump, std::array{0.0, 0.0}, std::array{1.0, 1.0}, 1e-8);
double y  = fn2({0.3, 0.7})[0];

// 3-D input, vector output — fn3(x) returns std::array<double, output_dim>
auto fn3  = treeweave::fit(field, std::array{-1.0,-1.0,-1.0}, std::array{1.0,1.0,1.0}, 1e-8);
```

```python
approx = treeweave.fit(bump, [0.0, 0.0], [1.0, 1.0], tol=1e-8)   # 2-D -> scalar
approx(pts)                       # pts: (N, 2) -> (N,);  transposed=True -> (out_dim, N)
```

See the [guides](https://diamondinoia.github.io/treeweave) for per-language ND
examples (incl. the transposed/SoA output route).

## Install

**Prebuilt binaries (recommended).**

- **Python:** `pip install treeweave` (latest release from PyPI). The x86-64
  wheel dispatches SSE4.2 / AVX2 / AVX-512 at runtime; the C ABI is linked in
  statically. To test an unreleased change, every push to `main` publishes a
  staging wheel to [TestPyPI](https://test.pypi.org/project/treeweave/):

  ```sh
  pip install --index-url https://test.pypi.org/simple/ \
              --extra-index-url https://pypi.org/simple/ treeweave
  ```
- **Julia:** `Pkg.add(url="https://github.com/DiamonDinoia/treeweave", subdir="bindings/julia/Treeweave")`
  downloads the matching `libtreeweave_c` from the GitHub Release on first build.
- **MATLAB / Octave:** built from source (no prebuilt MEX — Octave has no stable
  MEX ABI). With MATLAB or `mkoctfile` on `PATH`:

  ```sh
  cmake --preset bindings-matlab      # or bindings-octave for the license-free path
  cmake --build build/bindings-matlab -j
  ```

  Then `addpath` the `bindings/matlab` dir and the generated MEX dir (see the
  [MATLAB/Octave guide](https://diamondinoia.github.io/treeweave)).
- **C / Fortran:** download a `treeweave-<version>-<platform>` archive
  from [Releases](https://github.com/DiamonDinoia/treeweave/releases) — it carries
  `include/treeweave.h`, `libtreeweave_c`, and a `find_package(treeweave)` package.

**FetchContent (easiest source path for CMake).**

```cmake
include(FetchContent)
FetchContent_Declare(treeweave
  GIT_REPOSITORY https://github.com/DiamonDinoia/treeweave.git
  GIT_TAG main)
FetchContent_MakeAvailable(treeweave)
target_link_libraries(my_app PRIVATE treeweave::treeweave)   # header-only C++
```

**Installed package / vendored.**

```cmake
find_package(treeweave REQUIRED)                       # installed C ABI
target_link_libraries(my_app PRIVATE treeweave::treeweave_c)

add_subdirectory(extern/treeweave)                     # vendored
target_link_libraries(my_app PRIVATE treeweave::treeweave)
```

The header-only C++ template API (`treeweave::treeweave`) instantiates against
FetchContent-only headers, so it is **not** part of the installed
`find_package` package — consume it in-tree. The installable surface is the
**C ABI** (`treeweave::treeweave_c` / `treeweave::treeweave_c_static`).

## Bindings

A stable C ABI (`libtreeweave_c`, header [`include/treeweave.h`](include/treeweave.h))
underpins every wrapper, all under [`bindings/`](bindings/). Each is an opt-in
CMake option (all default OFF); a missing toolchain is skipped gracefully.

| Language | Option | Install / build |
|----------|--------|-----------------|
| Python | `TREEWEAVE_BUILD_PYTHON` | `pip install treeweave` |
| Julia | `TREEWEAVE_BUILD_JULIA` | `Pkg.add(url=…, subdir="bindings/julia/Treeweave")` |
| MATLAB/Octave | `TREEWEAVE_BUILD_MATLAB` | CMake builds the MEX via [mwrap](https://github.com/zgimbutas/mwrap) |
| C++ | — (header-only) | `#include <treeweave/treeweave.hpp>` |
| C | `TREEWEAVE_BUILD_C_API` | `find_package(treeweave)` → `treeweave::treeweave_c` |
| Fortran | `TREEWEAVE_BUILD_FORTRAN` | `cmake --preset bindings-fortran` |

See [`bindings/README.md`](bindings/README.md) for per-language usage and the
cross-language parity check.

## Benchmarks

A small, stable set of batch-eval benchmarks runs on every push to `main` and is
tracked at
[diamondinoia.github.io/treeweave/dev/bench](https://diamondinoia.github.io/treeweave/dev/bench/),
which charts each series over time and flags regressions.

## Docs

- Guides + API reference: [diamondinoia.github.io/treeweave](https://diamondinoia.github.io/treeweave/)
- Design note: [`docs/how-treeweave-works.md`](docs/how-treeweave-works.md)
- API entry point: [`include/treeweave/treeweave.hpp`](include/treeweave/treeweave.hpp)

## Limitations

- treeweave can use a _lot_ of memory on oscillatory or rapidly-varying
  functions. If your function is periodic, fit one period.
- It can't fit through a singularity — shift the domain off the singular point,
  or piecewise a small set of fits around it.
- The fit covers the half-open interval `[x0, x1)`. Evaluating exactly at `x1`
  returns the boundary value (the last panel's polynomial); every other
  out-of-domain point — below `x0`, above `x1`, or `NaN`/±Inf — returns `NaN`,
  on all eval paths. Out-of-domain evaluation is never an error, and no input
  causes out-of-bounds access.
- No bounds checking on inputs — the caller must sanitise.

## Acknowledgements

treeweave draws its inspiration from
[**baobzi**](https://github.com/flatironinstitute/baobzi) by **Robert Blackwell**
(Flatiron Institute) — an "adaptive fast function approximator based on tree
search". baobzi is the origin of the adaptive-tree-of-polynomial-panels idea
that treeweave builds on; treeweave shares no code with it and is a clean
reimplementation that re-architects the fit/eval pipeline around
[polyfit](https://github.com/DiamonDinoia/polyfit) and
[POET](https://github.com/DiamonDinoia/POET) and adds the multi-language C ABI.
See [`NOTICE`](NOTICE). Thank you, Robert.

For the numerical-analysis background behind treeweave's choices (Chebyshev
nodes, the monomial basis at low degree, and adaptive paneling in one and more
dimensions), see **Alex Barnett**'s talk
[*What everyone should know about function approximation*](https://users.flatironinstitute.org/~ahb/talks/fwam25.pdf)
(FWAM7, Flatiron Institute, 2025) — a clear tour of the methods treeweave
automates. Thank you, Alex.

## License

BSD-3-Clause — see [`LICENSE`](LICENSE).
