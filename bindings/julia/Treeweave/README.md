# Treeweave.jl

Julia bindings for [treeweave](https://github.com/DiamonDinoia/treeweave), a
piecewise-polynomial approximator for smooth functions over axis-aligned box
domains in 1-3 dimensions. `fit` returns a callable object; call it with a
point or with a batch.

The API, the options and the worked examples live in the
[Julia guide](https://diamondinoia.github.io/treeweave/guides/julia.html).

## Requirements

Julia 1.9+ and `libtreeweave_c`, the C ABI over the C++ library. The package
resolves the library in this order: the `LIBTREEWEAVE_C` environment variable,
then a sibling `build*/libtreeweave_c.<ext>` for developers, then a prebuilt
download from the matching GitHub Release.

## Install

The package ships through GitHub Releases only, not the General registry:

<!-- not-run-in-ci: fetches a published release; julia-smoke.yml exercises the same path -->
```julia
using Pkg
Pkg.add(url = "https://github.com/DiamonDinoia/treeweave",
        subdir = "bindings/julia/Treeweave")
```

## Quick start

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

## Build from source

The preset builds the sibling C ABI the package resolves against:

<!-- literalinclude: tools/ci/docs-recipes.sh start-after: # BEGIN DOCS_JULIA_DEV end-before: # END DOCS_JULIA_DEV dedent: 4 -->
```bash
cmake --preset bindings-julia
cmake --build build/bindings-julia -j --target treeweave_c
```

<!-- literalinclude: tools/ci/docs-recipes.sh start-after: # BEGIN DOCS_JULIA_TEST end-before: # END DOCS_JULIA_TEST dedent: 4 -->
```bash
ctest --test-dir build/bindings-julia -R julia_treeweave --output-on-failure
```
