# Performance and Design Rationale Notes

This file holds the full text of rationale comments that were trimmed to
1-2 line summaries in the source. Organised by file and symbol.

---

## polytree.hpp — leaf-table construction

The per-subtree quantize-to-leaf table is built when `input_dim * max_depth_ <= 16`
(at most 64 K entries, 256 KiB). At depth >~16 the table is skipped and eval falls
back to descent.

Size rationale: 64 K uint32 entries = 256 KiB per subtree. Sapphire Rapids has 2 MiB
L2/core, Zen4 has 1 MiB. The table is L2-resident on modern x86 cores. The tradeoff is
worth the L1d eviction because the descent path it replaces costs one `vucomisd + ja`
per level (IPC ~1.9, branch-miss 6-10%) vs one `vcvttsd2usi + load` for the table.
Measured ~3x worse at depth 15-16 on bench_pack_scatter.

---

## polytree.hpp — quantize_one

Full original docstring:

Compute the leaf id for a single point via the leaf-table fast path. 1D uses one scalar
quantize + unsigned wrap OOD; ND iterates per axis and bails on the first
out-of-range axis. Caller must have verified `has_leaf_table()` — this is the shared
kernel used by `find_leaf_id_with_ood` (scalar API) and the per-lane body of
`find_leaf_ids_batch` (1D SIMD batch path), as well as the scatter recompute in
`Function::eval_batch_tile`.

The signed `vcvttsd2si` + unsigned compare folds the OOD test into the table-index
quantize: an out-of-range double yields INT64_MIN (x86-64 indefinite-integer), which
compares above `mask` as uint64. Saves one cmp per axis vs. an explicit in-domain
pre-check. The early `return ood_id;` inside the per-axis loop keeps the OOD path off
the hot fall-through — flag-and-post-check formulations regressed 1D batch by 4-7%
(paired-median, n=24).

Out-of-domain gate: the positive-logic test `!(x >= lower_ && x <= upper_)` returns
`ood_id` for everything outside `[lower_, upper_]` in a single per-axis check. It
matches `Function::operator()` exactly, so the batch and scalar APIs agree
point-for-point: OOD-low, finite `x > upper_`, NaN and ±Inf all map to `ood_id`
(NaN/±Inf fail both comparisons — every NaN compare is false). This replaces the old
`isfinite` + unsigned-wrap pair, and because the gate proves `x` finite and in range
before the cast, `static_cast<int64_t>` is never applied to a non-finite value (no UB,
no aarch64 `fcvtzs(NaN) = 0`).

Closed upper endpoint: `x == upper_` passes the gate and quantizes to `mask + 1`; the
`min(q, mask)` clamp routes it to the last leaf (the boundary value, finite). Finite
`x > upper_` is rejected by the gate *before* the clamp, so it returns `ood_id` rather
than extrapolating — the one case the batch path used to disagree with the scalar API
on. `std::floor` matches the packed/sweep/tail truncation in `for_each_leaf_id_batch`
so a point classifies the same whichever runs.

---

## polytree.hpp — narrow_trunc_to_u32

Full original docstring:

Packed truncate of `W = batch<double>::size` doubles to `W` uint32 lanes (the
leaf-table index width). xsimd has no lane-matched double->int32 cast — the result is
half the source width (zmm->ymm at 8 lanes, ymm->xmm at 4) — so we issue
`vcvttpd2dq` on the batch register directly and wrap the half-width result in a uint32
batch whose lane count matches the double batch, ready for a `vpgatherdd` of
`leaf_table_`. `vcvttpd2dq` (7c lat / 1 CPI on SKX, one ymm result) beats the int64
`vcvttpd2qq` (two zmm) by skipping the int64->int32 lane juggling the index would
otherwise need.

x86 AVX2 / AVX-512 only — the only callers gate on `kFastGatherF64`, which is false
everywhere else, so the `#else` stub below is never instantiated. It exists purely so
the name stays *declared* on ARM/SSE2 targets: the discarded `if constexpr
(kFastGatherF64)` branch still mentions `narrow_trunc_to_u32` in a `decltype`, and
some non-x86 compilers reject an undeclared name there even though it never compiles.

