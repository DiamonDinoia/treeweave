"""
    Treeweave

Julia wrapper for the treeweave piecewise-polynomial function approximator.

# Overview

`Treeweave` wraps the `libtreeweave_c` C ABI. It approximates a smooth function
over a box-shaped domain in 1–3 dimensions, with 1–3 output components.

## User-function calling convention

The function `f` passed to `fit` must accept:
- `dim == 1`: a single `Float64` (or `Float32`) scalar.
- `dim > 1`: `dim` scalar arguments, one per coordinate (called as `f(x, y, ...)`).

It must return:
- `out_dim == 1`: a scalar.
- `out_dim > 1`: an indexable collection of length `out_dim`.

## Example

```julia
using Treeweave

# 1D scalar
b = fit(x -> exp(0.5x) + sin(3x), 0.0, 1.0, 1e-8)
b(0.5)                       # scalar result

# 2D -> 3D vector
b2 = fit((x, y) -> (sin(x)*cos(y), x+y, x*y), [0.0,0.0], [1.0,1.0], 1e-6; out_dim=3)
b2([0.5, 0.3])               # Vector{Float64} of length 3

# Batch eval: the handle is called directly
X = rand(100, 2)             # 100 points, 2 coordinates each
R = b2(X)                    # 100×3 matrix
R = b2(X; transposed=true)   # 3×100 (struct-of-arrays layout)
```

## Library resolution

The loader looks for `libtreeweave_c` in this order:

1. the `LIBTREEWEAVE_C` environment variable, an explicit path that always wins;
2. `deps/deps.jl`, written by `deps/build.jl` when `Pkg.build("Treeweave")` runs.
   For an end user that script downloads the prebuilt `libtreeweave_c` matching
   this package version from the GitHub Release. For a developer it finds a
   sibling CMake `build*/` tree;
3. the loader search path, through `Libdl.find_library`;
4. a sibling CMake `build*/` tree found by walking up from the package dir.

`Pkg.add` runs `Pkg.build`, so an end user gets the prebuilt binary. In-repo
`using Treeweave` and `Pkg.test()` need no env var once the project has been
built once and a `build*/libtreeweave_c.<ext>` exists. No `Artifacts.toml` is
committed, so a release adds no commit here.
"""
module Treeweave

using Libdl

# Library path

# Walk up from `start`, returning the first `build*/libtreeweave_c.<dlext>` found
# (the canonical location of an in-repo CMake build), or `nothing`.
function _find_in_build_tree(start::AbstractString)
    libname = "libtreeweave_c." * Libdl.dlext
    dir = abspath(start)
    while true
        for bd in sort(filter(isdir, readdir(dir; join = true)))
            if startswith(basename(bd), "build")
                cand = joinpath(bd, libname)
                isfile(cand) && return cand
            end
        end
        parent = dirname(dir)
        parent == dir && return nothing   # reached filesystem root
        dir = parent
    end
end

function _resolve_libtreeweave()
    env = get(ENV, "LIBTREEWEAVE_C", "")
    isempty(env) || return env

    # 2. path baked in by deps/build.jl (`Pkg.build("Treeweave")`): the downloaded
    #    prebuilt release binary for an end user, or a sibling build for a dev.
    depsjl = normpath(joinpath(@__DIR__, "..", "deps", "deps.jl"))
    if isfile(depsjl)
        path = include(depsjl)
        path isa AbstractString && isfile(path) && return path
    end

    found = Libdl.find_library(["libtreeweave_c", "treeweave_c"])
    isempty(found) || return found

    # 4. a sibling CMake build tree (covers a fresh checkout never built via Pkg)
    walked = _find_in_build_tree(@__DIR__)
    walked === nothing || return walked

    # Nothing found: return the bare soname so the __init__ warning is clear.
    return "libtreeweave_c." * Libdl.dlext
end

const LIBTREEWEAVE = _resolve_libtreeweave()

