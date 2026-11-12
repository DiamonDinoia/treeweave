# Comparison with native interpolators

Most languages already have a polynomial interpolator: numpy `Chebyshev`,
FastChebInterp.jl, Boost.Math, GSL, and `spline` in MATLAB/Octave. This page
compares treeweave with each of them on one problem, at one accuracy. It also
compares treeweave with [baobzi](https://github.com/flatironinstitute/baobzi),
the library that inspired treeweave.

## The problem

The target is the Riemann zeta function as a fixed 160-term sum on `[2, 10]`:

$$\zeta(s) \approx \sum_{k=1}^{160} k^{-s}.$$

A direct sum costs up to 160 `pow` calls per point, about 1 Mevals/s in C++. A
code that calls it a million times per step spends its time in this sum. The
usual fix is to fit an interpolant once and evaluate the interpolant.

## The rules

- Every method fits the same function on the same interval.
- Every method grows until it meets
  `max|p - f| <= 1e-10 * max|f|`, the default criterion of treeweave
  (`RelativeMax`). Polynomial methods grow the degree, `spline` grows the
  number of knots, and Boost and baobzi tighten their own tolerance.
- The benchmark times each method only after the method meets the tolerance.
- All error values come from the same 4096 random points.
- baobzi gets the order (6 to 16) and tolerance that give its fastest batch.
- Each method uses its native call: a scalar call per point for "single",
  and one vectorized call for "batch" and "sorted batch".

## Results

One CI run of `benchmark-showcase.yml` on `ubuntu-24.04`, 2026-11-12
([run 43290841646](https://github.com/DiamonDinoia/treeweave/actions/runs/43290841646)).
The unit is Mevals/s (higher is better). The number in parentheses is the
treeweave throughput divided by the throughput of that method. The
[README](https://github.com/DiamonDinoia/treeweave#benchmarks) has the numbers
of the latest run.

| Language | Method | Size | Single | Batch | Sorted batch |
|---|---|---|---|---|---|
| C | treeweave | tol 1e-10 | 82 | 249 | 1,121 |
| C | GSL `gsl_cheb` | degree 26 | 29 (2.8×) | 29 (8.6×) | 29 (38×) |
| C | baobzi | order 10, tol 1e-8 | 57 (1.4×) | 65 (3.8×) | 174 (6.4×) |
| C++ | treeweave | tol 1e-10 | 279 | 253 | 1,123 |
| C++ | Boost.Math `chebyshev_transform` | degree 27 | 28 (10×) | 28 (9.2×) | 35 (32×) |
| Python | treeweave | tol 1e-10 | 0.7 | 195 | 980 |
| Python | numpy `Chebyshev` | degree 26 | 0.1 (6.6×) | 18 (11×) | 26 (38×) |
| Julia | treeweave | tol 1e-10 | 9.6 | 189 | 544 |
| Julia | FastChebInterp.jl | degree 26 | 38 (0.25×) | 41 (4.7×) | 41 (13×) |
| Octave | treeweave | tol 1e-10 | 2e-3 | 136 | 237 |
| Octave | `spline` + `ppval` | 2048 knots | 4e-3 (0.41×) | 9.3 (15×) | 14 (17×) |

![Batch throughput against native interpolators](https://raw.githubusercontent.com/DiamonDinoia/treeweave/benchmark-results/interp_multi.svg)

## Why treeweave is faster

A global Chebyshev series needs degree 26 or 27 to reach `1e-10` on this interval.
Each evaluation is a Clenshaw recurrence of 26 dependent steps. treeweave
splits the interval into leaves and fits a degree-7 polynomial on each leaf.
Each evaluation is a leaf lookup and 7 fused multiply-adds in Horner form.

The batch call evaluates many points at once with SIMD. The sorted batch call
also skips the internal sort, so consecutive points stay in the same leaf. The
[performance guide](guides/performance.rst) gives the details.

## Where treeweave is not faster

- Single-point calls from Julia and Octave. Each call crosses a C binding,
  and this crossing costs more than the polynomial evaluation. FastChebInterp.jl
  is native Julia code, so the Julia compiler inlines its Clenshaw loop.
  Use the batch call from these languages.
- Error. The baselines stop below the tolerance (1.1e-11 to 5.6e-11 relative
  to `max|f|`, 4.5e-11 for baobzi), and treeweave stops at 9.8e-11 to 9.95e-11.
  All of them meet `1e-10`.
- One run per method after one warm-up, on a shared CI runner. Read the
  ratios to the nearest factor of two. On an Apple M2 Pro the ratios have the
  same order, but the Julia single-point ratio drops to about 0.05×.

## Reproduce

Configure with `TREEWEAVE_BENCH_BASELINES=ON` to build the C and C++
baselines. This option needs Boost, FFTW, GSL and baobzi.

```bash
cmake -S . -B build/bench -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DTREEWEAVE_BUILD_TESTS=OFF -DTREEWEAVE_BUILD_BENCHMARKS=ON \
  -DTREEWEAVE_BENCH_BASELINES=ON -DCMAKE_PREFIX_PATH=/path/to/baobzi-install
cmake --build build/bench --target zeta_bench treeweave_c_zeta_bench
build/bench/zeta_bench
build/bench/treeweave_c_zeta_bench
```

The Python, Julia and Octave benchmarks are in
`bindings/python/benchmarks/zeta_bench.py`,
`bindings/julia/Treeweave/benchmarks/zeta_bench.jl` and
`bindings/matlab/benchmarks/zeta_bench.m`. The workflow
`.github/workflows/benchmark-showcase.yml` runs all of them.