TODO(xsimd): upstream as a lane-narrowing `fast_cast<int32_t>(double)` so this
reaches-into-the-register helper can go away.

---

## polytree.hpp — gather_leaf_ids

Full original docstring:

f32 gather kernel shared by `for_each_leaf_id_batch` and `leaf_ids_batch`: floor ->
truncate -> clamp (`vpminud`) -> gather (`vpgatherdd` on AVX2+, xsimd scalar-loop
emulation below that) -> OOD-select, matching `quantize_one` lane-for-lane. Index and
value are both 32-bit so the gather lanes match. `*_v` are loop-invariant broadcasts
hoisted by the caller. Only instantiated for `float`.

---

## polytree.hpp — for_each_leaf_id_batch (doc)

Full original docstring:

1D batch leaf-id stream — invokes `on_id(i, id)` for every point in `[xp, xp+n)`,
amortising the quantize across `xsimd::batch<value_type>::size` lanes per iteration.
Caller picks the per-point side effect: `Function::eval_batch_tile` uses this twice,
first to bump `counts[id]` (the histogram), then to scatter `xp_packed[counts[id]++] =
xp[i]` (no `leaf_ids[]` materialisation in between — see FINUFFT bin-sort recon,
spread.hpp:421).

Pipeline per SIMD chunk:
  1. SIMD compute: `q = (x - lo) * inv_span_bins` for `simd_size` lanes (one
     `load_unaligned`, one sub, one mul). Truncating conversion to int produces
     INT64_MIN on x86 for OOD doubles, which compares above `mask` as uint64 — same
     single-cmp OOD trick used by the scalar variant.
  2. Scalar lane sweep: per-lane table lookup `leaf_table_[q]` (or `ood_id` on wrap)
     and the user-supplied `on_id(i+j, id)` callback. Vectorised gather/scatter to
     the histogram would lose to bank-conflict serialization on shared counters
     (FINUFFT spread.hpp:454-457 documents the same finding).

The trailing `n % simd_size` points are dispatched through the scalar quantize for
clarity; the loop is short enough that the branch-predictor handles it without
measurable cost.

---

## polytree.hpp — for_each_leaf_id_batch (truncation strategy by value type)

Original inline comment block:

Truncating-conversion strategy, by value type (leaf ids are <= 2^16, so 32 bits is
always ample for the index):

  * float  -> int32 via `vcvttps2dq` (xsimd `fast_cast(float,int32)`, lane-matched on
    SSE2/AVX/AVX-512 — xsimd_sse2.hpp:739 etc.). This is the *only* lane-matched
    packed float->int truncate, and it replaces the per-lane `vcvttss2si` sweep that
    made the 16-wide AVX-512 float batch the costliest quantize cell measured
    (bench/binsort_phase0.md). OOD lanes are decided by the float domain mask below,
    so the packed cast only has to land in-range for the *kept* lanes — the clamp in
    `resolve` guarantees that.

  * double -> int32 via `vcvttpd2dq` (`narrow_trunc_to_u32`, a direct intrinsic since
    xsimd has no lane-narrowing double->int cast), then the SAME `vpgatherdd` the
    float path uses. An asm profile put the old int64 quantize (`vcvttpd2qq`, lat 4c)
    + dependent scalar `leaf_table_[q]` loads at ~43% of the 1D kernel; narrowing to
    int32 (lat 1c, no int64 lane juggling) lets W ids gather in one shot, exactly the
    win the f32 path already measured. AVX2 / AVX-512 only; SSE2 and non-x86 double
    keep the per-lane `vcvttsd2si` sweep (no hardware gather to feed).

---

## polytree.hpp — for_each_leaf_id_batch (f32 kFastInt32 block)

Original comment:

