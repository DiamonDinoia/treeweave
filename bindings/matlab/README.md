# treeweave MATLAB / Octave binding

MATLAB and GNU Octave wrapper for treeweave. One source ([`treeweave.mw`](treeweave.mw)) generates the MEX gateway and `tw_*.m` stubs via mwrap; the default build uses the pre-generated files in [`generated/`](generated/), so mwrap, bison and flex are not required. To regenerate after editing `treeweave.mw`, configure with `-DTREEWEAVE_MATLAB_USE_PREGENERATED=OFF` and build the `treeweave_mw_regen` target.

## Prerequisites

- MATLAB R2019b+ (tested on R2025a) or GNU Octave with `mkoctfile`
- A C++20 toolchain + CMake ≥ 3.25 (the default build needs no mwrap/bison/flex;
  they are only required to regenerate `generated/`, see below)

## Build

The binding is an opt-in CMake option, `TREEWEAVE_BUILD_MATLAB`. CMake builds
the MATLAB MEX when a MATLAB install is on `PATH`, the Octave MEX when
`mkoctfile` is present, and both when it finds both.

```bash
cmake --preset bindings-matlab          # or: cmake -S . -B build -DTREEWEAVE_BUILD_MATLAB=ON ...
cmake --build build/bindings-matlab -j
ctest --test-dir build/bindings-matlab -R matlab_treeweave --output-on-failure
```

For the license-free Octave path specifically, use the `bindings-octave` preset
(identical, but selects the Octave backend when only `mkoctfile` is present):

```bash
cmake --preset bindings-octave
cmake --build build/bindings-octave -j
ctest --test-dir build/bindings-octave -R matlab_treeweave --output-on-failure
```

The MATLAB build links `-static-libstdc++ -static-libgcc` so the `.mexa64` doesn't
depend on a newer GLIBCXX than MATLAB's bundled libstdc++. After editing
`treeweave.mw`, regenerate the checked-in gateway with the `treeweave_mw_regen`
target (configure with `-DTREEWEAVE_MATLAB_USE_PREGENERATED=OFF`) and commit the
result. The only Octave-specific piece is
[`octave_compat/matrix.h`](octave_compat/matrix.h), a
shim that satisfies mwrap's `#include <matrix.h>` (a MATLAB-only header; Octave's
`mex.h` is self-contained).

No prebuilt Octave binary ships, because Octave has no stable MEX ABI across
versions. Build from source as above.

## Usage

```matlab
addpath('/path/to/treeweave/bindings/matlab');          % treeweave.m class
addpath('/path/to/build/bindings/matlab/treeweave_mw'); % generated tw_*.m + MEX

% 1-D scalar fit (dim & out_dim inferred)
f   = @(x) exp(0.5*x(1)) + sin(3*x(1));
obj = treeweave(f, [0], [1], 1e-8);

% Evaluate at N points (N×dim matrix → N×out_dim)
X = linspace(0, 1, 1000)';
Y  = obj.eval(X);                  % or Y = obj(X)
Ys = obj(X, 'sorted', true);       % 1-D ascending fast path

% 2-D → 3-D vector fit (out_dim inferred by probing g; degree auto per CPU)
g    = @(x) [sin(x(1)+x(2)); cos(x(1)-x(2)); x(1)*x(2)];
obj2 = treeweave(g, [-1,-1], [1,1], 1e-6, 'max_memory_mib', 64);

[gx, gy] = meshgrid(linspace(-1,1,50));
Y2 = obj2([gx(:), gy(:)]);              % 2500×3
Yt = obj2([gx(:), gy(:)], 'transposed', true);   % 3×2500 (struct-of-arrays)

fprintf('Memory: %.1f KiB\n', obj2.memory_usage()/1024);
delete(obj2);
```

The `matlab_treeweave` CTest does the `addpath` wiring and runs
`test_treeweave.m` headless under Octave (preferred) or MATLAB.

## Performance

Single-point eval (`obj.eval(scalar)`) carries a fixed per-call overhead.
mwrap's generic R2008OO codegen sets that cost, not treeweave. The codegen
stores the handle as a string in the `mwptr` property, so every call re-parses
it with `sscanf`, and every call allocates and copies a temporary output buffer.
In a hot loop, call the batch API (`obj.eval(X)` with an `N×dim` matrix), which
pays the overhead once for the whole array.

## Files

| File | Purpose |
|------|---------|
| `treeweave.mw` | mwrap source of truth (inline-C wrappers, trampoline, `@function` decls) |
| `treeweave.m` | Thin `classdef` over the generated `tw_*` stubs (handle in `mwptr`) |
| `CMakeLists.txt` | CMake-only build: fetch mwrap (CPM), generate gateway + stubs, compile MEX |
| `octave_compat/matrix.h` | Shim so Octave satisfies mwrap's `<matrix.h>` include |
| `test_treeweave.m` | Smoke/parity tests (portable across MATLAB and Octave) |
| `examples/` | Simple 1-D, 2-D, vector examples |

`generated/` holds the committed `treeweave_mex_gen.cpp` and `tw_*.m`. Never
hand-edit them. Edit `treeweave.mw` and regenerate.