# Verify at load time so the error is clear.
function __init__()
    if !isfile(LIBTREEWEAVE) && Libdl.find_library([LIBTREEWEAVE]) == ""
        @warn "Treeweave: could not locate libtreeweave_c ($LIBTREEWEAVE). Set the " *
              "LIBTREEWEAVE_C env var, run Pkg.build(\"Treeweave\"), or build the " *
              "project (a build*/ dir next to the repo root)."
    end
end

# Enumerations (mirrored as Cint constants)

# treeweave_tol_kind_t
const TREEWEAVE_RELATIVE_TAIL  = Cint(0)
const TREEWEAVE_ABSOLUTE_TAIL  = Cint(1)
const TREEWEAVE_RELATIVE_MAX   = Cint(2)
const TREEWEAVE_ABSOLUTE_MAX   = Cint(3)
const TREEWEAVE_RELATIVE_L2    = Cint(4)
const TREEWEAVE_ABSOLUTE_L2    = Cint(5)

# treeweave_dtype_t
const TREEWEAVE_F64 = Cint(0)
const TREEWEAVE_F32 = Cint(1)

# Options struct (maps directly to treeweave_opts)

"""
    TreeweaveOptions(; tol_kind, max_depth, max_memory_mib, allow_max_depth_leaves, min_uniform_depth)

Mirror of the C `treeweave_opts` struct.  All fields are `Cint` so the struct
is blittable for `ccall`.

Defaults match `treeweave_default_opts()` = {RELATIVE_MAX, 50, -1, 0, 0}.
`max_memory_mib = -1` lets the C layer pick automatically (4/8/16 MiB by dim).
"""
struct TreeweaveOptions
    tol_kind              :: Cint   # treeweave_tol_kind_t
    max_depth             :: Cint
    max_memory_mib        :: Cint
    allow_max_depth_leaves:: Cint
    min_uniform_depth     :: Cint
end

function TreeweaveOptions(;
        tol_kind               = TREEWEAVE_RELATIVE_MAX,
        max_depth              = Cint(50),
        max_memory_mib         = Cint(-1),
        allow_max_depth_leaves = Cint(0),
        min_uniform_depth      = Cint(0))
    TreeweaveOptions(Cint(tol_kind), Cint(max_depth), Cint(max_memory_mib),
                  Cint(allow_max_depth_leaves), Cint(min_uniform_depth))
end

# Treeweave handle struct

"""
    TreeweaveFn{T}

Opaque wrapper around a `treeweave_t` handle.  `T` is `Float64` or `Float32`.
Call the handle directly as a function: `b(x)`, `b(x; sorted=true)`, or
`b(x; transposed=true)`.

Do not copy; memory is managed by a finalizer.
"""
mutable struct TreeweaveFn{T}
    ptr     :: Ptr{Cvoid}   # treeweave_t  (opaque struct*)
    dim     :: Int
    out_dim :: Int
end

# Internal helpers

@inline function _last_error()::String
    ptr = ccall((:treeweave_last_error, LIBTREEWEAVE), Cstring, ())
    ptr == C_NULL ? "(no error message)" : unsafe_string(ptr)
end

# Fit callback state, passed to the C layer through `context` (a Ptr{Cvoid}).
# Closures cannot be turned into C function pointers on ARM64 (no runtime
# codegen), so the trampolines below are non-capturing top-level functions that
# recover this struct from `context` via `unsafe_pointer_to_objref`.
mutable struct _FitCtx
    f
    dim     :: Int
    out_dim :: Int
    err                     # first user exception, or `nothing`
end

@inline function _invoke_user(ctx::_FitCtx, xptr::Ptr{CT}, yptr::Ptr{CT}) where {CT}
    nan = CT(NaN)
    if ctx.err !== nothing
        for j in 1:ctx.out_dim; unsafe_store!(yptr, nan, j); end
        return
    end
    try
        f = ctx.f
        result = ctx.dim == 1 ? f(unsafe_load(xptr, 1)) :
                                f(ntuple(i -> unsafe_load(xptr, i), ctx.dim)...)
        if ctx.out_dim == 1
            unsafe_store!(yptr, CT(result), 1)
        else
            for j in 1:ctx.out_dim; unsafe_store!(yptr, CT(result[j]), j); end
        end
    catch e
        ctx.err = e
        for j in 1:ctx.out_dim; unsafe_store!(yptr, nan, j); end
    end
    return