f32: gather the leaf table for W lanes in one shot, replacing the W dependent scalar
`leaf_table_[q]` loads the audit measured as the bottleneck of the scalar resolve
sweep. Lowers to `vpgatherdd` on AVX2+; xsimd emulates with a scalar load loop below
that, which still beats the old sweep (measured ~1.7x at SSE2, ~2-4x with a hardware
gather) — so no ISA gate, let xsimd pick. Only the consumer stays scalar: `on_id` may
carry a cross-lane RMW (e.g. the histogram `++counts[id]`) a vector scatter can't
serialize. `gather_leaf_ids` does the floor/clamp/OOD-select in SIMD, matching
`quantize_one`.

---

## polytree.hpp — for_each_leaf_id_batch (f64 kFastGatherF64 block)

Original comment:

double on AVX2/AVX-512: narrow double->int32 (`vcvttpd2dq`, 7c lat / 1 CPI on SKX,
one ymm) over the int64 `vcvttpd2qq` (two zmm). The profile fingered the *cast* as the
bottleneck (~43% of the kernel), feeding a dependent scalar `leaf_table_[q]` load — so
narrow the cast only and KEEP the scalar loads. A `vpgatherdd` over the narrowed
indices also classifies right but loses from ~256 leaves up (one gather uop waits on
every lane's cache line; the table spills L1) — measured -9% at d8 to -24% at d16, vs
+8% at d4/d6. The narrowed index batch is u32 (`vpminud` clamp) so the scalar loop
indexes the table with a zero-extended 32-bit lane.

---

## polytree.hpp — for_each_leaf_id_batch (resolve lambda)

Original comment block:

Per-lane resolve: the caller passes the truncated quantize `qi` and the per-lane
out-of-domain bit `lane_ood`, computed in SIMD from the same positive-logic domain test
`quantize_one` uses. `lane_ood` alone decides OOD — it already covers OOD-low, finite
OOD-high, NaN and ±Inf — so this is a branchless select that matches the scalar API
point-for-point. `qi` is clamped to `[0, mask]` only to keep the discarded lanes'
table read in bounds (an OOD-low `qi < 0`, or aarch64 `fcvtzs(NaN) = 0` /
`fcvtzs(+Inf) = INT_MAX`); the same clamp routes the closed upper endpoint `x ==
upper_` (quantizing to `mask + 1`) to the last leaf.

One unsigned min keeps the index in [0, mask] for every lane: a negative `qi` (OOD-low,
or the x86 indefinite INT_MIN) wraps to a huge unsigned and clamps to `mask`. OOD lanes
are discarded by the `lane_ood` select, so this index only has to stay in bounds, not be
meaningful — hence a bare clamp with no sign test suffices.

---

## polytree.hpp — leaf_ids_batch

Full original docstring:

Vectorized leaf-id stream into `out[i]` (dependency-free consumer, unlike the
histogram/scatter callbacks). On the f32 path this is the fully packed gather pipeline
with a vector store — no per-lane callback, the audit's ~4x cell. The double path falls
back to the generic callback.

---

## polytree.hpp — get_node_index

Full original docstring:

Descent hot loop. The per-subtree (lo, hi) bounds live in registers and
`mid = 0.5 * (lo + hi)` is recomputed each level — the node carries no `center`, so
descent is not load-bound. `input_dim` is constexpr so the ND per-axis compare unrolls.

`mid` is computed differently from the fit-time `box.center` (chained halving), so
bit-exactness at boundary points is not guaranteed; tests assert relative tolerance.

---

## function.hpp — Scratch (internal batch scratch)

Full original docstring on `Scratch`:

Internal scratch for the unsorted batch path. Constructed stack-local inside each batch
call (the public API does not expose this type) and parametrised on the caller's
allocator so arena / pool / pinned-memory allocators reuse storage without treeweave
having to hold any state across calls.

Leaf ids are not materialised — they are recomputed during scatter from the same SIMD
quantize that drove the histogram (FINUFFT bin-sort recon, spread.hpp:421). One
quantize per W points is cheaper than the u16/u32 read/write stream the materialised
`leaf_ids[]` array would push through L1d.

`counts` doubles as the histogram, the exclusive-scan output, and the scatter cursor —
after scatter, `counts[k]` is the one-past-end of leaf k's packed slice (Reinecke's
trick), removing the need for a separate `offsets` array.

