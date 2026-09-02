# treeweave host-language bindings

Thin wrappers over the C ABI (`libtreeweave_c`, [`treeweave.h`](../include/treeweave.h))
for Python, Julia, MATLAB/Octave, Fortran and JavaScript/TypeScript. All reuse the
pre-instantiated C++ templates, so nothing recompiles.

Python, Julia, MATLAB and JavaScript infer `dim` and `out_dim` from the callable.
Call the fitted object directly, with a point or with a batch, and pass the
`sorted` and `transposed` flags to select the fast paths. The Fortran binding
infers nothing: it exposes named procedures and takes `input_dim` and
`output_dim` directly.

| Supported | values |
|-----------|--------|
| input dim  | 1, 2, 3 |
| output dim | 1, 2, 3 (a 1-D vector-valued fit is `dim=1, out_dim>1`) |
| dtype      | `f64` (double), `f32` (float) |

A callback that raises propagates to the caller after the C ABI unwinds. The
out-of-domain rule is the same in every language and each guide states it.

Each language guide carries the API, the options and the worked examples. The
per-binding README below each of them carries only what is specific to that
binding's build.

| Language | Guide | Build from source |
|----------|-------|-------------------|
| Python | [guides/python](https://diamondinoia.github.io/treeweave/guides/python.html) | [`python/README.md`](python/README.md) |
| Julia | [guides/julia](https://diamondinoia.github.io/treeweave/guides/julia.html) | [`julia/Treeweave/README.md`](julia/Treeweave/README.md) |
| MATLAB / Octave | [guides/matlab](https://diamondinoia.github.io/treeweave/guides/matlab.html) | [`matlab/README.md`](matlab/README.md) |
| Fortran | [guides/fortran](https://diamondinoia.github.io/treeweave/guides/fortran.html) | [`fortran/README.md`](fortran/README.md) |
| JavaScript / TypeScript | [guides/js](https://diamondinoia.github.io/treeweave/guides/js.html) | [`js/README.md`](js/README.md) |

One preset per binding builds it and registers its ctests. Python:

<!-- literalinclude: tools/ci/docs-recipes.sh start-after: # BEGIN DOCS_PYTHON_DEV end-before: # END DOCS_PYTHON_DEV dedent: 4 -->
```bash
cmake --preset bindings-python
cmake --build build/bindings-python -j
ctest --test-dir build/bindings-python -R python_treeweave --output-on-failure
```

The other presets are `bindings-julia`, `bindings-matlab`, `bindings-octave`,
`bindings-fortran` and `bindings-js`; each language's guide prints its recipe.
CMake detects a missing toolchain and skips that binding's tests.