end

function _trampoline64(xptr::Ptr{Cdouble}, yptr::Ptr{Cdouble}, ctxptr::Ptr{Cvoid})
    _invoke_user(unsafe_pointer_to_objref(ctxptr)::_FitCtx, xptr, yptr)
    return nothing
end

function _trampoline32(xptr::Ptr{Cfloat}, yptr::Ptr{Cfloat}, ctxptr::Ptr{Cvoid})
    _invoke_user(unsafe_pointer_to_objref(ctxptr)::_FitCtx, xptr, yptr)
    return nothing
end

"""
    fit(f, a, b, tol; dim=length(a), out_dim=1, dtype=Float64,
        options=TreeweaveOptions())  -> TreeweaveFn{dtype}

Approximate `f` over the axis-aligned box `[a,b]` to tolerance `tol`.

`a` and `b` can be scalars (for dim==1) or `AbstractVector`s.

The user function `f` is called as:
- dim == 1 : `f(x::T)` where `x` is a scalar.
- dim > 1  : `f(x1::T, x2::T, ...)`: one scalar argument per dimension,
             dispatched via `f(coords...)` where `coords` is an `NTuple`.

Return value of `f`:
- out_dim == 1 : a scalar convertible to `T`.
- out_dim > 1  : an indexable object of length `out_dim`.

Supported combinations: `dim` ∈ {1,2,3}, `out_dim` ∈ {1,2,3}.
"""
function fit(f, a, b, tol::Real;
             dim     = (a isa AbstractVector ? length(a) : 1),
             out_dim = nothing,
             dtype   = Float64,
             options :: TreeweaveOptions = TreeweaveOptions())

    T   = (dtype == Float32 ? Float32 : Float64)
    CT  = (T == Float64 ? Cdouble : Cfloat)

    av  = a isa AbstractVector ? CT[CT(x) for x in a] : CT[CT(a)]
    bv  = b isa AbstractVector ? CT[CT(x) for x in b] : CT[CT(b)]
    @assert length(av) == dim && length(bv) == dim

    # Infer out_dim (when not given) by probing f once at the box midpoint:
    # dim 1 takes a scalar, dim > 1 takes splatted coordinates. `length` of a
    # scalar return is 1, of an indexable is its element count.
    if out_dim === nothing
        mid = (av .+ bv) ./ 2
        probe = dim == 1 ? f(mid[1]) : f(mid...)
        out_dim = length(probe)
    end

    # State reaches the trampoline through `context`, not a closure capture, so a
    # non-capturing @cfunction works on ARM64 as well as x86_64 (see _FitCtx).
    ctx = _FitCtx(f, Int(dim), Int(out_dim), nothing)

    if T == Float64
        cfun = @cfunction(_trampoline64, Cvoid, (Ptr{Cdouble}, Ptr{Cdouble}, Ptr{Cvoid}))
        ptr = GC.@preserve ctx av bv begin
            # treeweave_fit(f, input_dim, output_dim, a, b, tol, context, opts).
            ccall((:treeweave_fit, LIBTREEWEAVE), Ptr{Cvoid},
                  (Ptr{Cvoid}, Cint, Cint,
                   Ptr{Cdouble}, Ptr{Cdouble}, Cdouble, Ptr{Cvoid}, Ref{TreeweaveOptions}),
                  cfun, Cint(dim), Cint(out_dim),
                  av, bv, Cdouble(tol), pointer_from_objref(ctx), options)
        end
    else
        cfun = @cfunction(_trampoline32, Cvoid, (Ptr{Cfloat}, Ptr{Cfloat}, Ptr{Cvoid}))
        ptr = GC.@preserve ctx av bv begin
            # treeweavef_fit(f, input_dim, output_dim, a, b, tol, context, opts).
            ccall((:treeweavef_fit, LIBTREEWEAVE), Ptr{Cvoid},
                  (Ptr{Cvoid}, Cint, Cint,
                   Ptr{Cfloat}, Ptr{Cfloat}, Cdouble, Ptr{Cvoid}, Ref{TreeweaveOptions}),
                  cfun, Cint(dim), Cint(out_dim),
                  av, bv, Cdouble(tol), pointer_from_objref(ctx), options)
        end
    end

    # If the user function threw, re-raise that (more informative).
    if ctx.err !== nothing
        throw(ctx.err)
    end

    if ptr == C_NULL
        error("treeweave fit failed: " * _last_error())
    end

    handle = TreeweaveFn{T}(ptr, dim, out_dim)
    finalizer(handle) do h
        if h.ptr != C_NULL
            h.ptr = ccall((:treeweave_free, LIBTREEWEAVE), Ptr{Cvoid}, (Ptr{Cvoid},), h.ptr)
        end
    end
    return handle