---

## function.hpp — leaf_id

Full original docstring:

Leaf id the bin sort assigns to `x`: the index into the panel store (`< num_leaves()`)
whose polynomial evaluates `x`, or the out-of-domain sentinel `num_leaves()`. Resolves
through the leaf-table fast path when live, else tree descent. This is the scalar twin
of one `leaf_ids` lane and shares its quantize/OOD-wrap semantics exactly — including
flagging NaN/±Inf as OOD (the leaf-table wrap test catches them, where the bare
`operator()(x)` domain pre-check would let a NaN through to a panel and evaluate to
NaN). It is the parity oracle the quantize tests assert the vectorized `leaf_ids`
stream against.

---

## function.hpp — leaf_ids

Full original docstring:

Batch leaf-id assignment: write each of the `n` points' `leaf_id` into `out`. Streams
through the same vectorized quantize the batch evaluator uses (1D leaf-table fast path
-> `for_each_leaf_id_batch`; otherwise the per-point resolve), so this is the binning
stage of `operator()(xp,res,n)` exposed on its own. `xp` is AoS (`input_dim` coords
per point).

---

## function.hpp — operator()(xp, res, n) pipeline stages

The full 5-stage pipeline description from the batch `operator()` doc:

  1. **Leaf-id traversal.** For each input point, look up the owning leaf index (the
     `polyfits_` slot that holds its Horner coefficients). When the Function has a
     single subtree with a precomputed leaf-table the lookup is a quantize + u32 load
     and folds OOD detection into the same unsigned wrap test. Otherwise, we descend
     the tree per point. Out-of-domain points are tagged with the sentinel id `n_leaves`
     and counted in their own bucket.
  2. **Counting sort + in-place exclusive scan.** Histogram leaf populations into
     `counts[0..n_leaves]`, then exclusive-scan `counts` in place. After scatter (next
     stage) consumes `counts` as a cursor, `counts[k]` equals the one-past-end of leaf
     k's slice — enough to recover `(off, cnt)` for the per-leaf dispatch by walking
     ids with a running `prev_end` (Reinecke's trick).
  3. **Scatter to packed layout.** Walk points in input order; for each, append its
     coordinates to `xp_packed` at its leaf's cursor (`counts[id]++`) and record the
     inverse mapping in `perm[dst] = i`.
  4. **Per-leaf SIMD batch eval.** For each non-empty leaf, hand its contiguous slice
     of `xp_packed` to polyfit's SIMD batch kernel once and write into the same slice
     of `out_packed`. The OOD bucket is filled with NaN instead of evaluated.
  5. **Permute back to caller order.** For each `dst`, copy `out_packed[dst]` into
     `res[perm[dst] * output_dim]`. The store address is random in `res`, so we
     prefetch ahead by `LOOKAHEAD` to hide the RFO latency that otherwise dominates 1D
     throughput.

Tiny batches (`n_trg < kSortThreshold`) skip stages 2-5 and just loop
point-at-a-time. Large batches are tiled (`kDefaultTileK`, lifted by an adaptive floor
for high-leaf-count Functions) so the packed buffers fit in L1d/L2.

For 1D, callers who can promise sortedness should prefer `sorted(xp, res, n)` — it
skips stages 2, 3, 5 entirely and runs ~3-4x faster.

---

## function.hpp — partition_into_leaves

Full original docstring:

Stages 1-3 of the unsorted tile pipeline, shared (byte-identical) between the AoS and
SoA tile bodies: zero `counts`, histogram leaf populations, exclusive-scan into slice
starts, then scatter each point's coords into `xp_packed` while recording the inverse
permutation in `perm_inv`. On return `counts[k]` is leaf k's one-past-end cursor
(Reinecke) — enough to recover (off, cnt) per leaf in the dispatch walk.

Leaf-id materialisation is path-dependent. The 1D leaf-table fast path amortises the
quantize over an xsimd batch (`for_each_leaf_id_batch`, FINUFFT bin-sort recon,
spread.hpp:421) and re-quantizes in pass 2 rather than materialising the ids. The
descent (`!table`) path is the opposite: each lookup is a full `get_node_index` tree
walk, far dearer than a u32 load, so pass 1 stores ids into `leaf_ids_` and pass 2
reads them back — halving the descents per point (measured ~1.6x full-throughput on
deep no-leaf-table 1D fits, depth 17-18; see bench/binsort_phase0.md).

Why the 1D table path keeps re-quantizing (re-measured 2026-06-25, SPR w5-3435X,
paired-interleaved). An asm sweep flagged the pass-2 quantize cast (`vcvttpd2qq` /
`vcvttps2dq`) as ~34% of the 1D kernel at N=1e6, so we tried materialising the id in
pass 1 and reading it in pass 2 (one cast, not two). It is a win *only at small leaf
counts*: deg-8 smooth fits (32-64 leaves) gained ~+15%, and the deg-3 bin-sort
microbench gained ~+10% at depth 4-6. But from ~256 leaves up it *regresses* — -9%
to -26% (f64) and up to -37% (f32) by depth 16 — because at high leaf counts the
scatter is bound on the random `counts[]` RMW, the re-quantize overlaps those stalls
for free, and the extra `leaf_id_buf` round-trip is pure added traffic. Re-quantize is
therefore the right default across the whole leaf-count range; a leaf-count gate would
just reintroduce the kind of fragile cache-size threshold the 2-level-radix attempt was
reverted for. Stretch follow-up: narrow the single remaining cast to `vcvttpd2dq` (lat
1c vs 4c) instead of removing the second sweep.

---

## function.hpp — dispatch_packed_leaves

Full original docstring:

Stage 4 skeleton, shared between the tile bodies. Walk the packed leaf slices in id
order (recovering each `(off, cnt)` from the Reinecke cursor in `counts`), speculatively
prefetch the next non-empty leaf's coefficient store, and invoke `eval_run(id, off, cnt)`
on every non-empty leaf. Returns the one-past-end of the last real leaf's slice (== the
OOD bucket's offset). Only the per-run polyfit dispatch (the `eval_run` callable)
differs between the AoS and SoA layouts.

Context: leaf slices in `xp_packed` are laid out by leaf id, so walking ids
monotonically is also a sequential scan of the packed buffer. The speculative prefetch
targets the *next non-empty* leaf's coefficient array (not just `id+1` which may be
empty): an inner while-loop skips zero-count leaves before issuing the prefetch. On ND
inputs polyfit doesn't expose a coefficient pointer, so the prefetch targets the
evaluator object's first cacheline (domain params + start of coeffsFlat). On 1D scalar
inputs the explicit `coeffs().data()` pointer gets the hot Horner array directly.

