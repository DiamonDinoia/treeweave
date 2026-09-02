# How treeweave works

treeweave turns an expensive function `f(x)` into a cheap lookup. It
precomputes a piecewise polynomial approximation on an adaptive tree. The fit
runs once and costs memory. Every later call is a tree descent followed by a
degree-7 polynomial evaluation.

```
   user calls fn(x), fn(x), fn(x)... a million times.
              │
              ▼
   ┌──────────────────────────────────────┐
   │  fit once:  treeweave::fit(f, a, b)  │  ◄── slow: runs f(x) on Chebyshev
   └──────────────────────────────────────┘      nodes, splits panels until
              │                                  tolerance is met.
              ▼
   ┌──────────────────────────────────────┐
   │  Function fn   (immutable)           │  ◄── tree + per-leaf polynomials.
   └──────────────────────────────────────┘
              │
              ▼
   fn(x) ──► descend tree ──► Horner on leaf coeffs ──► y
   (cheap, no f() calls anymore)
```

## Phase 1, fit: adaptive paneling

Start with the whole domain as one box. Try to fit a degree-D polynomial
(monomial basis, via polyfit, sampled at Chebyshev nodes). If error too large,
split into `2^Dim` children and recurse.
Stop when each leaf passes the tolerance check.

Three standard choices from numerical analysis make this work (see
[Background](#background-and-further-reading)):

- **Chebyshev nodes, not equispaced.** Sampling `f` at the Chebyshev points
  (end-clustered) avoids the Runge phenomenon, the edge oscillations that make
  a high-degree fit on equispaced nodes diverge. On each panel the error then
  converges *exponentially* in the degree for an analytic `f`.
- **Monomial basis at low degree.** treeweave keeps the polynomial in the plain
  monomial basis `Σ cₖ xᵏ`. That is a bad idea at high degree, but it is
  "easy, fast, accurate" at the low degrees treeweave uses (degree ≈ 7, well
  under the `n ≲ 30` regime where the monomial Vandermonde solve stays
  well-behaved). Horner evaluation of a monomial is a tight FMA chain that maps
  directly onto a SIMD register lane (Phase 3).
- **Adaptive panels, not one global fit.** A small enough panel near a rapid
  change or near-singularity is locally accurate, so refining only where `f` is
  hard is far cheaper than one global high-degree interpolant. The split-until-
  `max err ≤ tol` test is exactly the adaptive-paneling idea.

```
   1D example, fitting f on [a, b]:

                           root [a, b]
                         deg-8 fit FAILS
                      split into 2 children
                                │
                  ┌─────────────┴─────────────┐
                  │                           │
                  L                           R
              [a, mid]                    [mid, b]
              fit OK ✓                    fit FAILS
             ── leaf ──                  split again
                                              │
                                    ┌─────────┴─────────┐
                                    │                   │
                                    RL                  RR
                                [mid, q]             [q, b]
                                fit OK ✓            fit OK ✓
                               ── leaf ──          ── leaf ──
```

Each leaf is one box from the original bracket-view paneling: root → split into
`L` (fits) and `R` (fails) → `R` splits into `RL` and `RR` (both fit). Internal
nodes (`root`, `R`) carry a `first_child_idx` pointing at the first of their two
children in the flat `nodes_[]` array; leaves (`L`, `RL`, `RR`) carry a
`poly_eval_id` that indexes into `polyfits_[]`. A descent for a query `x` walks
one path from root to leaf, picking the child with `x` on the correct side of
`center` at each level. In ND, swap binary splits for `2^Dim` children per
internal node: quadtree (4) in 2D, octree (8) in 3D.

In 2D / 3D the same idea applies, but each split is into 4 / 8 children
(quadtree / octree).

```
   uniform quadtree         adaptive: more leaves
                            where f varies fast
   ┌───────┬───────┐        ┌───┬───┬───────────┐
   │       │       │        │   │   │           │
   │   .   │   .   │        │ . │ . │     .     │
   │       │       │        ├───┼───┤           │
   ├───────┼───────┤        │ . │ . │           │
   │       │       │        ├───┴───┼───────────┤
   │   .   │   .   │        │       │           │
   │       │       │        │   .   │     .     │
   └───────┴───────┘        └───────┴───────────┘
```

Phase 1 stores the result in two flat arrays, Struct-of-Arrays for cache
locality:

```
   nodes_[]   : the tree (24/32/40 B per node for 1D/2D/3D)
   ┌────┬────┬────┬─────────┬────┬────┬─────────────────────────┐
   │ N0 │ N1 │ N2 │   ...   │ Ni │... │ each Node carries:      │
   └────┴────┴────┴─────────┴────┴────┴─────────────────────────┘
                                        • center (split point)
                                        • first_child_idx
                                            (or 0xFFFFFFFF if leaf)
                                        • poly_eval_id
                                            → polyfits_[id]

   polyfits_[]   : per-leaf monomial coefficients (packed)
   ┌──────────┬──────────┬──────────┬──────────┐
   │   P0     │   P1     │   ...    │   Pk     │
   └──────────┴──────────┴──────────┴──────────┘
```

Why flat arrays of indices instead of pointers + children? Because the
descent's bottleneck is L1 load latency (Phase 7 closed at 17.1 cycles per
iteration, IPC 0.88, scheduler-queue full 89%). Packing nodes contiguously
and reading only `first_child_idx` per level minimises the dependent-load
chain.

## Phase 2, eval: descent

Given a query point `x`, walk down the tree:

```
   x = 0.37 (1D)

   ┌──────────────────────────────────┐
   │ Node 0:  center = 0.5            │   x < 0.5  → child 0
   │          first_child = 1         │
   └─────────┬────────────────────────┘
             │
             ▼
   ┌──────────────────────────────────┐
   │ Node 1:  center = 0.25           │   x > 0.25 → child 1
   │          first_child = 5         │
   └─────────┬────────────────────────┘
             │
             ▼
   ┌──────────────────────────────────┐
   │ Node 6:  first_child = MAX       │   ◄── leaf!  poly_eval_id = 17
   └─────────┬────────────────────────┘
             │
             ▼
   polyfits_[17].eval(x) → y
```

In ND, the child index takes one bit per axis: one comparison per axis,
4 children per 2D node, 8 per 3D node:

```
   2D quadtree at one level:

   ┌───────┬───────┐         child_idx = (x > cx) << 0
   │  10   │  11   │                   | (y > cy) << 1
   │ (NW)  │ (NE)  │
   ├───────●───────┤         the bullet is (cx, cy), the
   │  00   │  01   │         node's split point.
   │ (SW)  │ (SE)  │
   └───────┴───────┘
```

## Phase 3, leaf evaluation: tensor-product Horner

The leaf carries monomial coefficients `c[k_0, k_1, ..., k_{n-1}]`.
Evaluation collapses one axis at a time with Horner's rule, at a cost of
`K^n − 1` FMAs, where `K = degree + 1`.

```
   1D Horner (K = 8):              2D Horner (K = 8):

   y = c[7]                        for j in 0..7:
   for k in 6..0:                      y_j = (1D Horner over axis 1)
       y = y * x + c[k]
                                   y = y_7
   7 FMAs total, single chain.     for j in 6..0:
                                       y = y * x_0 + y_j

                                   63 FMAs total, two nested chains.
```

For 3D the cost is `8^3 − 1 = 511` FMAs per evaluation. The shipped kernel
batches W = 4 targets per call (AVX2 ymm) so the FMA-port pair runs at peak
when the leaf is hot in L1.

## Phase 4, batched eval: counting sort by leaf

A naive batch runs one independent descent and one independent polyfit call
per point, so the leaf coefficients leave L1 between points. Sorting the
points by leaf fixes that.

```
   xp[N] : caller's points, mixed across leaves:

   ┌────┬────┬────┬────┬────┬────┬────┬────┬────┐
   │ L3 │ L0 │ L3 │ L1 │ L0 │ L3 │ L0 │ L1 │ L3 │   each cell tagged with
   └────┴────┴────┴────┴────┴────┴────┴────┴────┘   its leaf id

   step 1: descend each x → leaf_ids[i]
   step 2: counting sort → permute into packed buffer:

   ┌────┬────┬────┬────┬────┬────┬────┬────┬────┐
   │ L0 │ L0 │ L0 │ L1 │ L1 │ L3 │ L3 │ L3 │ L3 │   one contiguous run per
   └────┴────┴────┴────┴────┴────┴────┴────┴────┘   leaf
    └── L0 run ───┘ └─ L1 ──┘ └──── L3 run ─────┘
           │            │              │
           ▼            ▼              ▼
        polyfit      polyfit        polyfit             ◄── one SIMD batch call
      kernel(L0)   kernel(L1)     kernel(L3)            per leaf, coeffs hot
                                                       in L1.

   step 3: permute outputs back to caller order.
```

Leaf coefficients stay loaded across all points in that leaf's run, so the
vectorized FMA kernel runs at peak. The scratch (the packed input and output
buffers, `counts` — which double as the bin offsets after the scan — and the
inverse permutation `perm_inv`, plus a `leaf_ids` staging buffer on the
descent path) is allocated per call through the caller-supplied allocator and
freed on return, so threaded callers never share state. Callers that cannot
pay that allocation per call drive the same pipeline over caller-owned buffers
through `<treeweave/guru.hpp>` (the guru interface).

## What costs what

Phase-7 attribution at 3D deg=7, N = 10^6:

```
                          % of runtime
   tree descent        │█████████████████████  55 %   ◄── L1 load-use bound,
                       │                                  at silicon floor
   polyfit FMA kernel  │████████               20 %
   input scatter       │███                     8 %
   stl_vector indexing │███                     8 %
   output permute      │██                    6.5 %
                       └────────────────────────────────►
                       0              50               100 %
```

End-to-end ceiling on one P-core: ~7 % of FMA peak (76.8 GFLOPS DP). The
55 % descent is at the load-use latency floor; closing the rest needs
parallel hardware. Phase 8 therefore locks down `operator()` thread-safety,
so callers can chunk and thread externally.

## TL;DR

```
   treeweave  =  k-d tree of monomial polynomials
              + flat-array descent (load-use bound)
              + counting-sort batched leaf evaluation
              + thread-safe immutable Function
```

Fit once, then evaluate from the immutable `Function` as often as needed.

## Degree selection: C ABI vs C++ header API

The C++ header API (`treeweave::fit<Degree>(...)`) requires the leaf degree
as a compile-time template parameter, so each call site can tune or override
it. Supported values are any positive integer; 7 is the default
(matches one AVX2 / AVX-512 register lane).

The C ABI (`treeweave_fit` for double, `treeweavef_fit` for float, in
`treeweave.h`, where the precision lives in the prefix, FINUFFT/FFTW style)
takes no degree argument. It uses degree 7 for every cell, the only spill-free
degree in the widest ones, independent of the CPU. The multi-arch dispatch path,
enabled at build time with `-DTREEWEAVE_C_MULTIARCH=ON`, selects among
compiled-in ISA variants, so the dispatcher picks the best leaf size for the
host CPU: on x86 a compiler-appropriate SIMD ladder, on aarch64 the single
mandatory NEON64 variant, and on RISC-V an `rvv` variant (best-effort,
untested). See the {doc}`Runtime ISA dispatch <guides/dispatch>` guide for the
full family selection and the `TREEWEAVE_FORCE_ARCH` override. Accuracy comes
from `tol` alone: the adaptive tree refines until every leaf meets the
tolerance.

## Background and further reading

The design above implements standard function-approximation practice. One
talk covers the numerical analysis behind it. It explains why Chebyshev nodes,
why the monomial basis is fine at low degree, and how adaptive paneling extends
to several dimensions.

> **Alex Barnett**, *What everyone should know about function approximation in
> one and more dimensions*, FWAM7, Flatiron Institute Center for Computational
> Mathematics, October 2025.
> [slides (PDF)](https://users.flatironinstitute.org/~ahb/talks/fwam25.pdf) ·
> [code demos](https://gist.github.com/ahbarnett)

For the wider HPC context, see Marco Barbone's [Practical HPC NUFFTs](https://diamondinoia.com/talks/practical-hpc-nuffts/index.html#1).

That talk surveys the exact recipe treeweave automates, "break the domain into
panels, use a fixed-degree interpolant on each, and recursively split panels
until the local error meets the user's tolerance". It lists
[baobzi](https://github.com/flatironinstitute/baobzi) (treeweave's predecessor)
alongside `HChebInterp.jl` and `Chebfun` as adaptive-interpolation codes. A few
specific points it makes that justify treeweave's choices:

- Chebyshev nodes defeat the Runge phenomenon and give exponential
  convergence for analytic functions; the convergence rate is governed by the
  size of the Bernstein ellipse in which `f` is analytic (Runge 1901;
  Trefethen, *Approximation Theory and Approximation Practice*).
- The monomial basis is "easy, fast, accurate" for degree `n ≲ 30`, even
  though it is ill-advised at higher degree, exactly the low-degree, many-panel
  regime treeweave operates in (Helsing 2008; Shen & Serkh, *SINUM* 2025).
- Piecewise high-order interpolation on adaptive boxes (a quadtree in 2D, an
  octree in 3D) is the standard way to beat the `nᵈ` curse of dimensionality in
  low dimensions, at the cost of memory, the trade treeweave makes explicit.

### The math, briefly

On each panel treeweave fits an order-`n` polynomial (degree `n − 1`). Working on
the standard interval `[−1, 1]`, it samples `f` at the `n` Chebyshev points

$$
t_j = \cos\!\frac{\pi j}{n - 1}, \qquad j = 0, 1, \dots, n - 1,
$$

which cluster toward the endpoints, and that clustering defeats the Runge
phenomenon. An affine map, exactly treeweave's `midpoint` ± `half_length`,
carries `[−1, 1]` onto a panel `[a, b]`:

$$
x = \frac{a + b}{2} + \frac{b - a}{2}\, t, \qquad t \in [-1, 1].
$$

treeweave stores the interpolant in the monomial basis, with coefficients `cₖ`
that solve the Vandermonde system `V c = f`:

$$
\tilde f(x) = \sum_{k=0}^{n-1} c_k\, x^k,
\qquad
V_{jk} = x_j^{\,k}, \quad f_j = f(x_j).
$$

Rescaling each panel to `[−1, 1]` keeps that Vandermonde solve well-conditioned.
At the low degrees treeweave uses, that is why the monomial form stays
accurate. The approximation error on a panel obeys the classical Chebyshev
bounds:

$$
\lVert f - \tilde f \rVert_\infty =
\begin{cases}
\mathcal{O}(\rho^{-n}) & f \text{ analytic inside the Bernstein ellipse } E_\rho
                          \text{ (exponential)},\\[4pt]
\mathcal{O}(n^{-p})    & f \text{ has } p \text{ continuous derivatives (algebraic)}.
\end{cases}
$$

The adaptive fit (Phase 1) subdivides any panel until its measured
$\lVert f - \tilde f \rVert_\infty \le \varepsilon$ (`tol`), so a near-singularity
gets many small panels while smooth regions stay coarse. Evaluation (Phase 3) is
then a tensor-product Horner sweep costing `Kⁿ − 1` FMAs per point, with
`K = n = degree + 1` values per axis.

With thanks to Alex Barnett for the talk and for the framing of this material.