end

# Evaluation: the fitted handle is *called*

"""
    (b::TreeweaveFn)(x; sorted=false, transposed=false)

Evaluate the fitted approximation. The handle is called directly; there are
no named eval methods.

- **Point**: a scalar (`dim == 1`) or a length-`dim` `AbstractVector` returns
  a scalar (`out_dim == 1`) or a length-`out_dim` `Vector`.
- **Batch**: an `n × dim` `AbstractMatrix` (or, for `dim == 1`, any
  `AbstractVector`) returns a length-`n` `Vector` (`out_dim == 1`) or an
  `n × out_dim` `Matrix`.

Keyword flags (batch only):
- `sorted=true`: 1-D ascending fast path; requires `dim == 1` and that the
  caller has sorted `x` (`x[i] ≤ x[i+1]`).
- `transposed=true`: return an `out_dim × n` `Matrix` (struct-of-arrays
  layout) instead of `n × out_dim`; requires `out_dim > 1`.
- `out=`: a pre-allocated `Vector{T}` of length `n` to write into (in-place,
  zero-copy), returned as-is. Batch/sorted only, and scalar-output
  (`out_dim == 1`) fits only.

A point whose length ≠ `dim`, a batch with the wrong column count, `sorted`
with `dim ≠ 1`, or `transposed` with `out_dim == 1` raises an error rather
than silently mis-shaping the result.
"""
function (b::TreeweaveFn{T})(x; sorted::Bool = false, transposed::Bool = false, out = nothing) where {T}
    if sorted && transposed
        error("`sorted` and `transposed` are mutually exclusive")
    end
    if out !== nothing && transposed
        error("out= is not supported with transposed=true")
    end

    if sorted
        b.dim == 1 || error("sorted=true requires dim == 1 (got dim=$(b.dim))")
        x isa AbstractVector || error("sorted=true expects a vector of 1-D points")
        return _eval_sorted(b, x; out = out)
    end

    if transposed
        b.out_dim > 1 || error("transposed=true requires out_dim > 1")
        return _eval_transposed(b, x)
    end

    # Point vs batch: a matrix is always a batch; for dim==1 a vector is a
    # batch and a scalar is a point; for dim>1 a length-dim vector is a point.
    if x isa AbstractMatrix
        return _eval_batch(b, x; out = out)
    elseif b.dim == 1
        if x isa AbstractVector
            return _eval_batch(b, x; out = out)
        else
            out === nothing || error("out= requires a batch input, not a single point")
            return _eval_point(b, x)
        end
    else
        x isa AbstractVector ||
            error("for dim > 1, pass a length-$(b.dim) vector (point) or an n×$(b.dim) matrix (batch)")
        length(x) == b.dim ||
            error("point has length $(length(x)) but dim == $(b.dim)")
        out === nothing || error("out= requires a batch input, not a single point")
        return _eval_point(b, x)
    end
end