---

## function.hpp — sorted_leaf_id_at

Full original docstring:

Per-point leaf-id lookup for the sorted 1D paths. `fast` and `ood_id` are pre-hoisted
by the caller to avoid re-deriving them per point.

The `sorted` paths deliberately do NOT call `leaf_id_of`: their bespoke scalar
early-return OOD check compiles to a tighter 1D loop than the generic flag+ternary
form, and unifying them was shown (objdump) to grow and reorder the `sorted` hot loop.
Keep the two forms separate.

---

## function.hpp — eval_pack

Full original docstring:

Compile-time-N batch point evaluation (1D scalar inputs/outputs).

Provided so consumers with a small fixed-size pack of evaluations can express intent at
the call site without an ad-hoc loop. Three regimes by `N`:

  * `N <= 16` (small): poet::static_for fully unrolls the scalar fan-out.
    operator() is TREEWEAVE_ALWAYS_INLINE so the N FMA chains run on independent
    registers — best ILP at small N.
  * `16 < N < kBatchPathFloor` (medium): plain for-loop. The compile-time N still lets
    the compiler unroll partially, and each operator() is inlined, so this matches the
    hand-rolled scalar_loop baseline. Avoids both poet's fully-unrolled code bloat at
    large N and the batch path's fixed setup overhead (counts/perm scratch).
  * `N >= kBatchPathFloor` (large): delegate to the SIMD-batched
    `operator()(xp, ys, n)`. The batch-path leaf dispatch amortises its fixed overhead
    only at high N.

