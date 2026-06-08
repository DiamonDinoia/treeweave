# Bin-sort Phase 0 measurement (gating data)

Harness: `examples/c++/treeweave_bench_binsort.cpp` (clang 18, Release, `taskset -c 2`,
Xeon w5-3435X, powersave governor — cycle figures are invariant-TSC ticks/pt, stable
for relative comparison). Degree-3 1D runge fit, `min_uniform_depth` pins `n_leaves`.
Per-phase `cyc/pt` over one 64K-point tile via the `bench_partition_phases` hook
(histogram/scatter figures already net of the quantize they re-run). `full MEvals/s`
is the public unsorted batch `operator()` over 1M points (manual chrono median).

## Per-phase cyc/pt (quantize is Phase-1 target; hist+scatter is Phase-3 target)

```
            depth4  ...........................  depth16 (n_leaves 16..64K)
native f64  quant ~1.5 flat | hist 0.86->2.46 | scatter 3.53->8.20
native f32  quant ~2.2 flat | hist 0.80->2.02 | scatter 7.22->10.60
v3     f64  quant ~1.45     | hist 0.60->2.47 | scatter 2.81->8.55
v3     f32  quant ~1.45     | hist 0.54->1.90 | scatter 2.14->7.04
v2     f64  quant ~1.65     | hist 0.39->2.21 | scatter 2.80->8.21
v2     f32  quant ~1.70     | hist 0.36->1.75 | scatter 1.42->6.57
```

Full throughput collapse with n_leaves (native f64): 307 -> 50 MEvals/s (16 -> 64K leaves),
a ~6x drop dominated entirely by the sort (scatter + histogram), not the quantize.

## Conclusions

1. **Quantize is never the bottleneck** (1.4–2.2 cyc/pt) vs scatter (3–10) and histogram
   (0.4–2.5). The "serialized scalar lane-sweep" the plan flagged is real but cheap:
   f64 scalar sweep on v3/v2 (~1.5) ties the AVX-512DQ int64 fast path (~1.5).

2. **f64 int32 narrowing has no measured headroom.** f64 quantize is ~1.5 cyc/pt on every
   arch already. xsimd has no lane-matched `double->int32` cast (lane counts differ), so a
   f64 int32 path needs per-arch intrinsics for <0.1 cyc/pt — fails "keep it simple". DROP.

3. **f32 quantize is elevated only on AVX-512 (2.2 cyc/pt, 16-lane scalar sweep).** xsimd
   *does* have lane-matched `float->int32` `fast_cast` (-> `vcvttps2dq`) on SSE2/AVX. A
   single clean f32 int32 fast path removes the 16 scalar converts. SHIP (small, low-risk).

