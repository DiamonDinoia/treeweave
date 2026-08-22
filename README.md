# treeweave

[![CI](https://img.shields.io/github/actions/workflow/status/DiamonDinoia/treeweave/ci.yml?branch=main&label=CI)](https://github.com/DiamonDinoia/treeweave/actions/workflows/ci.yml)
[![codecov](https://codecov.io/gh/DiamonDinoia/treeweave/branch/main/graph/badge.svg)](https://codecov.io/gh/DiamonDinoia/treeweave)
[![docs](https://img.shields.io/badge/docs-treeweave-1f6feb.svg)](https://diamondinoia.github.io/treeweave/)
[![PyPI](https://img.shields.io/pypi/v/treeweave)](https://pypi.org/project/treeweave/)
[![npm](https://img.shields.io/npm/v/%40flatironinstitute%2Ftreeweave)](https://www.npmjs.com/package/@flatironinstitute/treeweave)
[![License: BSD-3-Clause](https://img.shields.io/badge/License-BSD_3--Clause-blue.svg)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)

[![Python](https://img.shields.io/github/actions/workflow/status/DiamonDinoia/treeweave/python.yml?branch=main&label=Python)](https://github.com/DiamonDinoia/treeweave/actions/workflows/python.yml)
[![Julia](https://img.shields.io/github/actions/workflow/status/DiamonDinoia/treeweave/julia.yml?branch=main&label=Julia)](https://github.com/DiamonDinoia/treeweave/actions/workflows/julia.yml)
[![Fortran](https://img.shields.io/github/actions/workflow/status/DiamonDinoia/treeweave/fortran.yml?branch=main&label=Fortran)](https://github.com/DiamonDinoia/treeweave/actions/workflows/fortran.yml)
[![Octave](https://img.shields.io/github/actions/workflow/status/DiamonDinoia/treeweave/octave.yml?branch=main&label=Octave)](https://github.com/DiamonDinoia/treeweave/actions/workflows/octave.yml)
[![JavaScript](https://img.shields.io/github/actions/workflow/status/DiamonDinoia/treeweave/js.yml?branch=main&label=JS%2FWASM)](https://github.com/DiamonDinoia/treeweave/actions/workflows/js.yml)
[![MATLAB](https://img.shields.io/github/actions/workflow/status/DiamonDinoia/treeweave/matlab.yml?branch=main&label=MATLAB)](https://github.com/DiamonDinoia/treeweave/actions/workflows/matlab.yml)

## What it solves

treeweave turns repeated calls to a costly function into a one-time fit plus fast polynomial evaluation.

When `f(x)` is expensive but smooth on a bounded domain, treeweave samples it once, builds a compact polynomial approximation, and reuses that approximation for cheap lookups. The target is any workload that evaluates the same function many times: special functions, kernels, simulations, calibration models, table-backed interpolators.

## Benchmarks

Each chart fits a Riemann-zeta sum on `[2, 10]` to `1e-10` with a naive algorithm, then compares against treeweave. Bars are Mevals/s on a log scale; higher is better.

### Single eval

![Riemann-zeta single-eval throughput](https://raw.githubusercontent.com/DiamonDinoia/treeweave/benchmark-results/throughput_single.svg)

### Batch

![Riemann-zeta batch throughput](https://raw.githubusercontent.com/DiamonDinoia/treeweave/benchmark-results/throughput_multi.svg)

### Sorted batch

![Riemann-zeta sorted-batch throughput](https://raw.githubusercontent.com/DiamonDinoia/treeweave/benchmark-results/throughput_sorted.svg)

See the [performance guide](https://diamondinoia.github.io/treeweave/guides/performance.html) for throughput and latency details.

## What it is

treeweave fits low-order polynomial panels on an adaptive tree. The fitted object is immutable. C++, C, Python, Julia, MATLAB/Octave, Fortran and JavaScript/TypeScript can all evaluate it.

The fit covers `[a, b)`. Evaluation still accepts the closed interval `[a, b]`, because an input exactly at `b` lands in the last panel and returns a finite value. Pass `[a, b]` even when `f` is undefined at `b`. Inputs below `a`, inputs above `b`, and `NaN` or infinite inputs all return `NaN`.

## Examples and install

### Python

```python
import math
import treeweave

def zeta(s):
    return sum(math.pow(k, -s[0]) for k in range(1, 1001))

approx = treeweave.fit(zeta, 2.0, 10.0, tol=1e-10)
print(approx(3.5))
```

Omit the callable and `fit` becomes a decorator, the `functools.cache` spelling:

```python
@treeweave.fit(2.0, 10.0, tol=1e-10)
def zeta(s):
    return sum(math.pow(k, -s[0]) for k in range(1, 1001))

print(zeta(3.5))          # evaluates the approximation
```

Install:

```bash
pip install treeweave
```

[Python guide](https://diamondinoia.github.io/treeweave/guides/python.html)

### C++

```cpp
#include <treeweave/treeweave.hpp>
#include <cmath>
#include <iostream>

int main() {
    auto zeta = [](double s) {
        double y = 0.0;
        for (int k = 1; k <= 1000; ++k)
            y += std::pow(k, -s);
        return y;
    };

    auto approx = treeweave::fit(zeta, 2.0, 10.0, 1e-10);
    std::cout << approx(3.5) << "\n";
}
```

Use the CMake section below, or download `treeweave-cxx-headers.tar.gz` from
[Releases](https://github.com/DiamonDinoia/treeweave/releases) and compile with
`-std=c++20 -Iinclude`.

[C++ guide](https://diamondinoia.github.io/treeweave/guides/cpp.html)

### C

```c
#include <math.h>
#include <stdio.h>
#include <treeweave.h>

static void kernel(const double *x, double *y, void *ctx) {
    (void)ctx;
    y[0] = 0.0;
    for (int k = 1; k <= 1000; ++k)
        y[0] += pow((double)k, -x[0]);
}

int main(void) {
    double a = 2.0, b = 10.0;
    treeweave_t f = treeweave_fit(kernel, 1, 1, &a, &b, 1e-10, NULL, NULL);
    printf("%g\n", treeweave_eval_1d(f, 3.5));
    treeweave_free(f);
}
```

Install the C ABI tarball directly:

```bash
VER=stable
PLATFORM=linux-x86_64
URL="https://github.com/DiamonDinoia/treeweave/releases/download/${VER}/treeweave-${VER}-${PLATFORM}.tar.gz"
curl -fLO "$URL" || wget "$URL"
tar xzf "treeweave-${VER}-${PLATFORM}.tar.gz"
gcc example.c -Iinclude -Llib -ltreeweave_c -lm -o example
LD_LIBRARY_PATH=lib ./example
```

[C guide](https://diamondinoia.github.io/treeweave/guides/c.html)

### Julia

```julia
using Treeweave

zeta(s) = sum(k -> k^(-s), 1:1000)
approx = fit(zeta, 2.0, 10.0, 1e-10)
println(approx(3.5))
```

`fit` takes the callable first, so a `do` block fits a function written on the
spot: `approx = fit(2.0, 10.0, 1e-10) do s ... end`.

Install:

```julia
using Pkg
Pkg.add(url="https://github.com/DiamonDinoia/treeweave",
        subdir="bindings/julia/Treeweave")
```

[Julia guide](https://diamondinoia.github.io/treeweave/guides/julia.html)

### MATLAB

```matlab
zeta = @(x) sum((1:1000) .^ (-x(1)));
approx = treeweave(zeta, 2, 10, 1e-10);   % dim and out_dim are inferred
disp(approx(3.5));
delete(approx);
```

Install:

With [mip](https://mip.sh/), from the [`mip-org/labs`](https://github.com/mip-org/mip-labs) channel:

```matlab
mip install --channel mip-org/labs treeweave
mip load treeweave
```

Or download the MATLAB bundle directly:

```bash
VER=stable
PLATFORM=linux-x64
URL="https://github.com/DiamonDinoia/treeweave/releases/download/${VER}/treeweave-matlab-${VER}-${PLATFORM}.tar.gz"
curl -fLO "$URL" || wget "$URL"
tar xzf "treeweave-matlab-${VER}-${PLATFORM}.tar.gz"
```

```matlab
addpath('treeweave-matlab-stable-linux-x64')
```

Other platforms use the matching `treeweave-matlab-<version>-<platform>` asset from
[Releases](https://github.com/DiamonDinoia/treeweave/releases).

[MATLAB/Octave guide](https://diamondinoia.github.io/treeweave/guides/matlab.html)

### Octave

```matlab
zeta = @(x) sum((1:1000) .^ (-x(1)));
approx = treeweave(zeta, 2, 10, 1e-10);   % dim and out_dim are inferred
disp(approx(3.5));
delete(approx);
```

Install:

```bash
VER=stable
URL="https://github.com/DiamonDinoia/treeweave/archive/refs/tags/${VER}.tar.gz"
curl -fL "$URL" -o "treeweave-${VER}-source.tar.gz" || wget -O "treeweave-${VER}-source.tar.gz" "$URL"
tar xzf "treeweave-${VER}-source.tar.gz"
cd "treeweave-${VER}"
cmake --preset bindings-octave
cmake --build build/bindings-octave -j
```

[MATLAB/Octave guide](https://diamondinoia.github.io/treeweave/guides/matlab.html)

### Fortran

```fortran
subroutine kernel(x, y, context) bind(C)
    use, intrinsic :: iso_c_binding
    real(c_double), intent(in)  :: x(*)
    real(c_double), intent(out) :: y(*)
    type(c_ptr), value          :: context
    integer :: k
    y(1) = 0.0_c_double
    do k = 1, 1000
        y(1) = y(1) + real(k, c_double) ** (-x(1))
    end do
end subroutine kernel

program example
use, intrinsic :: iso_c_binding
use treeweave

interface
    subroutine kernel(x, y, context) bind(C)
        use, intrinsic :: iso_c_binding
        real(c_double), intent(in)  :: x(*)
        real(c_double), intent(out) :: y(*)
        type(c_ptr), value          :: context
    end subroutine kernel
end interface

real(c_double) :: a(1) = [2.0_c_double], b(1) = [10.0_c_double]
real(c_double) :: x(1) = [3.5_c_double], y(1)
type(c_ptr) :: h

h = treeweave_fit(c_funloc(kernel), 1_c_int, 1_c_int, a, b, &
                  1.0e-10_c_double, c_null_ptr, c_null_ptr)
call treeweave_eval(h, x, y)
print *, y(1)
h = treeweave_free(h)
end program example
```

Install:

```bash
cmake --preset bindings-fortran
cmake --build build/bindings-fortran -j
```

[Fortran guide](https://diamondinoia.github.io/treeweave/guides/fortran.html)

### JavaScript / TypeScript

```js
import { Treeweave } from "@flatironinstitute/treeweave";

function zeta(x) {
    let y = 0.0;
    for (let k = 1; k <= 1000; ++k) y += Math.pow(k, -x[0]);
    return y;
}

const approx = await Treeweave.fit(zeta, 2.0, 10.0, 1e-10);
console.log(approx.eval(3.5));
approx.free();
```

Install:

```bash
npm install @flatironinstitute/treeweave
```

[npm package](https://www.npmjs.com/package/@flatironinstitute/treeweave)

Source builds, release channels, and package details are in the [install guide](https://diamondinoia.github.io/treeweave/install.html).


## CMake

For C++, CPM is enough:

```cmake
CPMAddPackage("gh:DiamonDinoia/treeweave@stable")
add_executable(my_app example.cpp)
target_link_libraries(my_app PRIVATE treeweave::treeweave)
```

With `FetchContent`:

```cmake
include(FetchContent)
FetchContent_Declare(
    treeweave
    GIT_REPOSITORY https://github.com/DiamonDinoia/treeweave.git
    GIT_TAG stable # or any other release tag
)

FetchContent_MakeAvailable(treeweave)
add_executable(my_app example.cpp)
target_link_libraries(my_app PRIVATE treeweave::treeweave)
```

For this repo, prefer `cmake --preset <name>`:

| Preset | What it sets |
| --- | --- |
| `dev-release` | `Release`, `TREEWEAVE_ARCH=native`; examples/tests use top-level defaults. |
| `dev-debug` | `Debug`, `-Og -g`, `TREEWEAVE_ARCH=native`. |
| `bindings-matlab` | `TREEWEAVE_BUILD_MATLAB=ON`; examples/tests off via `lib-release`. |
| `bindings-octave` | Same CMake options as `bindings-matlab`; intended for `mkoctfile`/Octave builds. |
| `bindings-fortran` | `TREEWEAVE_BUILD_FORTRAN=ON`; examples/tests off via `lib-release`. |
| `multiarch` | `TREEWEAVE_C_MULTIARCH=ON`, `TREEWEAVE_ARCH=x86-64`; examples off. |

Advanced options:

| Option | Default | Use |
| --- | --- | --- |
| `TREEWEAVE_BUILD_C_API` | `ON` | Build `treeweave::treeweave_c` and `treeweave::treeweave_c_static`. |
| `TREEWEAVE_C_MULTIARCH` | `OFF` | Build the C ABI with runtime ISA dispatch when supported. |
| `TREEWEAVE_BUILD_EXAMPLES` | top-level only | Build examples. |
| `TREEWEAVE_BUILD_TESTS` | top-level only | Build tests. |
| `TREEWEAVE_BUILD_PYTHON` | `OFF` | Build/register the Python binding test path. |
| `TREEWEAVE_BUILD_JULIA` | `OFF` | Register the Julia binding test path. |
| `TREEWEAVE_BUILD_MATLAB` | `OFF` | Build MATLAB/Octave MEX. |
| `TREEWEAVE_BUILD_FORTRAN` | `OFF` | Build Fortran module, example, and tests. |
| `TREEWEAVE_BUILD_JS` | `OFF` | Build JavaScript/TypeScript native or WASM binding. |

Targets by language:

| Language | CMake target | Notes |
| --- | --- | --- |
| C++ | `treeweave::treeweave` | Header/interface target for `#include <treeweave/treeweave.hpp>`. |
| C | `treeweave::treeweave_c` | Shared C ABI, enabled by `TREEWEAVE_BUILD_C_API=ON`. |
| C | `treeweave::treeweave_c_static` | Static C ABI, enabled by `TREEWEAVE_BUILD_C_API=ON`. |
| Fortran | `treeweave_fortran` | Local target when `TREEWEAVE_BUILD_FORTRAN=ON`. |

## Acknowledgements

treeweave is inspired by [baobzi](https://github.com/flatironinstitute/baobzi) by Robert Blackwell (Flatiron Institute). treeweave rebuilds the fit/eval pipeline on [polyfit](https://github.com/DiamonDinoia/polyfit) and [POET](https://github.com/DiamonDinoia/POET), and adds the multi-language C ABI. See [`NOTICE`](NOTICE).

For numerical background, see Alex Barnett's talk [What everyone should know about function approximation](https://users.flatironinstitute.org/~ahb/talks/fwam25.pdf) (FWAM7, Flatiron Institute, 2025), and Marco Barbone's [Practical HPC NUFFTs](https://diamondinoia.com/talks/practical-hpc-nuffts/index.html#1).

## License

BSD-3-Clause. See [`LICENSE`](LICENSE).