kBatchPathFloor = 1024 picked from the bench_pack_scatter crossover sweep (1d_runge
deg=8, SPR, taskset -c 2). The 32->1024 bump is commit `3939d75 perf(eval_pack): raise
batch-path floor from 32 to 1024`.

---

## treeweave.hpp — auto_memory_budget_mib

Full original comment:

Auto memory budget (MiB) used when `options.max_memory_mib` is left negative. Leaf
storage grows ~geometrically with input_dim, so a flat cap reasonable in 1D is far too
tight in 3D; doubling per dimension keeps the default a small guardrail (4 / 8 / 16 MiB
for 1D / 2D / 3D) while still forcing an explicit opt-in for genuinely large tables.

---

## arch_dispatch.cpp — why dispatch returns a factory pointer

`xsimd::dispatch(...)` and its `operator()` are `noexcept`, but the fit can throw
(`MaxDepthExceeded`, `MemoryBudgetExceeded`, …) and the exception must reach the catch block in
`treeweave.cpp`. The dispatch functor (`SelectMakeEval`) therefore only *selects* — it returns the
address of `make_eval_for<Arch,…>` without running the fit — and the caller invokes that pointer
afterwards, outside the `noexcept` dispatch context, where exceptions propagate normally.

Runtime arch selection uses `available_architectures().has(Arch)` (NOT `Arch::available()`, which is a
`constexpr true` for every real arch and would always pick the widest *compiled* arch, causing SIGILL on
hosts lacking it — see memory: C-ABI multiarch dispatch fix).

---

## dispatch_arch.hpp — arch_list and family selection

`arch_dispatch.cpp` is compiled at each family's baseline `-march` (x86-64 → `best_arch=sse2`,
`armv8-a` → `neon64`, `rv64gc` → `rvv`), so the `std::conditional_t` ladder resolves to that family's
list. `xsimd::dispatch` over the list + `available_architectures().has` then picks the widest
host-supported variant at runtime.

Per-family lists:
- **x86**: full ladder `{avx512bw, fma3<avx2>, sse4_2, sse2}`, matching the four `-march` variant TUs
  CMake fans out. MSVC uses `avx` instead of `sse4_2` (no `/arch:SSE4.2` switch; its `/arch:` jumps
  SSE2 → AVX → AVX2 → AVX512).
- **aarch64**: `{neon64}` only. NEON64 is mandatory on ARMv8-A so it always dispatches. SVE is
  deliberately excluded: xsimd's `sve<N>` bakes the vector width at compile time, but the runtime
  `has(sve<N>)` probe only checks the SVE *presence* HWCAP bit, not the width — a fixed-width SVE
  variant would falsely match mismatched-width SVE hardware. `simdrng` makes the same choice.
- **riscv64**: `{rvv128}`. Best-effort / untested (no RISC-V CI runner). `rvv128` fixes the vector
  length to match the dispatch TU's `-mrvv-vector-bits=zvl` compile flag.

xsimd's x86 hierarchy has three independent roots (`sse2`, `avx`, `avx512f`) — no single x86 base class
— so the `dispatch_is_x86` check needs all three `is_base_of_v` tests.

---

## c_binding_detail.hpp — no include guard / phantom-Arch

`c_binding_detail.hpp` deliberately has **no include guard, no `#include`s, and no namespace**. It is textually
included inside an `anonymous` namespace nested in `treeweave::capi` by each per-arch variant TU:

