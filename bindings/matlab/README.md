# treeweave MATLAB / Octave binding

MATLAB and GNU Octave wrapper for treeweave. The
[MATLAB/Octave guide](https://diamondinoia.github.io/treeweave/guides/matlab.html)
carries the API, the options and the worked examples; this page covers the
build only.

One source, [`treeweave.mw`](treeweave.mw), generates the MEX gateway and the
`tw_*.m` stubs with mwrap. The default build uses the pre-generated files in
[`generated/`](generated/), so mwrap, bison and flex are not needed. Never
hand-edit those: edit `treeweave.mw`, configure with
`-DTREEWEAVE_MATLAB_USE_PREGENERATED=OFF`, build the `treeweave_mw_regen`
target and commit the result.

## Prerequisites

- MATLAB R2019b or newer (tested on R2025a), or GNU Octave with `mkoctfile`
- A C++20 toolchain and CMake 3.25 or newer

## Build

`TREEWEAVE_BUILD_MATLAB` is opt-in. CMake builds the MATLAB MEX when a MATLAB
install is on `PATH`, the Octave MEX when `mkoctfile` is present, and both when
it finds both. The license-free Octave path:

<!-- literalinclude: tools/ci/docs-recipes.sh start-after: # BEGIN DOCS_OCTAVE_DEV end-before: # END DOCS_OCTAVE_DEV dedent: 4 -->
```bash
cmake --preset bindings-octave      # or bindings-matlab to build against MATLAB
cmake --build build/bindings-octave -j
ctest --test-dir build/bindings-octave -R matlab_treeweave --output-on-failure
```

`bindings-matlab` is the same preset against MATLAB. The MATLAB build links
`-static-libstdc++ -static-libgcc`, so the `.mexa64` does not need a newer
GLIBCXX than MATLAB's bundled libstdc++.

Octave builds from source only, for the reason the
[MATLAB/Octave guide](https://diamondinoia.github.io/treeweave/guides/matlab.html)
gives. [`octave_compat/matrix.h`](octave_compat/matrix.h) is the one
Octave-specific piece, a shim satisfying mwrap's `#include <matrix.h>`, a
MATLAB-only header.

## Files

| File | Purpose |
|------|---------|
| `treeweave.mw` | mwrap source of truth (inline-C wrappers, trampoline, `@function` decls) |
| `treeweave.m` | Thin `classdef` over the generated `tw_*` stubs (handle in `mwptr`) |
| `CMakeLists.txt` | CMake-only build: generate gateway and stubs, compile the MEX |
| `octave_compat/matrix.h` | Shim so Octave satisfies mwrap's `<matrix.h>` include |
| `test_treeweave.m` | Smoke and parity tests, portable across MATLAB and Octave |
| `examples/` | 1-D, 2-D and vector examples; each is a ctest |