# Internal eval kernels. ccall's symbol must be a literal, so each branches on
# the compile-time type parameter T to pick the treeweave_/treeweavef_ twin.

@inline function _eval_point(b::TreeweaveFn{T}, x) where {T}
    xv = _coerce_x(x, b.dim, T)
    y  = Vector{T}(undef, b.out_dim)
    GC.@preserve xv y begin
        if T === Float64
            ccall((:treeweave_eval, LIBTREEWEAVE), Cvoid,
                  (Ptr{Cvoid}, Ptr{Cdouble}, Ptr{Cdouble}), b.ptr, xv, y)
        else
            ccall((:treeweavef_eval, LIBTREEWEAVE), Cvoid,
                  (Ptr{Cvoid}, Ptr{Cfloat}, Ptr{Cfloat}), b.ptr, xv, y)
        end
    end
    return b.out_dim == 1 ? y[1] : y
end

# Allocate the flat result buffer for a batch/sorted eval, or validate and reuse
# a caller-supplied `out` for in-place (zero-copy) evaluation. In-place out= is
# only meaningful for scalar-output fits, where the C result buffer *is* the
# returned vector (no point-major -> column-major matrix reshape).
@inline function _result_buffer(b::TreeweaveFn{T}, n::Int, out) where {T}
    out === nothing && return Vector{T}(undef, n * b.out_dim)
    b.out_dim == 1 || error("out= is only supported for scalar-output (out_dim == 1) fits")
    out isa Vector{T} || error("out= must be a Vector{$T}")
    length(out) == n || error("out= has length $(length(out)) but the batch has $n points")
    return out
end

@inline function _eval_batch(b::TreeweaveFn{T}, X; out=nothing) where {T}
    xbuf, n = _pack_x(X, b.dim, T)
    res = _result_buffer(b, n, out)
    GC.@preserve xbuf res begin
        if T === Float64
            ccall((:treeweave_batch, LIBTREEWEAVE), Cvoid,
                  (Ptr{Cvoid}, Ptr{Cdouble}, Ptr{Cdouble}, Csize_t), b.ptr, xbuf, res, Csize_t(n))
        else
            ccall((:treeweavef_batch, LIBTREEWEAVE), Cvoid,
                  (Ptr{Cvoid}, Ptr{Cfloat}, Ptr{Cfloat}, Csize_t), b.ptr, xbuf, res, Csize_t(n))
        end
    end
    return out === nothing ? _unpack_y(res, n, b.out_dim) : out
end

@inline function _eval_sorted(b::TreeweaveFn{T}, x::AbstractVector; out=nothing) where {T}
    n   = length(x)
    xv  = x isa Vector{T} ? x : Vector{T}(x)   # zero-copy when already Vector{T}
    res = _result_buffer(b, n, out)
    GC.@preserve xv res begin
        if T === Float64
            ccall((:treeweave_sorted, LIBTREEWEAVE), Cvoid,
                  (Ptr{Cvoid}, Ptr{Cdouble}, Ptr{Cdouble}, Csize_t), b.ptr, xv, res, Csize_t(n))
        else
            ccall((:treeweavef_sorted, LIBTREEWEAVE), Cvoid,
                  (Ptr{Cvoid}, Ptr{Cfloat}, Ptr{Cfloat}, Csize_t), b.ptr, xv, res, Csize_t(n))
        end
    end
    return out === nothing ? _unpack_y(res, n, b.out_dim) : out
end

