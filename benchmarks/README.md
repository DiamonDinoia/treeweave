# benchmarks

Performance benchmarks for treeweave. Build with `-DTREEWEAVE_BUILD_BENCHMARKS=ON`
(nanobench drivers, hand-rolled timing harness, and the C/C++ zeta reference).
CodSpeed CI driver: `-DTREEWEAVE_BUILD_CODSPEED=ON`. Not built by default.

The four `compare_interpolators` drivers are separate: they grow every method
until it *meets* the requested accuracy, then measure f-evals, coefficient
memory, throughput and achieved error.

- `compare_interpolators.py` compares treeweave against scipy, numpy,
  scikit-learn, chebpy and baobzi. It needs the Python extension on
  `PYTHONPATH`.
- `compare_interpolators.jl` compares it against Interpolations.jl, Dierckx.jl,
  FastChebInterp.jl and DataInterpolations.jl. It needs `Treeweave.jl` in the
  active project and `LIBTREEWEAVE_C` pointing at a built `libtreeweave_c`.
- `compare_interpolators.cpp` compares it against Boost.Math's cardinal cubic
  and quintic B-splines. It builds as the `compare_interpolators_cpp` benchmark
  target when `find_package(Boost 1.75)` succeeds, and counts coefficient memory
  in a replaced global `operator new`, so the number does not depend on the
  allocator or the instruction set.
- `compare_interpolators.m` compares it against Octave's `spline` (not-a-knot
  cubic) and `pchip`. It needs `bindings/matlab` and the built mex directory on
  the Octave path. `--check-docs` also holds Octave's not-a-knot rows against
  the published `scipy CubicSpline` rows, which is the evidence that
  Interpolations.jl's knot counts come from its natural boundary condition.

All four take the same flags. `--rst` regenerates that language's tables in
`docs/guides/performance.rst`; the Python tables live under `In Python`, the
Julia ones under `In Julia`, the C++ ones under `In C++`, the Octave ones under
`In Octave`, and each parser reads only its own section. `--check-docs` holds
those tables to a fresh measurement, and `--self-test` proves every gate can
fail.
