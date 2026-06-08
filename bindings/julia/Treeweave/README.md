# Treeweave.jl

Julia bindings for [treeweave](https://github.com/DiamonDinoia/treeweave) — a
piecewise-polynomial function approximator for smooth functions over
axis-aligned box domains in 1–3 dimensions.

## Requirements

- Julia 1.9+
- `libtreeweave_c` (the C ABI layer over the C++ library). For a released
  version it is **downloaded automatically** at build time — no committed
  `Artifacts.toml`, no manual step. `Pkg.add` runs `deps/build.jl`, which
  resolves `libtreeweave_c` in order: the `LIBTREEWEAVE_C` environment variable
  (explicit override) → a sibling CMake `build*/libtreeweave_c.<ext>` (in-repo
  developers) → the prebuilt release binary matching the package version,
  downloaded from the GitHub Release and cached under `deps/usr/`. The result
  is baked into `deps/deps.jl`, which `using Treeweave` reads at load time.

### Installing

Distribution is **GitHub Releases only** (not the General registry):

```julia
using Pkg
Pkg.add(url = "https://github.com/DiamonDinoia/treeweave",
        subdir = "bindings/julia/Treeweave")
```

`Pkg.add` triggers `deps/build.jl`, which downloads the prebuilt `libtreeweave_c`
for your platform from the matching release. Developers working in the repo
instead get the sibling CMake-`build*/` library automatically, or can point
`LIBTREEWEAVE_C` at any build.

## Quick start

```julia
using Pkg
Pkg.activate("path/to/bindings/julia/Treeweave")
Pkg.instantiate()

# ENV["LIBTREEWEAVE_C"] = "/path/to/libtreeweave_c.so"   # optional; auto-discovered
using Treeweave

# 1D scalar (dim & out_dim inferred)
b = fit(x -> exp(0.5x) + sin(3x), 0.0, 1.0, 1e-10)
b(0.5)                       # evaluate a point

# 2D -> 3D vector (out_dim inferred by probing f at the box midpoint)
b2 = fit((x,y) -> (sin(x)*cos(y), x+y, x*y), [0.0,0.0], [1.0,1.0], 1e-8)
b2([0.3, 0.7])               # Vector{Float64} of length 3

# Batch eval (100 points, 2D) — the handle is called directly
X = rand(100, 2)
R  = b2(X)                   # 100×3 Matrix{Float64}
Rt = b2(X; transposed=true)  # 3×100 (struct-of-arrays layout)

# Sorted 1D batch fast path
xs = sort(rand(500))
r  = b(xs; sorted=true)      # Vector{Float64}
```

## API

### `fit(f, a, b, tol; kwargs...) -> TreeweaveFn{T}`

| kwarg      | type / default       | meaning                                         |
|------------|----------------------|-------------------------------------------------|
| `dim`      | `Int`, `length(a)`   | input dimension (1–3)                           |
| `out_dim`  | `Int`, inferred      | output dimension (1–3); probed from `f` when omitted |
| `dtype`    | `Float64`            | element type (`Float64` or `Float32`)           |
| `options`  | `TreeweaveOptions()`    | advanced knobs (depth, memory budget, …)        |

The C ABI auto-selects a register-optimal leaf polynomial degree per detected CPU. Accuracy is controlled entirely by `tol`.

### User-function calling convention

| dim  | call signature          | return (out_dim==1) | return (out_dim>1)                    |
|------|-------------------------|---------------------|---------------------------------------|
| 1    | `f(x::T)`               | scalar              | indexable length-`out_dim`            |
| > 1  | `f(x1::T, x2::T, …)`   | scalar              | indexable length-`out_dim`            |

Coordinates are passed as separate scalar arguments (splatted from an
`NTuple`), so a 2D function is simply `(x, y) -> ...`.

### `TreeweaveOptions`

```julia
TreeweaveOptions(;
    tol_kind               = TREEWEAVE_RELATIVE_MAX,   # tolerance interpretation
    max_depth              = 50,                     # max tree depth
    max_memory_mib         = 4,                      # memory budget in MiB
    allow_max_depth_leaves = 0,                      # bool: allow leaves at max depth
    min_uniform_depth      = 0)                      # force uniform refinement up to this depth
```

### Evaluation — the handle is called directly

| call                         | description                                          |
|------------------------------|------------------------------------------------------|
| `b(x)`                       | point (scalar/`Vector`) or batch (`Vector`/`n×dim` matrix) |
| `b(x; sorted=true)`          | sorted 1-D batch fast path, requires `dim==1`        |
| `b(x; transposed=true)`      | batch returning `out_dim×n`, requires `out_dim>1`    |
| `memory_usage(b)`            | bytes used by the approximation tree                 |
| `print_stats(b)`             | print internal stats to stdout                       |

A point whose length ≠ `dim`, a batch with the wrong column count, `sorted`
with `dim ≠ 1`, or `transposed` with `out_dim == 1` raises an error.

## Running tests

```bash
LIBTREEWEAVE_C=/path/to/libtreeweave_c.so \
  julia --project=bindings/julia/Treeweave -e 'using Pkg; Pkg.test()'
```