4. **Scatter + histogram thrash cache at large n_leaves — Phase 3 (radix) is justified.**
   scatter grows ~3x (3 -> ~8–10 cyc/pt) and histogram ~5x as counts[] (4·n_leaves bytes)
   and the 512 KB xp_packed scatter overflow L1. Knee begins ~1–4K leaves (counts[] nearing
   L1's 32 KB). This is the dominant, n_leaves-sensitive cost — the real win.

5. **ND batched quantize** is unmeasured (harness is 1D, where `for_each_leaf_id_batch`
   lives). Speculative; defer unless ND is shown to matter.

## Recommended scope
- Phase 1: f32 int32 fast path only (drop f64 int32, defer ND batch quantize).
- Phase 2: keep — independent; targets the deep-tree descent double-walk (not in this 1D
  table harness; verify its own win on a descent-fallback fit).
- Phase 3: SHIP — 2-level radix above a ~few-thousand-leaf threshold; the headline win.

---

# Outcomes (post-implementation, 2026-06)

Same harness/host (Xeon w5-3435X, clang 18, `taskset -c 2`, powersave). NOTE: absolute
`full MEvals/s` at high `n_leaves` is memory/turbo-bound and varies run-to-run on this
shared powersave box (≈2× swings seen between idle and contended); the *controlled
back-to-back* deltas below (same build conditions, minutes apart) and the TSC per-phase
`cyc/pt` are the reliable signals, not the absolute throughput.

## Phase 1 — f32 int32 quantize: SHIPPED ✓
`for_each_leaf_id_batch` casts f32→int32 via `xsimd::batch_cast` (`vcvttps2dq`), replacing
the 16-lane per-lane scalar sweep that made f32-on-AVX-512 the costliest quantize cell.
Per-phase f32 quantize: ~2.2 cyc/pt (orig 16-lane sweep) → **~1.79 native / ~1.45 v3**.
objdump-confirmed: `vcvttps2dq` (f32, all x86), `vcvttpd2qq` (f64 AVX-512DQ, intact),
`vrndscaleps/pd` (AVX-512) / `vroundps/pd` (v3/v2) for the floor. Bit-identical in-domain
leaf ids; f64 untouched.

## Phase 3 — 2-level radix: REVERTED ✗ (measured regression)
Controlled A/B on this host (radix build vs flat-counting-sort build, same session):

```
            full MEvals/s (1M pts)   radix      flat      flat is
  f64 depth14 (16384 leaves)         22         112       5.1× faster
  f64 depth16 (65536 leaves)         13          56       4.3× faster
  f32 depth14                        37         163       4.4× faster
  f32 depth16                        34          78       2.3× faster
  depth12 (4096, both flat)         ~145       ~171       agree (control)
```
The radix is **2–5× slower** at every reachable large-leaf depth. The Phase-0 premise —
that `counts[]` (256 KiB at 64K leaves) thrashes L2 — does not hold on this class of
hardware: SPR has **2 MiB L2/core**, so 256 KiB is L2-resident and there is no thrashing
to fix. The radix's extra coarse pass (two quantize sweeps + 16 B/pt written to scratch
and read back) costs more than it saves. The leaf-table fast path caps at 64K entries
(depth 16), so the radix-eligible `n_leaves` never exceeds 65536 — i.e. the radix was a
regression for *every* input that could reach it here. Removed entirely (code + Scratch
buffers + threshold + gate). The flat counting sort is the sole large-`n_leaves` path.

## Phase 2 — descent (!table) leaf-id materialization: SHIPPED ✓
Deep 1D fits (`min_uniform_depth` 17–18 → >64K-entry cap → no leaf table → tree-descent
`partition_into_leaves`) walked `get_node_index` twice per point (histogram + scatter).
Pass 1 now stores ids in a `leaf_ids_` scratch (sized only on the `!table` path) and pass 2
reads them back. Controlled A/B (idle host, back-to-back):

```
            full MEvals/s (1M pts)   baseline   materialized   speedup
  f64 depth17 (131072 leaves)        7.9        12.8           1.61×
  f64 depth18 (262144 leaves)        5.5         9.4           1.68×
```
The 1D leaf-table fast path and ND-with-table keep re-quantizing (one cheap SIMD/scalar
quantize beats an L1d round trip); only the descent path materializes. `run_descent` in the
harness measures this; toggle was inlined after measurement.

## Two latent correctness bugs fixed alongside (caught by strengthened parity tests)
- **OOD-low quantize sliver**: `q=(x-lo)*inv ∈ (-1,0)` truncated to cell 0 (false in-domain).
  Fixed with `std::floor`/`xsimd::floor` before the truncating convert in `quantize_one` and
  `for_each_leaf_id_batch` (in-domain bit-identical; the sliver now wraps OOD).
- **Scalar `operator()` NaN/multi-output OOD**: NaN slipped the `x<lo||x>=hi` guard into an
  OOB `subtrees_` read (now positive-logic guard); and the OOD return `output_type{nan}`
  filled only element 0 of a multi-output `std::array` (now NaN-fills all components).
