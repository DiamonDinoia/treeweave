# treeweave

[![CI](https://img.shields.io/github/actions/workflow/status/DiamonDinoia/treeweave/ci.yml?branch=main&label=CI)](https://github.com/DiamonDinoia/treeweave/actions/workflows/ci.yml)
[![codecov](https://codecov.io/gh/DiamonDinoia/treeweave/branch/main/graph/badge.svg)](https://codecov.io/gh/DiamonDinoia/treeweave)
[![docs](https://img.shields.io/badge/docs-treeweave-1f6feb.svg)](https://diamondinoia.github.io/treeweave/)
[![PyPI](https://img.shields.io/pypi/v/treeweave)](https://pypi.org/project/treeweave/)
[![npm](https://img.shields.io/npm/v/%40flatironinstitute%2Ftreeweave)](https://www.npmjs.com/package/@flatironinstitute/treeweave)
[![License: BSD-3-Clause](https://img.shields.io/badge/License-BSD_3--Clause-blue.svg)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)

[![Bindings](https://img.shields.io/github/actions/workflow/status/DiamonDinoia/treeweave/bindings.yml?branch=main&label=Bindings)](https://github.com/DiamonDinoia/treeweave/actions/workflows/bindings.yml)
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

Every snippet below is a marked region of a file CI compiles or runs, and
`scripts/check_docs_code.py` fails the build when one drifts. The language
guides carry the full API, the options and the other install routes.

### Python

<!-- literalinclude: bindings/python/examples/simple_1d.py start-after: # BEGIN DOCS_MINIMAL end-before: # END DOCS_MINIMAL -->
```python
import math
import numpy as np
import treeweave


def func(x):
    return math.exp(x[0])


# Fit exp(x) on [0, 1] syntax is fit(callback, lower_bound, upper_bound, tol).
approx = treeweave.fit(func, 0.0, 1.0, tol=1e-10)
print(approx)  # dtype, dim, out_dim, memory_usage
```

Omit the callable and `fit` becomes a decorator, the `functools.cache` spelling.
Install:

<!-- literalinclude: tools/ci/docs-recipes.sh start-after: # BEGIN DOCS_PIP_PYPI end-before: # END DOCS_PIP_PYPI dedent: 4 -->
```bash
pip install treeweave
```

[Python guide](https://diamondinoia.github.io/treeweave/guides/python.html)

### C++

<!-- literalinclude: examples/quickstart/main.cpp start-after: // BEGIN DOCS_PROGRAM -->
```cpp
#include <treeweave/treeweave.hpp>

#include <cmath>
#include <cstdio>

int main() {
    // Stand-in for an expensive function: a 1000-term series, ~1 us per call.
    auto zeta = [](double s) {
        double y = 0.0;
        for (int k = 1; k <= 1000; ++k)
            y += std::pow(k, -s);
        return y;
    };

    // fit(callback, lower_bound, upper_bound, tolerance)
    const auto f = treeweave::fit(zeta, 2.0, 10.0, 1e-10);

    const double x   = 3.5;
    const double err = std::abs(f(x) - zeta(x)) / std::abs(zeta(x));
    std::printf("f(%g) = %.15g, relative error %.2e\n", x, f(x), err);
    return err < 1e-8 ? 0 : 1;
}
```

`treeweave::treeweave` comes from every CMake route: FetchContent, CPM,
`add_subdirectory` and `find_package` against an installed prefix.

<!-- literalinclude: examples/quickstart/cpp-fetchcontent/CMakeLists.txt start-after: # BEGIN DOCS_PROJECT -->
```cmake
cmake_minimum_required(VERSION 3.25)
project(treeweave_quickstart LANGUAGES CXX)

include(FetchContent)
FetchContent_Declare(
    treeweave
    GIT_REPOSITORY https://github.com/DiamonDinoia/treeweave.git
    GIT_TAG stable # a branch that always points at the newest release
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(treeweave)

add_executable(app "${CMAKE_CURRENT_LIST_DIR}/../main.cpp")
target_link_libraries(app PRIVATE treeweave::treeweave)
```

Without CMake, download `treeweave-cxx-headers.tar.gz` from
[Releases](https://github.com/DiamonDinoia/treeweave/releases) and compile with
`-std=c++20 -Iinclude`.

[C++ guide](https://diamondinoia.github.io/treeweave/guides/cpp.html)

### C

<!-- literalinclude: examples/quickstart/main.c start-after: /* BEGIN DOCS_PROGRAM */ -->
```c
#include <treeweave.h>

#include <math.h>
#include <stdio.h>

/* treeweave calls this while fitting. x holds input_dim values, y holds
 * output_dim; here both are 1. */
static void zeta(const double *x, double *y, void *context) {
    (void)context;
    double sum = 0.0;
    for (int k = 1; k <= 1000; ++k)
        sum += pow((double)k, -x[0]);
    y[0] = sum;
}

int main(void) {
    const double a = 2.0, b = 10.0;

    /* treeweave_fit(callback, input_dim, output_dim, lower, upper, tolerance,
     *               context, options) */
    treeweave_t f = treeweave_fit(zeta, 1, 1, &a, &b, 1e-10, NULL, NULL);
    if (f == NULL) {
        fprintf(stderr, "treeweave_fit failed: %s\n", treeweave_last_error());
        return 1;
    }

    double exact;
    zeta((const double[]){3.5}, &exact, NULL);
    const double approx = treeweave_eval_1d(f, 3.5);
    const double err    = fabs(approx - exact) / fabs(exact);
    printf("f(3.5) = %.15g, relative error %.2e\n", approx, err);

    f = treeweave_free(f);
    return err < 1e-8 ? 0 : 1;
}
```

The installed package adds `treeweave::treeweave_c` and
`treeweave::treeweave_c_static`. To use the release tarball without CMake:

<!-- literalinclude: tools/ci/docs-recipes.sh start-after: # BEGIN DOCS_DOWNLOAD_C_TARBALL end-before: # END DOCS_DOWNLOAD_C_TARBALL dedent: 4 -->
```bash
PLATFORM=linux-x86_64      # or linux-aarch64, macos-arm64, macos-x86_64
URL="https://github.com/DiamonDinoia/treeweave/releases/latest/download/treeweave-${PLATFORM}.tar.gz"
curl -fLO "$URL"
```

<!-- literalinclude: tools/ci/docs-recipes.sh start-after: # BEGIN DOCS_C_TARBALL end-before: # END DOCS_C_TARBALL dedent: 4 -->
```bash
tar xzf "treeweave-${PLATFORM}.tar.gz"   # extracts include/ and lib/ into ./
cc main.c -Iinclude -Llib -ltreeweave_c -lm -o app
LD_LIBRARY_PATH=lib ./app
```

[C guide](https://diamondinoia.github.io/treeweave/guides/c.html)

### Julia

<!-- literalinclude: bindings/julia/Treeweave/examples/example_1d.jl start-after: # BEGIN DOCS_MINIMAL end-before: # END DOCS_MINIMAL -->
```julia
using Treeweave

f = x -> exp(0.5x) + sin(3x)
a, b_val = 0.0, 2.0

println("Fitting f(x) = exp(0.5x) + sin(3x) on [$a, $b_val] ...")
# Fit f(x) on [0, 2] syntax is fit(callback, lower_bound, upper_bound, tolerance).
approx = fit(f, a, b_val, 1e-10)
println("  ", approx)   # show() prints dtype, dim, out_dim and bytes
```

`fit` takes the callable first, so a `do` block fits a function written on the
spot. Install:

<!-- not-run-in-ci: fetches a published release; julia-smoke.yml exercises the same path -->
```julia
using Pkg
Pkg.add(url="https://github.com/DiamonDinoia/treeweave",
        subdir="bindings/julia/Treeweave")
```

[Julia guide](https://diamondinoia.github.io/treeweave/guides/julia.html)

### MATLAB and Octave

<!-- literalinclude: bindings/matlab/examples/example_1d.m start-after: % BEGIN DOCS_MINIMAL end-before: % END DOCS_MINIMAL -->
```matlab
f   = @(x) exp(0.5*x(1)) + sin(3*x(1));
% Fit f(x) on [0, 1] syntax is
% treeweave(callback, lower_bound, upper_bound, tolerance, name/value options).
obj = treeweave(f, 0, 1, 1e-8);

Xtest = linspace(0, 1, 500)';
% Evaluate obj on 500 points and print the maximum error.
Yhat  = obj.eval(Xtest);
Yref  = exp(0.5*Xtest) + sin(3*Xtest);
fprintf('1D max abs error: %.3e\n', max(abs(Yhat - Yref)));
```

MATLAB installs from [mip](https://mip.sh/), from the
[`mip-org/labs`](https://github.com/mip-org/mip-labs) channel:

<!-- not-run-in-ci: installs a published channel package; matlab.yml builds and tests the MEX bundle -->
```matlab
mip install --channel mip-org/labs treeweave
mip load treeweave
```

Octave builds the MEX from source:

<!-- literalinclude: tools/ci/docs-recipes.sh start-after: # BEGIN DOCS_OCTAVE_DEV end-before: # END DOCS_OCTAVE_DEV dedent: 4 -->
```bash
cmake --preset bindings-octave      # or bindings-matlab to build against MATLAB
cmake --build build/bindings-octave -j
ctest --test-dir build/bindings-octave -R matlab_treeweave --output-on-failure
```

[MATLAB/Octave guide](https://diamondinoia.github.io/treeweave/guides/matlab.html)

### Fortran

<!-- literalinclude: bindings/fortran/example.f90 start-after: ! BEGIN DOCS_MINIMAL end-before: ! END DOCS_MINIMAL dedent: 4 -->
```fortran
! Fit exp(x) on [0, 1] syntax is
! treeweave_fit(callback, input_dim, output_dim, lower, upper, tolerance, context, options).
h = treeweave_fit(c_funloc(kernel_exp), 1_c_int, 1_c_int, a, b, tol, &
               c_null_ptr, c_null_ptr)
if (.not. c_associated(h)) then
    write (*, '(2A)') "treeweave_fit failed: ", treeweave_error_message()
    error stop 1
end if
write (*, '(A,I0,A,I0,A,I0,A)') "fit exp(x): input_dim=", treeweave_input_dim(h), &
    " output_dim=", treeweave_output_dim(h), " memory=", treeweave_memory_usage(h), " bytes"
x(1) = 0.5_c_double
! Evaluate h on (0.5) and print the result.
call treeweave_eval(h, x, y)
write (*, '(A,F0.12,A,F0.12)') "exp(0.5) approx=", y(1), " exact=", exp(0.5_c_double)
```

Install:

<!-- literalinclude: tools/ci/docs-recipes.sh start-after: # BEGIN DOCS_FORTRAN_DEV end-before: # END DOCS_FORTRAN_DEV dedent: 4 -->
```bash
cmake --preset bindings-fortran
cmake --build build/bindings-fortran -j
```

[Fortran guide](https://diamondinoia.github.io/treeweave/guides/fortran.html)

### JavaScript and TypeScript

<!-- literalinclude: bindings/js/examples/simple_1d.mjs start-after: // BEGIN DOCS_USAGE end-before: // END DOCS_USAGE -->
```js
// In an installed package this import is "@flatironinstitute/treeweave".
import { Treeweave } from "../dist/index.js";

// Fit sin(x) on [0, 1] syntax is fit(callback, lower_bound, upper_bound, tolerance, options).
const fn = await Treeweave.fit((x) => Math.sin(x[0]), 0.0, 1.0, 1e-10, {
    backend: "native",
});

// Evaluate fn on (0.5) and print the result.
const single = fn.eval(0.5);
console.log(`sin(0.5) approx=${single.toFixed(12)}`);
```

Install:

<!-- literalinclude: tools/ci/docs-recipes.sh start-after: # BEGIN DOCS_NPM end-before: # END DOCS_NPM dedent: 4 -->
```bash
npm install @flatironinstitute/treeweave
```

[JavaScript guide](https://diamondinoia.github.io/treeweave/guides/js.html)

Source builds, release channels and package details are in the
[install guide](https://diamondinoia.github.io/treeweave/install.html).

## Experimental: the guru interface

`<treeweave/guru.hpp>` re-exposes the batch pipeline's stages over caller-owned
buffers, so several fits can share one sort. Signatures and semantics can change
in any release, with no deprecation period. Nothing above needs it. See the
[guru interface guide](https://diamondinoia.github.io/treeweave/guides/guru.html).

## CMake

`cmake --list-presets` prints every preset; the `description` fields in
`CMakePresets.json` are the documentation. `cmake -LH` lists every
`TREEWEAVE_*` option with its default. The
[CMake guide](https://diamondinoia.github.io/treeweave/guides/cmake.html)
covers the targets, the options that matter and the recipe for each route.

## Acknowledgements

treeweave is inspired by [baobzi](https://github.com/flatironinstitute/baobzi) by Robert Blackwell (Flatiron Institute).
Thank you Robert! treeweave rebuilds the fit/eval pipeline on [polyfit](https://github.com/DiamonDinoia/polyfit) and [POET](https://github.com/DiamonDinoia/poet), and adds the multi-language C ABI. See [`NOTICE`](NOTICE).

For numerical background, see Alex Barnett's talk [What everyone should know about function approximation](https://users.flatironinstitute.org/~ahb/talks/fwam25.pdf) (FWAM7, Flatiron Institute, 2025), and Marco Barbone's [Practical HPC NUFFTs](https://diamondinoia.com/talks/practical-hpc-nuffts/index.html#1).

## License

BSD-3-Clause. See [`LICENSE`](LICENSE).