# Struct-of-arrays eval. treeweave_transposed wants `out_dim` contiguous buffers
# of `n` each; a column of an n×out_dim column-major matrix is exactly that, so
# point soa[d] at column d, then return the transpose (out_dim × n).
@inline function _eval_transposed(b::TreeweaveFn{T}, X) where {T}
    xbuf, n = _pack_x(X, b.dim, T)
    buf  = Matrix{T}(undef, n, b.out_dim)
    ptrs = Ptr{T}[pointer(buf, (d - 1) * n + 1) for d in 1:b.out_dim]
    GC.@preserve xbuf buf ptrs begin
        if T === Float64
            ccall((:treeweave_transposed, LIBTREEWEAVE), Cvoid,
                  (Ptr{Cvoid}, Ptr{Cdouble}, Ptr{Ptr{Cdouble}}, Csize_t), b.ptr, xbuf, ptrs, Csize_t(n))
        else
            ccall((:treeweavef_transposed, LIBTREEWEAVE), Cvoid,
                  (Ptr{Cvoid}, Ptr{Cfloat}, Ptr{Ptr{Cfloat}}, Csize_t), b.ptr, xbuf, ptrs, Csize_t(n))
        end
    end
    return permutedims(buf)   # out_dim × n
end

# Introspection

"""
    memory_usage(b::TreeweaveFn) -> Int

Return the number of bytes occupied by the approximation tree.
"""
memory_usage(b::TreeweaveFn) =
    Int(ccall((:treeweave_memory_usage, LIBTREEWEAVE), Csize_t, (Ptr{Cvoid},), b.ptr))

"""
    print_stats(b::TreeweaveFn)

Print internal statistics via the C library (to stdout).
"""
print_stats(b::TreeweaveFn) =
    ccall((:treeweave_print_stats, LIBTREEWEAVE), Cvoid, (Ptr{Cvoid},), b.ptr)

function Base.show(io::IO, b::TreeweaveFn{T}) where T
    bytes = memory_usage(b)
    print(io, "TreeweaveFn{$T}(dim=$(b.dim), out_dim=$(b.out_dim), $(bytes) bytes)")
end

# Internal utility helpers

function _coerce_x(x, dim::Int, ::Type{T}) where T
    if dim == 1
        return T[T(x isa AbstractVector ? x[1] : x)]
    else
        xv = x isa AbstractVector ? x : (x,)
        length(xv) == dim || error("Expected $dim coordinates, got $(length(xv))")
        return T[T(c) for c in xv]
    end
end

# Pack an n-point array into a point-major C buffer of type T.
# Returns (buf::Vector{T}, n::Int).
function _pack_x(X, dim::Int, ::Type{T}) where T
    if X isa AbstractVector && dim == 1
        n = length(X)
        # Zero-copy when X is already a dense Vector{T}; otherwise convert.
        return (X isa Vector{T} ? X : Vector{T}(X)), n
    elseif X isa AbstractMatrix
        # X is n × dim in Julia (column-major). The C ABI needs a point-major
        # flat buffer: [p0_x0, p0_x1, ..., p0_x{dim-1}, p1_x0, ...].
        # X[i,j] is the j-th coordinate of the i-th point.
        n, d = size(X)
        d == dim || error("Matrix has $d columns but dim=$dim")
        buf = Vector{T}(undef, n * dim)
        for i in 1:n
            for j in 1:dim
                buf[(i-1)*dim + j] = T(X[i, j])
            end
        end
        return buf, n
    else
        error("X must be a Vector (dim==1) or an n×dim Matrix")
    end
end

# Reshape the flat AoS result buffer.
function _unpack_y(res::Vector{T}, n::Int, out_dim::Int) where T
    if out_dim == 1
        return res   # already length-n
    else
        # res is [p0_y0..p0_y{out-1}, p1_y0...], point-major.
        # Return an n × out_dim matrix (Julia column-major).
        M = Matrix{T}(undef, n, out_dim)
        for i in 1:n
            for j in 1:out_dim
                M[i, j] = res[(i-1)*out_dim + j]
            end
        end
        return M
    end
end

# Exports

export TreeweaveOptions, TreeweaveFn, fit, memory_usage, print_stats
export TREEWEAVE_RELATIVE_TAIL, TREEWEAVE_ABSOLUTE_TAIL, TREEWEAVE_RELATIVE_MAX
export TREEWEAVE_ABSOLUTE_MAX, TREEWEAVE_RELATIVE_L2, TREEWEAVE_ABSOLUTE_L2
export TREEWEAVE_F64, TREEWEAVE_F32

end # module Treeweave