```cpp
namespace treeweave::capi {
namespace {
#include <treeweave/detail/c_binding_detail.hpp>
}  // namespace
...
}
```

This gives `EvalImpl` / `EvalFactory` / `wrap_callback` **internal linkage**, preventing the linker from
COMDAT-folding the four per-`-march` variants onto a single architecture's codegen. All names it uses resolve
from the enclosing `treeweave::capi` / `treeweave` scopes already declared by `c_binding.hpp`.

**COMDAT-dedup fix (Bug #2):** `ArchTaggedScalar<Arch,T>` (1D→1D) and `ArchTaggedND<Arch,T,IN,OUT>` carry the
xsimd `Arch` type as a phantom template parameter. This makes `poly_eval::FuncEval<ArchTaggedScalar<Arch,...>,...>`
and `poly_eval::FuncEvalND<ArchTaggedND<Arch,...>,...>` distinct types per `-march` level, so all downstream
COMDAT kernel bodies have different mangled names per arch and the linker cannot fold them. No inline namespace
needed; the type system solves it cleanly.

Accepted limitation: only eval-path `poly_eval` types are arch-tagged; the fit-time single-point
`horner_nd_impl` lambda remains COMDAT-folded to the baseline scalar variant (no crash, no
eval-throughput impact — fit-time only).

---

## treeweave.hpp — kDefaultDegree

Full original comment:

Default leaf degree. The C-ABI tuning campaign (see arch_degree_table.hpp) found degree
7 wins or ties in every (arch, dtype, input_dim) cell — within ~1% in 1D and 2-10x in
2D/3D, and the only spill-free degree in the register-pressured wide cells — so the C++
template default matches the baked C-ABI value. Override per call via the `Degree`
template parameter.

---

## unroll evaluation (ponytail-pass)

### Change A: `poet::static_for` for inner lane sweeps in `for_each_leaf_id_batch`

Replaced the three `for (j = 0; j < lanes; ++j)` inner sweeps with
`poet::static_for<lanes>([&](auto J) { ... })`. Target: remove the inner
j-loop back-edge so the compiler is forced to fully unroll, and let the
lambda capture simplify register assignment.

**gcc 13.3.0 / AVX-512 asm evidence (inner sweep window):**

Both baseline and after-A: zero inner j-loop back-edges in the f32 and f64 paths.
gcc already unrolled the inner loop in the baseline (16 consecutive `incl`/`vpextrd`
instructions visible in the f32 path, one per lane). `static_for` makes the unrolling
explicit in the IR rather than relying on the optimiser's heuristic — identical
back-edge count, cleaner template instantiation.

**Instruction-count delta (nanobench, 3×, median):**

| case | baseline ins/eval | after-A ins/eval | delta |
|---|---|---|---|
| eval/1d/runge-deep/f64 | 64.44 | 62.94 | −2.3% |
| eval/1d/runge-deep/f32 | 49.91 | 49.50 | −0.8% |
| eval/2d/bump-deep/f64  | 360.45 | 360.45 | 0% |
| eval/3d/smooth-deep/f64| 1701.07| 1701.07| 0% |

Branch-per-eval (f64-deep): 8.87 → 7.37 (−1.5; static_for changes template
instantiation path, not the raw back-edge count, so the saving comes from
reduced lambda/loop-setup overhead in the IR, not a newly-absent jmp).

**Verdict: KEPT.** No regression; f64-deep instruction count −2.3%.

### Policy update (ponytail-pass, 2026-07-06)

At performance parity, `poet::static_for`/`dynamic_for` is PREFERRED over plain loops
(cross-compiler unroll consistency is the win). Regression beyond noise = reject;
parity or win = adopt. Policy applied in the evaluation below.

### Change B: `poet::dynamic_for<U, lanes>` outer loop in `leaf_ids_batch` — REJECTED

`leaf_ids_batch` is the dependency-free store path (`Function::leaf_ids`): outer loop
is `for (i = 0; i < n_simd; i += lanes) gather(...).store_unaligned(out+i)`. No
benchmark exercised it in the CI bench suite (nm confirmed zero symbol).

A minimal driver bench was written in /tmp/pony/parity/leaf_ids_bench.cpp calling
`Function::leaf_ids` via the public API. nm/objdump confirmed `leaf_ids_batch` is
non-inlined in the driver binary, with the expected `vpgatherdd + vmovdqu64 + add
$0x10 + jb` hot loop (0 spills, clean single-gather per iteration).

**Baseline (3× median, cyc/eval):**

| case | cyc/eval | ins/eval |
|---|---|---|
| f32/deg3/N65536 | 0.65 | 1.06 |
| f32/deg3/N1000000 | 0.69 | 1.06 |
| f32/deg7/N65536 | 0.70 | 1.06 |

Both callable forms tested (`lane_by_value` and `index_only`) with U=4 (32 AVX-512 regs):

**After dynamic_for (lane_by_value form), cyc/eval:**

| case | cyc/eval | delta |
|---|---|---|
| f32/deg3/N65536 | 0.82 | **+26%** |
| f32/deg3/N1000000 | 0.92 | **+33%** |
| f32/deg7/N65536 | 1.06 | **+51%** |

**After dynamic_for (index_only form):** identical regressions — same asm generated.

**Root cause:** `dynamic_for`'s closure mechanism stores captured zmm broadcasted
constants (lo_v, hi_v, inv_v, mask_v, ood_v) to a stack struct. The compiler loses
visibility that `store_unaligned(out+i)` is a zmm-stride write and falls back to
14 shuffle instructions (vextracti32x4, vpextrd, vpsrldq, valignd, vpunpckldq,
vpunpcklqdq, vinserti128) + 2×vmovdqu(ymm) instead of 1×vmovdqu64(zmm). The
zmm store is the critical path; the shuffle degrades every case.

**Verdict: REJECTED-REGRESSION** (both forms). Outer loop stays as a plain `for`.

### Change C: `poet::dynamic_for` for `for_each_leaf_id_batch` outer loops — UNSUITABLE

All three outer loops (kFastInt32/f32, kFastGatherF64/f64, else/SSE-double) write
SIMD results into a shared scratch buffer (`id_arr`, `q_arr`/`ood_arr`) and then
consume it with a `static_for` inner sweep + user-supplied `on_id` callback. Any
unrolled pairing of consecutive outer iterations would corrupt the shared buffer before
the first iteration's sweep completes. No mitigation is possible without allocating U
separate scratch arrays (more stack, more register pressure). Verdict: **UNSUITABLE**.

### Misc cleanup

- `n & ~(lanes - 1)` → `n & -lanes` (two's complement power-of-two mask, more compact).
- Removed `if (n_simd < n)` guard before the scalar tail call in `leaf_ids_batch`:
  `for_each_leaf_id_batch` with n=0 is a no-op, the guard was dead code.

### dynamic_for closure-spill: root cause + poet fix (2026-07-06)

Root cause of the 26-51% regression: GCC SRA refuses to promote the lambda
closure's zmm fields (5 broadcasts = 320-byte anonymous struct) to registers
because the closure's address is taken for the noinline tail call — the whole
struct is conservatively address-taken, so every unrolled iteration reloads all
5 constants from [rsp+N]. Not fixable library-side for the capturing-lambda API.

Fix (prototyped, works): new `poet::dynamic_for_args<U>(count, func, hot_args...)`
passing hot values as explicit by-value parameters — zero stack loads, register
layout identical to plain-for. Standalone bench: plain 2.005 cyc/elem, lambda
+3%, functor/args −9%. Patch + repro preserved at ~/scratch/poet-dynamic-for-args/
(89-line addition to poet's core/dynamic_for.hpp), ready to upstream to
DiamonDinoia/POET. treeweave's leaf_ids_batch keeps the plain for regardless:
gather-throughput-bound, no scalable win.
