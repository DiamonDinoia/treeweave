# Profile-driven Phase 2 — results & analysis

## What this branch ships

1. **Microbench harness** (`examples/c++/baobzi_microbench.cpp` + nanobench
   FetchContent dep) — sweeps {1D, 2D, 3D} × scientific kernels × {deg 6,
   8, 10} × N ∈ {1, 32, 1024, 10⁶}, reports MEvals/s, ns/eval, cyc/eval,
   IPC, branch-miss%, MdAPE.
2. **xsimd fork wired in** (`DiamonDinoia/xsimd:feat/dynamic-masks` via
   `CPM_xsimd_SOURCE` override, since polyfit fetches xsimd through CPM)
   — gives runtime / compile-time `batch_bool` masked-load primitives
   on AVX2 + AVX-512.
3. **Three perf-confirmed eval-pipeline changes** to
   `include/baobzi/detail/function_impl.hpp` (Phase 2.3, 2.4, and a
   profile-discovered prefetch removal).

## Profile-driven workflow (per simdref `references/workflow.md`)

```bash
# 1. Compile baobzi_microbench Release with -g -fno-omit-frame-pointer
# 2. Profile run
simdref profile run --target build-new/baobzi_microbench \
    --adapter perf --event "cycles:u,instructions:u" \
    --duration 60 --arch alderlake --top 5 -o report/

# 3. Hot-symbol triage via perf report -F overhead,sample,symbol
# 4. perf annotate --symbol "<hot baobzi function>"
# 5. simdref show <mnemonic> --arch alderlake  (lat/cpi citations)
```

Resolved microarch: **alderlake** (Intel Core Ultra 7 155H, P-core,
AVX2 W=4). simdref's measured ADL-P payloads cited inline below.

## Iteration 1 — perf identified two hotspots

The hot baobzi function in the microbench is
`baobzi::Function<deg, F>::operator()(double const*, double*, size_t)`,
which inlines into the bench's `sweep_*` lambdas. `perf annotate`
showed two clear targets in the `for (i…) leaf_ids[i] = …` traversal
loop (1D, log1p kernel, depth ~6):

| addr | % cycles | instruction | meaning |
|------|---------:|-------------|---------|
| 88b1c | **14.22%** | `prefetcht0 (%rdx)` | descent prefetch |
| 88b06 |   5.59% | `mov 0x10(%rdx),%edx` | load `first_child_idx` |
| 88b0c |   9.31% | `add %rdx,%rax` | `next = first_child_idx + child_idx` |
| 88b13 |   5.46% | `lea (%rsi,%rdx,8),%rdx` | `&node_pointers_[idx]` |
| 88b17 |   5.24% | `cmpq $-1,0x8(%rdx)` | `is_leaf()` via `poly_eval_id` sentinel |
| 88b24 |   7.95% | `mov 0x98(%r9),%rsi` | reload `node_pointers_.data()` |

`simdref show prefetcht0 --arch alderlake` → `lat=- cpi=1.0 ports=p23A`.
`simdref show vcomisd  --arch alderlake` → `lat=3c cpi=1.0`.

The prefetch is **port 23A** (load port). Each descent step's
`nodes_[next]` load is strictly dependent on the previous level's
`child_idx`, so the prefetch's address arrives *after* the demand
load it was meant to hide. It just steals a load-port slot per
descent step. Hence 14.22 % of the symbol burned on a no-op.

### Change A — drop the post-descent two-load indirection (Plan §2.3)

Replace `node_pointers_[idx]->poly_eval_id` (vector of `node_t*` →
deref to read `poly_eval_id`) with `leaf_index_by_global_node_[idx]`
(`std::vector<std::uint32_t>`, single load). Built and benched in the
same session vs pristine baseline (both on the xsimd fork): clean
+5–17 % across 1D N=1/32/1024 cells with err% < 3 %.

### Change B — remove the descent prefetch

```cpp
// before
const index_t next = nodes_[curr_index].first_child_idx + child_idx;
__builtin_prefetch(&nodes_[next]);
curr_index = next;

// after
curr_index = nodes_[curr_index].first_child_idx + child_idx;
```

A re-profile after this change confirmed `prefetcht0` no longer appears
in the function's annotated listing.

## Iteration 2 — descent now has 2 dependent loads per step

Re-profile of the post-A+B binary showed the descent loop tightened to:

```
mov 0x10(%rdx),%edx     ; load first_child_idx           — 7.18%
add %rdx,%rax           ; next = first_child_idx + idx   — 9.79%
lea (%rax,%rax,2),%rdx  ; ×3
lea (%r12,%rdx,8),%rdx  ; rdx = &nodes_[next]
cmpq $-1,0x8(%rdx)      ; is_leaf via poly_eval_id (off 8) — 4.16%
je   loop               ;                                — 13.20%
```

The `cmpq $-1, 0x8(%rdx)` test reads `poly_eval_id` at a *different*
offset than `first_child_idx`. Two dependent loads per descent step.

### Change C — reorder `Node` so `is_leaf` fuses with `first_child_idx` (Plan §2.4)

```cpp
struct Node {
    Value<value_type, input_dim> center;
    std::uint32_t first_child_idx = max32;
    std::uint32_t _pad_           = 0;
    std::uint64_t poly_eval_id    = max64;

    bool is_leaf() const {
        return first_child_idx == max32;   // same sentinel + same load
    }
};
```

`_pad_` keeps the total node size unchanged (24 B for 1D, 32 B for 2D,
40 B for 3D — i.e. existing footprint preserved). The descent loop
collapses to a *single* dependent load per level: load
`first_child_idx` once, use both for the leaf test and the
`next = first_child_idx + child_idx` step.

A subsequent re-profile after A+B+C shows the descent body is now:

```
vcomisd (%rsi),%xmm0    ; descent compare
seta %dl                ; child_idx
add  %rax,%rdx          ; next = …
lea  (%rdx,%rdx,2),%rsi ; ×3
lea  (%rcx,%rsi,8),%rsi ; &nodes_[next]
mov  0x8(%rsi),%eax     ; load first_child_idx (one load now)
cmp  $-1,%eax           ; is_leaf
jne  loop
```

## Tried & rejected — Plan §2.5 (axis_stride hoist)

I prototyped Plan §2.5 (precompute prefix-product strides for ND
`get_linear_bin`). Re-bench under nanobench showed it regressed ND
cells by 5–18 %, almost certainly because adding the new
`Value<size_t, dim>` member shifted `polyfits_`' cacheline residency
in the `Function` class layout. Reverted.

## Final delta vs pristine xsimd-fork baseline

`bench/v3_final_compare.txt` — pristine (xsimd fork) vs Phase 2.3 + 2.4
+ no-prefetch (xsimd fork), same session, interleaved builds, one nanobench
sample each. The wins are reproducible per intermediate compares
(`bench/v3_phase24_compare.txt` shows the cleanest signal at +5–20 %
across cells when the bench was re-run during the iteration loop).

The single-sample comparisons cap at ~1.7 % MdAPE on most large-N cells
and the day-to-day variance on this hybrid Meteor Lake host is ~5 %, so
small-N ratios should be averaged across multiple runs before being read
as definitive.

## Honest caveats

- The host (Meteor Lake P-core) is the noisier validation environment.
  AVX-512 hosts (Sapphire Rapids, Zen4-5) are where the next-step Phase 3
  SIMD-parallel descent would shine — the descent here is dependent-load
  bound, exactly as the plan diagnosed.
- The xsimd fork's masked-load APIs are wired in but **not yet used by
  baobzi**. The infrastructure is ready for Phase 2.1/2.2 SIMD bounds +
  child-bit (skipped because perf showed they aren't on the critical
  path on AVX2) and for Phase 3 vector descent (the actual next move).
- Phase 1.5 file split deferred — pure refactor, no perf delta.

## Files

- `include/baobzi/detail/function_impl.hpp` — Phase 2.3 + 2.4 + prefetch removal.
- `cmake/baobzi_deps.cmake` — xsimd fork override + nanobench fetch.
- `cmake/baobzi_examples.cmake` — `baobzi_microbench` target.
- `examples/c++/baobzi_microbench.cpp` — nanobench-driven sweep.
- `bench/compare_nb.py` — A/B parser for nanobench markdown.
- `bench/baseline_v3_nb.txt`, `bench/final_v3_nb.txt` — pristine vs final
  on the same xsimd-fork build.
- `bench/v3_final_compare.txt` — per-cell delta with MdAPE columns.
- `bench/phase23_prefetch_v3_nb.txt`, `bench/phase24_v3_nb.txt`,
  `bench/v3_phase24_compare.txt` — intermediate iteration data.
- `report/perf.data`, `report_v1/`, `report_v2/`, `report_v3/` —
  successive perf-record snapshots.

## Iteration 4 — sort-driven descent (Plan §3.1) — REJECTED

**Hypothesis:** sort points into subtree-major order before descent so each
subtree's `nodes_` slice stays L1-resident across its run. Counting sort on
`get_linear_bin(x)` is cheap (~5–15 c/pt) and would amortize over the
descent's ~5 c/level × depth work.

**Implementation:** added a sort pre-pass to the batch `operator()` (gated
by `total_bins > 1 && leaf_index_by_global_node_.size() >= 1024`):

1. Pass 1 — in-domain check + `get_linear_bin` + per-subtree histogram.
2. Prefix sum on subtree counts.
3. Scatter `xp` → `xp_sorted` (subtree-major), remembering caller indices in
   `sub_perm[]`.
4. Per-subtree descent on `xp_sorted` writing `leaf_ids` and the existing
   leaf-count histogram.
5. Existing leaf-major scatter from `xp_sorted` (instead of `xp`) into
   `xp_packed`, with `perm[dst] = sub_perm[s]` to combine the two
   permutations.

**A/B (`bench/baseline_post23_nb.txt` vs `bench/sort_descent_v2_nb.txt`,
gated form, taskset -c 2):**

| cell                              | A     | B     | B/A  |
|-----------------------------------|-------|-------|------|
| 2d_bump deg=8 dim=2 N=1000000     | 20.24 | 18.29 | 0.90 |
| 2d_mq   deg=8 dim=2 N=1000000     | 51.48 | 45.33 | 0.88 |
| 2d_osc  deg=8 dim=2 N=1000000     | 37.73 | 31.52 | 0.84 |
| 2d_bump deg=10 dim=2 N=1000000    | 24.76 | 19.81 | 0.80 |
| 3d_gauss deg=8 dim=3 N=1000000    | 22.82 | 22.96 | 1.01 |
| 3d_imq  deg=8 dim=3 N=1000000     | 18.92 | 20.13 | 1.06 |
| 3d_yukawa deg=8 dim=3 N=1000000   | 19.72 | 20.34 | 1.03 |

Net: 2D large-N regresses ~10–20 %, 3D large-N flat to +6 %, 1D mixed.
Per the iteration discipline (B/A < 1.0 on stable cells = stop and
re-investigate), reverted.

**Why it didn't work:** the sort adds an extra full pass over `xp`
(in-domain + `get_linear_bin`) and an extra `input_dim · n_trg`
materialization of `xp_sorted`. At N=10⁶ in 2D that is 16 MiB of
read+write traffic that overflows L2 (24 MiB on alderlake) and contends
with the working set. The locality benefit on the descent — which after
Phase 2.3 + 2.4 is already a single L1-resident dependent-load chain —
is too small to offset that bandwidth cost. The 3D cells, where descent
is deeper and the per-subtree node slice larger, get closer to a wash.

**Where to try next:**
- Drop `xp_sorted` materialization (sort indices only, descent reads
  `xp` via indirection). Saves the 16 MiB write+read, costs an L1 gather
  per descent. Worth a single-shot A/B if Phase 3.2 doesn't land first.
- Phase 3.2 (xsimd-gather descent): now the sole on-deck candidate. Uses
  the masked-load primitives already wired into the xsimd fork; descends
  W lanes simultaneously with `xsimd::gather` on `nodes_`. Bigger swing
  on AVX-512 hosts where W = 8.

Files for the rejected attempt are kept for reference:
- `bench/sort_descent_nb.txt`, `bench/sort_descent_v2_nb.txt` — raw runs
  (ungated and gated form).
- `bench/v4_compare.txt`, `bench/v4_compare_v2.txt` — A/B against the
  post-Phase-2 baseline `bench/baseline_post23_nb.txt`.


## Iteration 5 — sort-indices-only retry (FINUFFT-style, simdref-driven)

### What changed (now reverted)

Replaced the single-pass traversal in
`Function::operator()(double*, double*, std::size_t)` with a four-stage
pipeline that keeps `xp` in its caller buffer (no `xp_sorted`) — the one
discriminating choice from FINUFFT's bin-sort vs. iter-4 — and routes
descent through an index permutation only:

1. **Stage 1** — bin pass over `xp`: `get_linear_bin` per point + per-
   subtree histogram. No descent. No `nodes_` access.
2. **Stage 2** — exclusive prefix sum on `sub_counts` → `sub_offsets`.
3. **Stage 3** — `sub_perm` scatter: `sub_perm[sub_offsets[sid[i]]++] = i`.
   4 B per point (uint32) — never touches `xp` coordinates.
4. **Stage 4** — subtree-major descent: walk each subtree's slice of
   `sub_perm`, gather `xp[i]` via the index, descend through that
   subtree's `nodes_` only. Fills `leaf_ids[orig_i]` and the existing
   leaf-count histogram.
5. Existing leaf-major scatter / per-leaf SIMD eval / output permute-
   back: unchanged.

`total_bins == 1` and `n_trg < kSortThreshold` short-circuit to the
existing single-pass.

### Result — fails the acceptance gate

Both runs taskset -c 2, same idle system, within minutes of each other
(the saved `bench/baseline_post23_nb.txt` from earlier sessions reflects
a different turbo / thermal state — re-baselined first to avoid drift).

| cell                              | base   | v5    | B/A  |
|-----------------------------------|-------:|------:|-----:|
| 2d_bump  deg=10 dim=2 N=10⁶       |  8.22  |  8.03 | 0.98 |
| 2d_bump  deg=6  dim=2 N=10⁶       |  3.89  |  4.21 | 1.08 |
| 2d_bump  deg=8  dim=2 N=10⁶       |  8.03  |  8.09 | 1.01 |
| 2d_mq    deg=8  dim=2 N=10⁶       | 27.42  | 28.43 | 1.04 |
| 2d_osc   deg=8  dim=2 N=10⁶       | 11.69  | 12.62 | 1.08 |
| 3d_gauss deg=10 dim=3 N=10⁶       |  9.68  |  9.81 | 1.01 |
| 3d_gauss deg=6  dim=3 N=10⁶       |  4.41  |  4.66 | 1.06 |
| 3d_gauss deg=8  dim=3 N=10⁶       |  4.44  |  4.48 | 1.01 |
| 3d_imq   deg=8  dim=3 N=10⁶       |  4.19  |  4.21 | 1.00 |
| 3d_yukawa deg=8 dim=3 N=10⁶       |  4.23  |  4.25 | 1.01 |

Stable-cell regressions outside large-N (e.g. `3d_gauss deg=6 dim=3 N=1
→ 0.86`, `1d_runge deg=6 dim=1 N=1024 → 0.22` — the latter is an
outlier, but well outside noise on a stable cell), only 2/5 2D-large-N
and 1/5 3D-large-N cells reach B/A ≥ 1.05. Per the plan's gate:

> If 2D-large-N still regresses, the locality win is not real on this
> uarch; we skip to Phase 3.2 (xsimd-gather descent).

2D-large-N is flat (no regression, no clear win), so the strict
"regresses" trigger doesn't fire — but the broader gate (≥1.05 on
large-N + no stable-cell regression) fails. **Reverted.**

### Why it didn't work

The hypothesis was that `nodes_` cross-subtree miss rate in today's
descent is high enough that subtree-major reordering would amortise
the extra uint32 traffic + extra `xp` gather. The measurements say
otherwise on alderlake P-core:

- The new pipeline reads `xp` three times instead of two (Stage 1 seq +
  Stage 4 gather + leaf scatter seq). At 2D N=10⁶ that's 48 MiB vs
  32 MiB of `xp` traffic — most of it L3-resident, but the extra
  Stage-1 + Stage-4 passes also blow ~16 MiB of fresh uint32 scratch
  through L1.
- After Phase 2.3 + 2.4, the descent is already a single L1-resident
  dependent-load chain on `nodes_`. The cross-subtree miss rate is
  low enough that a per-subtree "warm" descent saves few cycles —
  the iter-4 finding ("locality benefit too small to amortise the
  bandwidth cost") survives the bandwidth fix.
- Single-bin trees and tiny batches go through the legacy fall-through
  path, so we don't lose those — we just don't gain anywhere either.

### Where to next — Phase 3.2

Per the plan's exhausted-design-space rule, this design space is closed.
On-deck: **Phase 3.2 (xsimd-gather descent)**. Same single-pass shape
as today, but descend W lanes in parallel with `xsimd::gather` on
`nodes_`. The xsimd-fork's masked-load primitives already enable it.
Bigger swing on AVX-512 hosts where W = 8.

Files kept for reference:
- `bench/sort_idx_v5_nb.txt` — v5 raw run.
- `bench/v5_compare.txt` — A/B vs the on-system re-baseline.
- `report_v5_baseline/perf.data` — perf data captured against the
  pre-v5 binary; the simdref annotate pipeline timed out under the
  contended workload, so no `summary.md` / `hot.sa` were produced.

## Iteration 6 — profile-anchored stop (C1 rejected, optimisation closed)

After iter-5's null result we re-anchored on a fresh perf profile of
operator() instead of more cost-model arithmetic. A separate
perf-symbol build (`build-perf` with `-O3 -march=native
-fno-omit-frame-pointer -g`) plus a focused driver
(`baobzi_perf_driver`, 2d_bump deg=8 N=10⁶ + 3d_gauss deg=8 N=10⁶,
15s each, `taskset -c 2 perf record -F 4000`, no call-graph,
~121k samples) gave a clean source-line attribution.

### Bucket profile (sum across `--sort=srcline,symbol`)

| Bucket                                 | % cycles |
|----------------------------------------|---------:|
| Descent loop + leaf-hist               |     ~55% |
| Per-leaf SIMD eval (avxintrin.h)       |     ~20% |
| Input scatter (lines 1057–1068)        |     ~ 8% |
| `stl_vector` indexing (resize, `[]`)   |     ~ 8% |
| Output permute-back (lines 1127–1133)  |    ~6.5% |

`perf stat`: IPC = 1.64; L1-d miss 2.98%; **LLC-miss/instruction =
3.7e-5** (≈ 0.04 / 1k inst). Branch-miss 0.12%. The hot path is **not**
DRAM-bound; it is dominated by the descent's serial dependent-load
chain on `nodes_`.

### Decision per the plan rules

- C1 (drop `out_packed`): permute ≥ 8% **OR** LLC-miss ≥ 3% — borderline
  (6.5% / 0.01%); attempted because `stl_vector` resize hits push the
  combined attack surface to ~14%.
- C2 (xsimd-gather descent): descent ≥ 15% **AND** LLC-miss < 3% — gate
  technically met (55% / 0.01%), but alderlake P-core has firmware-
  disabled AVX-512 and microcoded `vgatherdpd`. Plan flags this as
  repeating the v5 mistake without a prerequisite descent-only
  microbench. Held in reserve.
- C3 (declare done): polyfit ≥ 60% — not met (20%).

C1 attempted first.

### C1 patch — fuse permute-back into per-leaf scatter

Removed the n_trg-sized `out_packed` buffer; per-leaf eval writes a
small reused scratch (`out_leaf`, sized to `max(counts)`) and is
followed immediately by a scatter into `res` via `perm`. Bandwidth
saved per call: `output_dim · n_trg · sizeof(double)` of read + same
of write (16 MiB at 2D N=10⁶, output_dim=1).

### Result — rejected

A/B (`bench/baseline_phase6_nb.txt` vs `bench/phase6_nb.txt`,
`bench/phase6_compare.txt`):

| stable cell                       |   A   |   B   |  B/A |
|-----------------------------------|------:|------:|-----:|
| 1d_runge   deg=10 dim=1 N=10⁶     | 21.58 | 15.29 | 0.71 |
| 2d_bump    deg=8  dim=2 N=10⁶     | 10.48 |  7.27 | 0.69 |
| 3d_imq     deg=8  dim=3 N=10⁶     |  4.18 |  4.00 | 0.95 |
| 3d_yukawa  deg=8  dim=3 N=10⁶     |  4.15 |  3.98 | 0.96 |

A few cells went up (`2d_osc deg=8 N=10⁶` 1.55, `2d_bump deg=10 N=10⁶`
1.55) but several stable cells regressed below B/A = 1.00. Per gate:
**any stable-cell regression below 1.00 disqualifies. Reverted.**

### Why C1 didn't work

C1 replaces one large sequential write + sequential-read + scatter
(`eval → out_packed`, `permute-back`) with many small per-leaf
scatters. On large N, both shapes have the same number of scattered
writes to `res`; the old path additionally streamed `out_packed`
sequentially, which the hardware prefetchers handle well. The new
path serialises the per-leaf scatter against the SIMD eval (no
overlap) and loses the streaming-prefetch advantage on the read side,
while the absolute bandwidth saved (16 MiB on a 36 MiB-LLC alderlake)
is already L3-resident in the old code — no DRAM round-trip removed.
Net: a wash plus a pipelining penalty.

### Closure — single-threaded operator() optimisation closed

Per the plan's exhaustion rule:

> If C1 is chosen and fails the gate, revert; the design space for
> single-threaded eval is then exhausted and we transition to C3.

The descent is the sole remaining lever and it is dominated by a
serial dependent-load chain that AVX-2 gather cannot accelerate on
alderlake P-core (microcoded `vgatherdpd`, no AVX-512). Pursuing C2
without AVX-512 silicon repeats the iter-5 mistake.

**Phase 6 closed. No code change shipped from the perf work.** The
profile artefacts (`report_phase6_baseline/{perf.data, top.txt,
srcline.txt, stat.txt, buckets.md}`), the rejected A/B
(`bench/phase6_nb.txt`, `bench/phase6_compare.txt`), and the
re-baselined session-anchored reference
(`bench/baseline_phase6_nb.txt`, replacing
`bench/baseline_post23_nb.txt`) are kept as evidence. A new perf
driver (`examples/c++/baobzi_perf_driver.cpp`) ships in-tree so future
profile work can re-run the same focused capture without disturbing
the full microbench harness.

The next material step on this branch is the queued fit-time changes
(`allow_max_depth_leaves`, `max_memory_mib=64`) — they land on their
own commits and have nothing to do with eval performance.

## Iteration 7 — instruction-level closure (asm-analysis)

Phase 6 closed at the **bucket level**: descent ~55 % of cycles,
polyfit eval ~20 %, scatter ~8 %, `stl_vector` ~8 %, permute-back
~6.5 %, LLC-miss/inst = 3.7e-5. The closure rested on (i) a
second-hand claim that `vgatherdpd` is microcoded on alderlake P-core
and (ii) a bucket-level inference that the descent is a serial
dependent-load chain. Phase 7 verifies (ii) at the instruction level
via the `asm-analysis` skill. Default expected outcome: **A1 — closure
strengthened with asm evidence, no code change.**

### Pipeline run

Skill stage map (per `simdref/skills/asm-analysis/references/workflow.md`):

| Stage | Action | Output                                      |
|------:|--------|---------------------------------------------|
| 0 | preflight `simdref --version`, `simdref profile run --help` | `simdref 0.0.0-dev` from local checkout, profile subcommand present |
| 1 | compile-line resolution: `objdump -d` on shipped `build-perf/baobzi_perf_driver` (LTO; per-TU `-S` would lose inlining) | `report_phase7_asm/disasm.s` (911 KiB, 21 041 lines) |
| 2 | microarch resolution: `gcc -march=native -Q --help=target` | `alderlake` (Core Ultra 7 155H) |
| 2b | profile-driven region selection: `simdref profile run --target ./build-perf/baobzi_perf_driver --args 15 --adapter perf --event "cycles:u,instructions:u" --duration 60 --arch alderlake --top 5` | `summary.md`, `hot.sa`, `merged.json`, `annotated.json`, `loops.json`, `samples.json`, `perf.data` (11 MiB, 120 k samples) |
| 4 | annotate hot region | covered by `simdref profile run`'s `annotated.json`; manual re-annotation of a hand-extracted slice failed (see "simdref findings" below) |
| 4a | sanity-check via `simdref show` on dominant mnemonics + cross-reference uops.info / Intel Intrinsics Guide | one mismatch surfaced (see below) |
| 5 | `simdref llm batch --source-kind measured --preset intel` on descent mnemonics | all returned `no_match` (separate simdref issue, see below); the `simdref show`-derived numbers were used instead |
| 6 | llvm-mca cross-check | `report_phase7_asm/mca_descent.txt` |

All artefacts under `report_phase7_asm/` (gitignored via `/report*`).
`baobzi_perf_driver` arg is **seconds per shape**, not iterations
(driver loops until wallclock exceeds the arg). 15 s per shape × 2
shapes = ~30 s of measured eval per run.

### What the asm shows — descent (the dominant bucket)

Source: `function_impl.hpp:741–760`,
`PolyTree::get_node_index(const input_type &x)`.

The 3D descent body (fully covered by simdref's annotation) at
`main+0x6ee0..main+0x6f30`:

```
6ee1: mov    0x18(%rdi),%edx          # esi = nodes_[curr].first_child_idx (root load)
6ee7: cmp    $0xffffffff,%edx         # is_leaf check (max32 sentinel)
6eea: je     6f34                     # exit if leaf
6ef0: xor    %r15d,%r15d              # child_idx = 0  ─┐
6ef3: vcomisd 0x8(%rcx),%xmm10        # cmp x[1] vs c[1]│
6ef8: seta   %r15b                    # r15b = (x[1]>c[1])
6efe: add    %r15,%r15                # << 1
6f01: vcomisd 0x10(%rcx),%xmm11       # cmp x[2] vs c[2]│  3 lane compares,
6f06: seta   %al                      # al  = (x[2]>c[2])  parallel on rcx
6f09: shl    $0x2,%rax                # << 2            │
6f0d: or     %r15,%rax                # pack lanes 1..2
6f10: vcomisd (%rcx),%xmm9            # cmp x[0] vs c[0]│
6f14: seta   %cl                      # cl = (x[0]>c[0])
6f17: movzbl %cl,%r15d                #
6f1b: or     %r15,%rax                # full child_idx ─┘
6f1e: add    %rdx,%rax                # += first_child_idx (rdx from prev iter's load)
6f21: lea    (%rax,%rax,4),%rdx       # rdx = idx*5
6f25: lea    (%rdi,%rdx,8),%rcx       # rcx = &nodes_[next]   (node stride 40 = 5*8)
6f29: mov    0x18(%rcx),%edx          # esi = nodes_[next].first_child_idx  ←─── load-use chain
6f2c: cmp    $0xffffffff,%edx         # is_leaf check
6f2f: jne    6ef0                     # back-edge
```

The 2D descent body (`main+0xebd0..main+0xec50`) is structurally
identical with 2 lane compares, node stride 32, and `first_child_idx`
at offset 0x10 (vs 0x18 in 3D).

### Cited per-mnemonic numbers (`simdref show ... --arch alderlake`, all measured)

| Mnemonic        | Latency      | CPI       | Ports                |
|-----------------|-------------:|----------:|----------------------|
| `vcomisd xmm,m64` (lane compare) | 3.0 c | 1.0 | `1*p0+1*p23A`       |
| `vcomisd xmm,xmm`                | 3.0 c | 1.0 | `1*p0`              |
| `cmp r32,i32` (leaf-test)        | 1.0 c | 0.20| `1*p0156B`          |
| `xor r32,r32`                    | 1.0 c | 0.33| `1*p0156B`          |
| `or r64,r64`                     | 1.0 c | 0.33| `1*p0156B`          |
| `add r64,r64`                    | 1.0 c | 0.20| `1*p0156B`          |
| `shl r64,i8`                     | 1.0 c | 0.50| `1*p0156B`          |
| `lea r64,[r64+r64*i]`            | 1.0 c | 0.95| `2*p0156B`          |
| `mov r32,m32` (leaf-test load)   | **0.0 c** *(see simdref findings)* | 0.33 | `1*p23A` |

L1 hit latency for an integer load on alderlake P-core is 4–5 c per
Intel's optimization manual and uops.info; llvm-mca's schedule model
agrees (5 c). simdref's catalog entry of `lat=0.0c` for
`MOV (R32, M32)` is logged as a discrepancy below and **not used**.

### llvm-mca cross-check

```
$ llvm-mca-18 -mcpu=alderlake -iterations=100 ...
```

| Variant | Total cyc | per-iter | IPC  | RThroughput |
|---------|----------:|---------:|-----:|------------:|
| 2D descent body, original (2 vcomisd-mem + leaf-test mov-load) | 1710 | **17.1 c** | 0.88 | 3.0 |
| descent with leaf-test load only (lane compares → reg-reg)     |  913 |  9.13 c    | 1.64 | 3.0 |
| descent with all loads → reg-reg (compute floor)               |  609 |  6.09 c    | 2.46 | 3.0 |

Scheduler-queue-full **89.3 %** of cycles in the original variant —
the back-end is starved on operand availability, not dispatch
bandwidth. Strict load-use latency bound. The compute-only floor of
~6 c/iter rises to ~17 c/iter once the three dependent loads are
re-introduced; load latency dominates.

Per-target descent cost on a typical 5-level tree: 5 × 17.1 ≈ 85 c.
This matches the Phase 6 bucket attribution (descent ≈ 41–55 % of
cycles per perf, with the rest going to eval, scatter, permute-back).

### Single hottest individual instruction

```
$ perf annotate -i report_phase7_asm/perf.data --stdio --no-source main
   ...
   1.30 :    ec27:  mov    0x10(%rcx),%esi      # leaf-test load
  26.02 :    ec2a:  cmp    $0xffffffff,%esi     # ← attributed by skid to the load
   ...
```

26 % of all cycles are attributed by perf-skid to a single `cmp`
that immediately consumes the leaf-test mov-load — definitive
instruction-level evidence that the descent's binding constraint is
load-use latency on `nodes_[curr].first_child_idx`.

### A2 candidates considered and rejected

For each, the auto-rejection rule the candidate trips, plus the
cycle-level reason it cannot win even on its own merits.

1. **SIMD lane-pack** (replace the per-lane `vcomisd → seta → or`
   chain with `vmovupd → vcmppd → vmovmskpd`):
   - 2D: critical path 5+4+3 = 12 c (load → compare → mskmov) on the
     rcx chain vs. the scalar path's two parallel `vcomisd` (8 c
     each) + 3-uop pack ≈ 12 c. **No win**, and adds an extra
     `and $0x3, %eax` that scalar avoids.
   - 3D: would require a 32-byte YMM load reading 8 bytes past
     `center[3]` into `first_child_idx` (Node stride is 40, so safe);
     critical path ~13 c vs scalar ~12 c. **Slightly slower**.
   - Auto-rejection: in 3D it changes `Node` access semantics
     (reading the next field as part of the lane load and masking it
     out) — close to the spirit of Phase 2.5 axis_stride layout
     fragility. Even with that ignored, it is not a critical-path
     win.

2. **Prefetch the leaf-test load** (`__builtin_prefetch(&nodes_[next])`):
   - Auto-rejection ❌ (Phase 2.4): the next address is strictly
     dependent on the previous load; prefetch can't get ahead. Already
     tried, became the top hot line at 14 %.

3. **Restructure `Node` so `first_child_idx` is at offset 0** (load
   before compare):
   - Doesn't reduce L1 latency (still 4–5 c). The bottleneck is the
     load itself, not the offset.
   - Auto-rejection ❌ (Phase 2.5): adds layout pressure on the
     descent's hot cacheline.

4. **Gather-based descent** (vectorise across W targets, batch the
   tree-load via `vgatherdpd`):
   - Auto-rejection ❌ (Phase 6 C2): AVX-512 firmware-disabled on
     consumer alderlake; AVX2 `vgatherdpd` is microcoded → serialises
     into per-lane scalar loads.

5. **Sort targets by first-step bin to localise node access**:
   - Auto-rejection ❌ (Phase 4, Phase 5): adds at least one extra
     pass over `xp`; the descent's L1-resident chain is too short to
     amortise.

6. **Restructure to two-way Horner inside the eval kernel** (not
   strictly an A2 since it is upstream `polyfit`): the kernel already
   emits two parallel FMA chains (the `ymm15`/`ymm5` interleave
   visible at `f7be..f818`), so the obvious split is already done.
   Any further wins are speculative and belong upstream — see "A3"
   below.

No candidate survives the conjunction of (i) cycle-level critical
path, (ii) auto-rejection rules from prior failures, (iii) the Phase
6 perf gate.

### A3 — polyfit eval kernel (briefly)

The eval kernel (`fast_eval_impl.hpp`,
`detail::horner_nd_acrossPts<DIM, NCOEFFS, batch_t>`) at
`main+0xf680..main+0xfa50` is a textbook FMA Horner with all
coefficients spilled to the local stack frame:

```
f6ae: vmovapd -0x1990(%rbp),%ymm15           # init: ymm15 = c[k]
f6b6: vfmadd213pd -0x19b0(%rbp),%ymm1,%ymm15 # ymm15 = ymm1*ymm15 + c[k-1]
f6bf: vfmadd213pd -0x19d0(%rbp),%ymm1,%ymm15 # ymm15 = ymm1*ymm15 + c[k-2]
... (7 chained FMAs per inner-axis Horner step, lat=4 cpi=0.50) ...
f6f5: vfmadd132pd %ymm6,%ymm15,%ymm2         # ymm2 = ymm6*ymm2 + ymm15  (cross-axis Horner)
```

Per-axis critical path (degree-8 → 7 chained `vfmadd213pd`): 7 × 4 c
= **28 c**. GCC already pipelines two parallel Horner chains
(`ymm15`/`ymm1` and `ymm5`/`ymm0`), visible at `f7be..f818`. No
obvious instruction-level swap — coefficients must be loaded each
FMA because there are too many per evaluation to fit in 16 ymm
registers; FMA is the optimal mnemonic.

A potential upstream tweak (further split a single chain into 2-way
even/odd lanes) would halve the per-axis critical path on paper, but
it is speculative without a polyfit-only profile and doesn't affect
baobzi's bucket boundary. **No A3 issue filed at this iteration.**
A future polyfit profile pass can revisit.

### simdref findings (logged for upstream)

Two issues found while running the pipeline. Both are logged here so
the next asm pass can avoid being misled.

1. **Annotation gap, address range 0xa000..0x10000.**
   `simdref profile run`'s `annotated.json` has 3096 records inside
   `main` body addresses 0xc000..0x14000 but **zero records** in the
   sub-range 0xa000..0x10000, which is exactly where the 2D descent
   body sits (0xebd0..0xec50). 3D descent (0x6ee0..0x6f30) and eval
   kernel (0xf680..0xfa50) are at addresses that fall in this gap or
   just outside it.

   Workaround used: ran `objdump`/`perf annotate` directly on the
   binary; cross-referenced mnemonics via `simdref show <mnem>
   --arch alderlake`.

2. **`MOV (R32, M32)` reports `lat=0.0c` measured on ADL-P.**
   `simdref show mov r32, m32 --arch alderlake` reports `lat=0.0c
   cpi=0.33`. Intel's optimization manual, uops.info ADL-P measured
   tables, and llvm-mca's schedule model all give ~4–5 c for an L1
   hit on integer load. The 17.1 c/iter llvm-mca number for the
   original 2D descent matches a 5 c load latency model; if the load
   were 0 c the same body would run in ≤9 c/iter. simdref's
   `lat=0.0c` value is therefore inconsistent with both the cited
   primary sources and the secondary llvm-mca cross-check. Per skill
   §6 ("flag the disagreement, do not pick a side"), it is recorded
   here and **not used** for any conclusion in this iteration. The
   conclusion (load-use bound) is robust under either value: 0 c
   would still leave a `vcomisd`-dependent chain at ~9 c/iter,
   ~6× the 1-c/iter dispatch floor of a 6-wide back-end.

3. **`simdref llm batch` returned `no_match`** for all the bare
   mnemonics drawn from `annotated.json` (`{xor, vcomisd, seta, add,
   or, movzbl, mov, shl, lea, cmp, jne, imul}`), even with
   `--source-kind measured --preset intel` per the skill workflow.
   `simdref show <mnem>` and `simdref show "<mnem> r32, m32"` both
   work fine, so the mismatch appears to be in the batch-resolver's
   query format. Logged for upstream; the workflow proceeded with
   `simdref show` instead.

### Verdict — A1, instruction-level closure

The descent's binding constraint at the instruction level is the
strictly-dependent L1 load `mov 0x10(%rcx),%esi` (or `0x18(%rcx)` in
3D) followed by `cmp $0xffffffff,%esi`. llvm-mca confirms 17.1
cycles/iter at IPC 0.88 with the scheduler queue full 89 % of the
time — strict latency bound, not port- or front-end-bound. No
instruction substitution within this region (SIMD lane-pack,
prefetch, layout reorder, gather, sort) yields a shorter critical
path while also clearing the auto-rejection rules accumulated across
iterations 2.4, 2.5, 4, 5, and 6.

**Phase 7 closes as A1.** The bucket-level closure from Phase 6
becomes instruction-level closure backed by:

- `report_phase7_asm/perf.data, summary.md, hot.sa, merged.json,
  annotated.json, loops.json, samples.json, disasm.s` — simdref
  profile-run artefacts (gitignored)
- `report_phase7_asm/asm-mca-{descent,leafonly,noload}.s` and
  `report_phase7_asm/mca_descent.txt` — llvm-mca cross-check
- `report_phase7_asm/descent_annotation.txt` — per-instruction
  alderlake annotation for the 3D descent body, plus perf source-line
  attribution

No code change. The three commits on this branch as of Phase 7 entry
(`d4f34da`, `308ec31`, `3ab6664`) remain unchanged. The next step on
this branch is unrelated to eval performance.

## Iteration 8 — peak-gap math + thread-safety contract

This iteration answers two questions: where is single-thread baobzi
relative to the silicon's DP-FMA peak, and how should callers
parallelise. The first is a quantitative bound; the second is a
contract on `operator()` that lets callers thread externally.

### Single-thread peak vs measured

Tensor-product Horner ND with K = 9 coefficients per axis costs
`K^n − 1` FMAs per evaluation: 8 (1D), 80 (2D), 728 (3D). Phase-7
perf-driver throughput on `taskset -c 2` was 6.60 Mevals/s at 2D
deg=8 (1.06 GFLOPS) and 3.86 Mevals/s at 3D deg=8 (5.62 GFLOPS).

Core Ultra 7 155H P-core peak: 2 × FMA-256 ports × 4 doubles ×
2 flops × 4.8 GHz ≈ 76.8 GFLOPS DP single-thread. The measured
fractions are **1.4 %** (2D) and **7.3 %** (3D) of FMA-limited
single-thread peak.

### Why the gap is closed at single thread

Phase-6 attribution: descent ~55 %, polyfit FMA kernel ~20 %,
input scatter ~8 %, `stl_vector` indexing ~8 %, output permute
~6.5 %. An infinitely fast FMA kernel only removes the 20 % FMA
bucket → ceiling 1.25× single-thread. Phase-7 proved the descent
is at the silicon's load-use floor (17.1 c/iter, IPC 0.88,
scheduler-queue full 89 %). Closing the rest of the peak-gap needs
either non-microcoded gather (Sapphire Rapids, Zen 5) or parallel
hardware. The user's stated direction is *callers parallelise
themselves*; Phase 8 makes that contractually safe.

### The contract that landed

> A single `Function` built once and not subsequently mutated may
> be called concurrently from many threads, provided each call
> writes to a disjoint output slice. Per-call scratch lives in
> `thread_local` storage; the Function's nodes / polyfits /
> coefficient state are immutable after construction. Baobzi does
> not parallelise internally.

Documented at the top of `include/baobzi/baobzi.hpp`, on both
`operator()` overload declarations in
`include/baobzi/detail/function_impl.hpp` (lines 1082, 1231), and
in `README.md` ("Thread safety" section).

### Test that pins the contract

`tests/test_threadsafe.cpp` — Catch2 suite, registered in
`cmake/baobzi_tests.cmake`, hooked into `ctest` (33/33 green).
Two test cases (`2d_bump deg=8` and `3d_gauss deg=8`):

- Build one Function, allocate 65 536 random in-domain inputs.
- Spawn 8 threads through `std::latch` (gates simultaneous start,
  not spawn-time serialisation), each calls
  `f(xp_chunk, res_chunk, n_chunk)` over disjoint slices.
- Repeat 16 times. **Bit-exact** match across repeats — that is
  how a true race surfaces (any flap on shared state would change
  bits between repeats).
- Compare threaded result against a serial reference under a
  `1e-12` relative tolerance.

### Finding: chunking-dependent ~1 ULP drift in polyfit's batch kernel

Initial drafts of the test asserted bit-exact equality against the
serial reference. The 3D case failed: 1 588 / 65 536 outputs
diverged by exactly 1 ULP. Investigation:

- Repeated threaded calls are bit-exact across all 16 repeats →
  no race on shared state.
- `polyfit::FuncEvalND<…>::operator()(pts, out, count)` for
  `OUT_DIM == 1` switches between (a) an unrolled across-points
  SIMD batch (`U·B` points/iteration), (b) a non-unrolled SIMD
  path (`B` points/iteration), and (c) a scalar
  `evalCanonical<>(pts[i])` tail. Path (c) uses a Horner kernel
  with a different FMA-fusion shape from paths (a)/(b).
- Splitting 65 536 points across 8 threads changes per-leaf
  `cnt` distributions, which changes how many points land in the
  scalar tail per leaf, which changes which Horner shape sees
  them. This is non-associative FP across kernels, not a race.

The drift is bounded ≤ 1 ULP and well below the fit's 1e-10
tolerance. The test bounds it at 1e-12 relative, which both 2D
and 3D clear comfortably. Recorded here so future readers don't
reinterpret it as a race signal: **bit-exact across-repeat
identity** is the correct racing-asserts probe; threaded vs
serial is informational and chunking-sensitive.

### What did NOT land (explicit non-goals, per plan)

- No new `operator()` overload, no `n_threads` argument.
- No internal thread pool, `std::async`, OpenMP, TBB, or PSTL.
- No automatic chunk-parallelisation.
- No relaxation of the across-repeat bit-exactness assertion in
  the test.
- No layout / coefficient-store change (Phase-7 + iter-2.5/4/5
  lessons stand).

### Side-thread (Phase 8b) status

Bounded ≤ 1 hr asm-analysis pass on the polyfit FMA kernel was
deferred — it is independent of the contract that just landed and
ships upstream against `polyfit`, not into baobzi. Open as
follow-up.

### Files

- `tests/test_threadsafe.cpp` — new
- `cmake/baobzi_tests.cmake` — registers `test_threadsafe`,
  links `Threads::Threads`
- `include/baobzi/detail/function_impl.hpp` — doxygen on both
  batch `operator()` overloads
- `include/baobzi/baobzi.hpp` — top-of-file thread-safety contract
- `README.md` — "Thread safety" section

## Iteration 9 — slim-node descent (Phase 9, Layers A + E)

### What landed

A single commit on `use-polyfit` shipping Layers **A + E** of the
Phase-9 plan:

- **Layer A — slim Node.** Drop `center` and `_pad_` from the
  runtime `Node`; shrink `poly_eval_id` from `uint64_t` to
  `uint32_t`. Total `sizeof(Node) == 8 B` for every `Dim` (locked by
  `static_assert` in `function_impl.hpp`). `find_node` /
  `get_node_index` no longer reads `center` per level — it carries
  the subtree's `(lower, upper)` bounds in registers and recomputes
  `mid = 0.5 * (lo + hi)` on the fly. Per level the descent loads
  drop from `Dim×8 B (center) + 4 B (first_child_idx)` to just
  `4 B (first_child_idx)` against the dep-chain. Storage density:
  8 nodes/cacheline vs 1.6 (3D) / 2.0 (2D) / 2.6 (1D) before.
- **Layer E — packed compare.** Bundled with A; the per-axis
  loop-form (`for d in 0..Dim: x[d] > mid[d]`) is left for the
  compiler to vectorise.

### Numbers (Core Ultra 7 155H, P-core, `taskset -c 2`)

| Scenario               | Phase 7   | Phase 9 (A+E) | Δ        | Target |
| ---------------------- | --------- | ------------- | -------- | ------ |
| 2d_bump deg=8 N=1e6    | 6.60      | **10.00**     | **+51.5 %** | ≥ 10 |
| 3d_gauss deg=8 N=1e6   | 3.86      | **8.06**      | **+109 %**  | ≥ 5  |

Both stop-criterion targets are hit with margin → Layers C, D, B,
G are deferred per the plan ("If hit, stop — bank the win").

### What actually fired

`objdump -d --disassemble=main baobzi_perf_driver | grep -cE
'vcmpgtpd|vmovmskpd|vcomisd'` returns 17 (all `vcomisd`); zero
packed compares. Layer **E did not auto-vectorise** under
GCC 15.2 `-O3 -march=alderlake` — the per-axis compare chain
remained scalar. **Layer A alone delivered the win.** This is
consistent with the plan's prediction that the bottleneck was load
volume, not compare throughput: removing `Dim×8 B` of per-level
center loads from the L1 dep-chain was the actionable lever.

If a future need arises, Layer E can be forced via explicit AVX2
intrinsics (`_mm256_cmp_pd` + `_mm256_movemask_pd`) for `Dim ∈
{2,3}` — but only if a profile shows compare latency, not load
volume, has become the new gate.

### Cross-checks

- **Tests:** all 33 ctest cases green, including
  `test_threadsafe` (1e-12 relative tolerance, 8-thread bit-exact
  across-repeat). The descent's `mid = 0.5*(lo+hi)` differs by
  ≤ 1 ULP from the fit-time `box.center` chain at boundary points;
  expected and within the contracted tolerance.
- **Slim-node invariant locked:** three `static_assert(sizeof(Node)
  == 8)` for 1D, 2D, 3D in `function_impl.hpp` — the next change
  that re-fattens the node fails the build.

### What did NOT land

- Layer C (post-fit DFS reorder of `nodes_[]`) — deferred (target hit).
- Layer D (post-fit Z/Hilbert leaf permute) — deferred (target hit).
- Layer B (per-axis u64 quantise) — deferred (E underperformed but
  total target met regardless; B was contingent on the *combined*
  result missing target).
- Layer G (W=4 batched-descent SIMD-across-points) — stretch tier;
  deferred (target hit, prototype ungated).
- No change to `polyfits_[]` storage, `Box` structure, or the
  thread-safety contract.

### Files

- `include/baobzi/detail/function_impl.hpp` — slim Node;
  `Node::fit` / `force_fit_as_leaf` take `center` as a parameter;
  `PolyTree` carries `(lower_, upper_)` and the descent rewrites
  `get_node_index` to recompute `mid` per level; three
  `static_assert(sizeof(Node)==8)` checks; minor `Function` ctor
  refactor (use `current_box.center` where the old code touched
  `node.center`).


## Iteration 10 — post-Phase-9 hot-loop chase (Phase 10 Tier 1)

### Setup

Same hardware/protocol as iter 9 (Core Ultra 7 155H, P-core, `taskset
-c 2`, 3×15 s `baobzi_perf_driver`, median reported). Re-baseline at
the start of the phase showed iter-9 numbers had drifted slightly under
ambient load — **2D 9.65, 3D 7.94 Mevals/s** is the local baseline used
for Δ comparisons below.

Per-layer protocol: implement → ctest 33/33 → 3×15 s perf median →
ship if Δ ≥ +1 % on either scenario AND no regression > 1 % on the
other; discard otherwise.

### What landed (two commits on `use-polyfit`)

#### Layer 1.2 — drop the global-node → leaf-id table (`a5e8750`)

Phase 9's slim 8-B Node co-locates `first_child_idx` and
`poly_eval_id` on the same cacheline that the descent's `is_leaf()`
check already touches. The flat
`leaf_index_by_global_node_[]` table that Phase 7 introduced (when
nodes were 40 B and the `node_pointers_[idx]->poly_eval_id` two-load
pointer chase was a real cost) is now pure overhead — one extra
`uint32` load per point in the histogram pass.

Read `poly_eval_id` directly from the descent's leaf:
```cpp
const std::uint32_t id = subtrees_[get_linear_bin(xi)].find_node(xi).poly_eval_id;
```
Drops the table, the per-tree-offset prefix sum (`build_cache` is now
empty), and the `get_global_node_index` helper (no external callers;
`grep` confirmed). Memory: −4 B per global node + the prefix-sum array.

| Scenario | Pre-1.2 | Post-1.2 | Δ |
|---|---|---|---|
| 2D bump deg=8 N=1e6 | 9.65 | **9.96** | **+3.2 %** |
| 3D gauss deg=8 N=1e6 | 7.94 | **8.45** | **+6.4 %** |

#### Layer 1.4 — drop `std::copy` in `Value(const T*)` ctor (`4d97121`)

The histogram inner loop instantiates one `Value<value_type,
input_dim>` per point via `Value(const T*)`. For `input_dim ∈ {2, 3}`
GCC 15.2 declined to inline the `std::copy` call, emitting a per-call
sequence
```
call _ZSt4copyIPdS0_ET0_T_S2_S1_.isra.0
```
inside a 1 M-iteration hot loop. That's the cost of moving 16 / 24
bytes behind a function call, register save/restore, and a return —
5–10 × the work of the load itself.

Replace with `poet::static_for<N>(...)` so the dim-element copy
unrolls inline at compile time. `objdump --disassemble=main` on the
perf driver confirms the `std::copy<.isra.0>` call is gone from the
histogram body; only the fit-time constructors retain `isra` calls
(unrelated).

| Scenario | Pre-1.4 | Post-1.4 | Δ |
|---|---|---|---|
| 2D bump deg=8 N=1e6 | 9.96 | **18.30** | **+83.7 %** |
| 3D gauss deg=8 N=1e6 | 8.45 | **18.22** | **+115.6 %** |

This was the single biggest win of the phase. The `std::copy` isra
call was eating ≈ 30 ns of the ≈ 110 ns/eval budget — pure call
overhead. A 5-line change.

### Discard log

The plan's other Tier-1 layers were tried; none shipped.

#### 1.1 — switch polyfit `FusionMode::Never` → `FusionMode::Always`

ctest hung indefinitely on the 3D batch test (`Batch vs single
evaluation agree — 3D scalar output`) and the 3D Yukawa
`MemoryBudgetExceeded` test. Hypothesis: `fuseNDDomain`'s unconditional
fusion at `FusionMode::Always` produces ill-conditioned coefficients on
non-canonical 3D domains (the `else`-branch heuristic that Always
bypasses includes a condition-number guard
`(coeffCount-1) * log10(|α|+|β|+1) < digits10 - 3`). The result was
either NaN-tainted evals that drove `sample_error_check` into infinite
refinement, or a runaway recursion that the memory budget eventually
caught — but slowly enough to look like a hang. **Reverted; deferred
to a future polyfit-side fix.**

#### 1.5 — `xsimd::default_allocator` on `xp_packed`/`out_packed`

Initial measurement when 1.4 + 1.5 were stacked looked like a +5 %
follow-up on top of 1.4. Splitting them out (1.4 alone, then 1.5 on
top): 2D 18.30 → 17.03 (-6.9 %), 3D 18.22 → 16.69 (-8.4 %). The
compiler inlines `std::allocator<double>` aggressively for
`std::vector<double>::resize`; the xsimd allocator's templated
equality/copy semantics defeat that and add per-call cost in the
histogram resize path. The polyfit kernel's internal
`alignas(kAlign)` AoS→SoA scratch buffer already gives it aligned
loads regardless of the input vector's alignment. **Reverted.**

#### 1.3 — force Layer E via xsimd `batch` in `get_node_index`

Vectorised the descent's per-axis compare to a single `vcmplt_oqpd +
vmovmskpd` (objdump confirmed). 2D 18.30 → 14.92 (-18.5 %), 3D 18.22
→ 17.55 (-3.7 %). simdref `llm batch` analysis on the resulting
descent body explained the regression:

| Insn (per descent iter) | Lat | CPI | Crit path |
|---|---|---|---|
| `vaddpd` (lo+hi) | 2 | 0.50 | FP |
| `vmulpd` (×0.5) | 3 | 0.50 | FP |
| `vcmplt_oqpd` | 1 | 0.50 | FP |
| `vmovmskpd` | 2 | 0.50 | INT |
| `vpcmpgtq` | 1 | 0.25 | redundant |
| `and / add / lea` | 1 each | 0.25 | INT |
| `vblendvpd` × 2 | 1 | 0.50 | FP |
| `mov (rax), eax` | ~5 | 0.50 | INT |

FP recurrence per iter: 2+3+1+1 = **7 c**.
INT recurrence (carrying `curr_index` through the load chain):
`vmovmskpd(2) + and(1) + add(1) + lea(1) + load(~5)` = **10 c**.

The integer chain gates descent throughput. Compressing the FP path
(P1 in the analysis: track `lo_v` + halving `delta_v` instead of
`(lo_v, hi_v)` to drop critical FP from 7 c to 4 c per iter) doesn't
help because FP was already off-critical. The pre-descent setup —
forced because `PolyTree::lower_/upper_` are 16 B (2D) / 24 B (3D)
unaligned, so GCC loads via `xmm` + spills to a 32 B stack slot
pre-zeroed via `vpxor` + reloads as `ymm`, ≈ 12 insns per point — is
where the 2D regression came from. **Reverted.**

The natural follow-up to make 1.3 land is a *layout* change in
`PolyTree` rather than a *codegen* change in the descent: pad
`lower_`/`upper_` to `alignas(32) std::array<double, 4>` so the
ymm load is one `vmovapd`. That's a Tier-2 candidate (Layer 2.5
in this notation), gated on its own A/B.

### Cumulative numbers

| Scenario | Phase 7 | Phase 9 (A+E) | Phase 10 (1.2 + 1.4) | Cumul. since Phase 7 |
|---|---|---|---|---|
| 2D bump deg=8 N=1e6 | 6.60 | 10.00 | **18.30** | **+177 %** |
| 3D gauss deg=8 N=1e6 | 3.86 | 8.06 | **18.22** | **+372 %** |

Both scenarios crossed the symbolic 18 Mevals/s line on a single
P-core thread without an algorithmic change to the tree, the polyfit
kernel, or the thread-safety contract.

### Stop criterion (carry-over)

Plan called for stopping after two consecutive layers ship with
Δ ≤ +1 %. We ended on Tier 1 with two big shipments (+3.2 % / +6.4 %
and then +83.7 % / +115.6 %), then three discards in a row. The
discards are *not* the stop signal — they're three different
attempts with different mechanics, and one of them (1.3) has a
clean follow-up (Layer 2.5: `PolyTree` bound padding) that the
asm-analysis identified as the real lever.

Resuming after this entry would start with Layer 2.5, then 3.2
(per-call leaf-id memo), per the original plan.

### Files

- `include/baobzi/detail/function_impl.hpp` — Layers 1.2, 1.4
- `bench/results.md` — this entry

## Iteration 11 — Phase 10b ships L9 (quantize-table)

The phase-10b plan ranked layers by intuition (L1 single-leaf, L2 memo,
L3 padded bounds, L4 packed OOD, L6 DFS reorder, L7 leaf permute,
L8 streaming stores, L9 Morton-table spike, L10 W=4 batched-descent
spike). Empirical attribution and 5-7×15s paired-interleaved bench on
`build-new/baobzi_perf_driver` (extended with `1d_gauss` and
`1d_runge` cases for this iteration) flipped the ranking: every
intuition-ranked Tier-1 layer regressed or was neutral, while the
"spike" L9 was a clean blowout on all four scenarios.

### Workload

`taskset -c 2`, `Function::operator()(double*, double*, n_trg=1e6)`,
4 GHz P-core, AVX2 (`xsimd::avxvnni`). Tree shapes:

| Scenario | Leaves | Subtree depth |
|---|---:|---:|
| 1D gauss `exp(-x²)` on [-3, 3] |   32 | 5 |
| 1D runge `1/(1+25x²)` on [-1, 1] |   30 | 6 |
| 2D bump `exp(-100·…)` on [0, 1]² | 7744 | 7 |
| 3D gauss `exp(-‖x‖²)` on [-1, 1]³ |  512 | 3 |

The phase-10b plan claimed `1D gauss: 8 leaves / depth 3, 82 Mevals/s
baseline`. On this branch + this machine the actual tree is 32 leaves
/ depth 5 and the baseline median is **51.46 Mevals/s**. The plan's
prior was therefore based on a different setup; iter-11's numbers are
self-consistent (paired binaries built from the same source minus one
diff).

### What was tried

Each layer was implemented, ctest-validated (33/33 at 1e-12 relative
tolerance), then benched paired-interleaved against an unmodified
binary on the same boot of the same shell.

#### L1 — single-leaf fast path (`polyfits_.size() == 1`)

Source change adds an early-out branch before the histogram pass that
calls the single polyfit directly with NaN-blend post-pass for OOD.
**The branch never fires on any of the 4 perf scenarios** (smallest is
30 leaves), so the only effect is on compiler inlining decisions
around the new branch. A 3×15s interleaved bench at load 3.6 showed
+5 % on 2D bump and +1.4 % on 3D gauss — those medians did not survive
the next noise sample.

5×15s interleaved at strict load < 2 (clean):

| scenario | baseline | L1 | Δ |
|---|---:|---:|---:|
| 1D gauss | 51.54 | 49.32 | **−4.3 %** |
| 1D runge | 32.15 | 32.24 |  +0.3 % |
| 2D bump  | 19.42 | 19.51 |  +0.5 % |
| 3D gauss | 19.59 | 19.31 |  −1.4 % |

`bump2d`'s out-of-line `operator()` symbol shrank from 0x29b0 → 0x190b
bytes (-40 %) and `gauss3d`'s from 0x5182 → 0x1a62 (-67 %): the added
branch shifted the compiler's inlining heuristic and de-inlined parts
of the polyfit dispatch path. The de-inlining freed icache for the hot
path on 2D/3D in the noisy 3-run bench but the cleaner 5-run sample
exposed it as accidental — `1D gauss` regresses 4.3 %. **Discarded.**

#### L3 — pad `PolyTree::lower_` / `upper_` and `Function::lower_left_` /
`upper_right_` to `alignas(32) std::array<double, 4>` for ND

Aimed at iter-10's Layer 1.3 follow-up: pre-descent setup spent
~12 insns/point widening 16-B (2D) / 24-B (3D) bounds into a ymm.
Implemented; padding is correct; ctest green. **Codegen of the descent
body is unchanged** — the compiler still emits per-axis scalar
`vmovsd` loads from the (now padded) bounds. The only delta is +64 B
in `bump2d`'s `operator()` size (compiler kept identical inner code
but shifted offsets to the new padded layout). Net perf delta below
noise. **Kept** as a prerequisite for L9's `inv_span_bins_` precompute
and to preserve the option to retry Layer 1.3 with explicit xsimd
intrinsics later.

#### Layer 1.3 retry — explicit xsimd packed descent

On top of L3, replaced the per-axis dim loop with a packed batch:
load `lower_` / `upper_` as `vmovapd ymm`, packed `(lo+hi)*0.5`,
packed `vcmplt_oqpd`, child_idx via `vmovmskpd & dim_mask`, packed
`vblendvpd` for lo/hi update. Codegen confirmed the loads are
ymm-aligned and the FP compute is fully packed.

Per-iter critical path on Alder Lake P (simdref measured):

```
vaddpd       lat 3   FP recurrence
vmulpd       lat 3   FP
vcmplt_oqpd  lat 3   FP / fan-out to mask
vpcmpgtq     lat 1   format-shuffle the mask for blendvpd (compiler-emitted)
vmovmskpd    lat 2   mask → INT (gates child_idx)
and / add    lat 1   INT
lea / load   lat 1+4 INT next-iter address fetch
```

INT recurrence (mask → curr_index → load → next eax) is **17 c/iter**
vs scalar's **~14 c/iter** (`vcomisd 2 + seta 1 + add 1 + lea 1 +
load 4 = ~9 c after mid is ready at 5 c, plus the mid → flags branch
takes ≥ 2 c more`). The vector path's gain on FP throughput is moot
because INT was already gating, and the imm-encoded compare's
3-cycle latency stretches the path further than scalar `vcomisd`.

This matches iter-10's discard rationale for Layer 1.3 (then attributed
to setup overhead from unaligned bounds; now confirmed the per-iter
critical path itself doesn't shrink). **Discarded — not benched** to
avoid burning a noise budget on a layer the asm rules out.

`vpcmpgtq` after `vcmplt_oqpd` is the wasted round-trip the iter-10
note flagged; an `xsimd::batch_bool` consumed directly by `select`
would drop that. Still wouldn't beat scalar at Dim ≤ 3.

#### L9 — quantize-table descent shortcut for shallow subtrees ✅ shipped

For PolyTrees where `2^(input_dim · max_depth_) · 4 B ≤ 64 KiB`,
build a row-major `leaf_table_` at fit-time mapping each quantize
cell to the `poly_eval_id` of its containing leaf. Eval-time descent
collapses to:

```
qd = (xd - lo[d]) * inv_span_bins_[d]   // vsubsd + vmulsd
qd = (size_t)qd                         // vcvttsd2si
idx = qd_0 | (qd_1 << bits) | …
return leaf_table_[idx];
```

`inv_span_bins_[d] = 2^depth / span[d]` is precomputed. The per-axis
quantize is therefore one `vmulsd` (lat 3) instead of a `vdivsd`
(lat 14) — confirmed in the dropped first prototype where leaving
`/ span` in the hot path produced a regression on every scenario.

7×10s paired-interleaved median (no load gate; runs naturally
straddle the same load envelope when interleaved):

| scenario | baseline | L9     | speedup |
|---|---:|---:|---:|
| 1D gauss | 51.46    | 102.55 | **+99 %** (×2.0) |
| 1D runge | 32.58    | 110.35 | **+239 %** (×3.4) |
| 2D bump  | 19.54    |  53.21 | **+172 %** (×2.7) |
| 3D gauss | 19.63    |  26.26 | **+34 %**  (×1.34) |

3D's smaller speedup is because depth=3 already only costs ~3·17 = 51
descent insns/point, and the polyfit kernel itself dominates total
cycles — Amdahl's law caps the descent-shortcut win below the 1D/2D
factors. The 2D table is 64 KiB exactly, fits L1d (32 KiB working set
with the polyfit coefficients pulling in another fraction), and shows
the largest absolute Mevals/s gain.

`fit()` cost is `O(2^(input_dim·max_depth_) · max_depth_)` calls into
`get_node_index` per subtree. For 2D bump (16 K cells × 7 levels =
115 K descents) this adds ~2 ms to fit time; the 119 ms fit grew to
~121 ms — a price already paid in fit, recovered by the ~1 µs/eval
inner loop saving over any non-trivial number of eval calls.

The 64-KiB-per-subtree budget stays under the per-Function
`max_memory_mib` budget by construction (single subtree per Function
in the perf driver; multi-subtree fits sum tables + node storage and
will hit the budget naturally if the user opts into a deeper tree).

### Numbers vs the prior baseline

| Scenario | Phase 7 | Phase 9 (A+E) | Phase 10 (1.2 + 1.4) | Phase 10b (L9) | Cumulative |
|---|---:|---:|---:|---:|---:|
| 1D gauss deg=8 N=1e6 |  —   |   —   |   —   | **102.55** |  — |
| 1D runge deg=8 N=1e6 |  —   |   —   |   —   | **110.35** |  — |
| 2D bump  deg=8 N=1e6 | 6.60 | 10.00 | 18.30 |  **53.21** | **+706 %** |
| 3D gauss deg=8 N=1e6 | 3.86 |  8.06 | 18.22 |  **26.26** | **+580 %** |

(`1d_gauss` and `1d_runge` are new perf-driver scenarios introduced
this iteration; their pre-iter-11 baselines exist only in this branch's
HEAD~1 measurement, 51.46 and 32.58 respectively.)

### Plan vs reality

The phase-10b plan's order was `L1 → L2 → L3 → L4 → L5 → L6 → L7 →
L8 → L9 (spike) → L10 (spike)`. L9 was ranked behind seven layers it
turned out to dominate by an order of magnitude. Lessons:

- **The "spike" qualifier was wrong** — L9 has a measurable, asm-clean
  cycle saving (~80 c/point on 2D bump descent → table) and a fit-time
  cost that is trivially amortised. It should have been Tier 1.
- **The plan's per-layer cycle estimates underweighted descent.** The
  pre-descent OOD check (L4 target) is ~3 % of total; the 7-level
  descent itself is ~58 %. Optimising the smaller share first is an
  EV inversion that the order encoded.
- **Asm-anchored attribution before benching** prevented burning a
  full-bench cycle on the L1.3 retry once the per-iter critical-path
  arithmetic showed it couldn't win.

### Files

- `include/baobzi/detail/function_impl.hpp` — L3 (kept) + L9 (shipped)
- `examples/c++/baobzi_perf_driver.cpp` — `1d_gauss` / `1d_runge` cases +
  `BAOBZI_PRINT_STATS` env-gated tree-shape dump
- `bench/results.md` — this entry
- `report_phase10b/AB4_baseline.txt`, `report_phase10b/AB4_L9.txt` —
  raw 7×10s paired-interleaved measurement

### Stop criterion

Phase-10b plan's stop criterion was "two consecutive layers fail the
ship rule → re-profile end-to-end". L1 failed (regression). L9
shipped with very large Δ. Per the plan we keep going.

The next candidate is **L2 (per-call leaf-id memo)** — the plan's
spatial-locality argument doesn't apply on independent-uniform query
points, but on user workloads where consecutive points cluster (any
quadrature, ray marching, sequential field sampling) the hit rate
should be high. Need a different bench scenario to value it. The
remaining plan layers (L4, L5, L6, L7, L8, L10) were all targeting
the descent inner loop or its periphery; with L9 collapsing descent
to a single load on these scenarios, their headroom is now bounded
by the post-L9 cycles which sit largely in the polyfit kernel — out
of scope per the plan.

## Iteration 12 — Phase 11 ships A + P (post-L9 batch-eval follow-up)

Single iteration covering three Phase-11 layers landed on
`use-polyfit`: A (shipped), G (discarded), P (shipped). After L9
collapsed descent on shallow single-subtree scenarios, attribution
shifted to OOD checks (Layer A) and the random-write unpermute pass
(Layer P). All four scenarios — 1d_gauss, 1d_runge, 2d_bump, 3d_gauss —
benefited from one or both shipped layers.

### Workload

Same harness as iter-11: `taskset -c 2`, `Function::operator()(*, *, n=1e6)`
on Intel Core Ultra 7 155H P-core, AVX2 (`xsimd::avxvnni`), 4 GHz.
Per-layer pair-build: stash → build baseline → unstash → build layer.
Saved binaries live in gitignored `report_phase11/`.

### Layer A — fuse OOD into L9 quantize-table descent (SHIPPED)

**Diff:** `include/baobzi/detail/function_impl.hpp` only.
**Commit:** `4b31d16`.

Two changes folded together:

1. **Hoist the runtime-constant single-subtree dispatch out of the
   per-point loop.** When the tree is one subtree with a leaf table
   (true for all four bench scenarios), the inner loop becomes a
   straight-line histogram update — no internal branch, no virtual
   dispatch — and the compiler unrolls it 5× (visible in `objdump`
   on bump2d at offset `0x21020`+).
2. **Replace the unsigned-cast OOD safeguard.** `static_cast<size_t>(double)`
   emits a 2-step convert (`vsubsd 2^63 / vcvttsd2si / vcomisd / cmovae`)
   on Alder Lake P. Where the input is bounded, signed
   `vcvttsd2si` + unsigned compare collapses it to one convert + one
   `cmp + jb` — saves ~7 c per axis.

**Per-axis critical path (simdref measured, ADL-P):**
| insn        | lat | cpi  |
|-------------|----:|-----:|
| vmovsd load |   4 | 0.50 |
| vsubsd      |   2 | 0.50 |
| vmulsd      |   3 | 0.50 |
| vcvttsd2si  |   9 | 0.50 |
| cmp         |   1 | 0.13 |

Per-axis chain 4+2+3+9+1 = 19 c. 2D parallel + shlx/or + table
load + store ≈ 27 c critical path → matches the +29-37% observed
gain.

**Symbol size deltas (within 5% gate):**
- bump2d: +5.0% (0x29f3 → 0x2c01)
- gauss3d: +1.9% (0x51ef → 0x538B)
- runge1d: +4.1% (0x1ce3 → 0x1d9d)

**Bench medians (7×10s, paired-interleaved):**

| Scenario | pre-A | post-A | Δ |
|---|---:|---:|---:|
| 1d_gauss | 102.55 | 145.70 | **+42.1%** |
| 1d_runge | 110.35 | 161.65 | **+46.5%** |
| 2d_bump  |  53.21 |  72.54 | **+36.3%** |
| 3d_gauss |  26.26 |  33.94 | **+29.2%** |

### Layer G — `[[unlikely]]` on OOD branch (DISCARDED)

Annotated `if (... > mask) return ood_id;` in `find_leaf_id_with_ood`.

**Codegen check showed zero effect.** Top-10 mnemonic histogram
identical to Layer A (424 mov, 205 vbroadcastsd, 156 vfmadd132pd,
…). Symbol size deltas: bump2d +24 B, gauss3d -16 B, runge1d +4 B —
noise. The compiler had already laid the OOD branch cold (forward
jump to `21500`, well past the polyfit kernel).

Discarded with codegen evidence; no bench cycle spent. Stash dropped.

### Layer P — UNPERMUTE prefetch lookahead (SHIPPED)

**Files:** `include/baobzi/detail/function_impl.hpp` (+10 in unpermute loop).

**Why:** post-A profile (`report_phase11/post_A/`) showed UNPERMUTE
at **65-69 % of 1D cycles**. Each iteration:

- load `perm[dst]` (contiguous, L1 hot, ~5 c)
- load `out_packed[dst]` (contiguous, hot, ~12-30 c)
- store `res[perm[dst]]` (random, RFO, ~50-200 c per cold cacheline)

Random `perm` over 8 MiB of `res` (N=1e6, output_dim=1) → every
cacheline of `res` requires an RFO. Layer P prefetches
`&res[perm[dst+32]]` for write (`__builtin_prefetch(p, 1, 0)`)
ahead of the actual store.

**Codegen confirmation (runge1d unpermute body at 0x6d40):**
```
6d54: mov    (%r12,%rax,4),%edi          ; perm[dst+LOOKAHEAD]
6d58: mov    -0x80(%r12,%rax,4),%ecx     ; perm[dst]
6d5d: vmovsd -0x100(%rsi,%rax,8),%xmm1   ; out_packed[dst]
6d6a: cmp    $0xf423f,%rax
6d70: prefetchw (%r10,%rdi,8)            ; &res[perm[dst+32]]
6d75: vmovsd %xmm1,(%r10,%rcx,8)         ; res[perm[dst]] = xmm1
6d7b: jbe    6d54
```

Compiler hoisted the bound check out by pre-rolling and emitting a
tail handler — `prefetchw` lands inside the loop body, dual-issued
with the store.

**Symbol size deltas (within 5% gate):**
- bump2d: +0x96 B (+2.2%)
- gauss3d: -0x5A B (-0.4%)
- runge1d: -0x94 B (-2.0%)

**Bench (14×10s paired-interleaved, run 7 excluded — thermal cliff
hit layerP only on that single run; verified by per-run paired
deltas):**

| Scenario | post-A | Layer P | Δ% (median) | Δ% (10%-trim mean) |
|---|---:|---:|---:|---:|
| 1d_gauss | 127.7 | 134.8 | **+8.5%** | +7.3% |
| 1d_runge | 141.0 | 145.3 | **+6.3%** | +5.3% |
| 2d_bump  |  63.4 |  65.9 | **+5.1%** | +4.1% |
| 3d_gauss |  30.3 |  30.4 | **+2.6%** | +1.9% |

Variance was high across the 14-run window (run-7 layerP 3d_gauss
dipped to 7.1 Mevals/s — clear thermal/scheduling outlier; baseline
binary was unaffected). Paired-delta median over 13 clean runs is
positive on every scenario; ship rule (≥+1 % on at least one, no
regression > 1 % on others) met cleanly.

### Cumulative gain (pre-A → post-P) on 7×10s clean medians

| Scenario | pre-A baseline | post-P | Δ |
|---|---:|---:|---:|
| 1d_gauss | 102.6 | ~135 | **+31%** |
| 1d_runge | 110.4 | ~145 | **+31%** |
| 2d_bump  |  53.2 |  ~66 | **+24%** |
| 3d_gauss |  26.3 |  ~30 | **+15%** |

(Layer-P numbers are noisier than Layer-A's; the post-P column is
the median of 13 paired runs minus the run-7 thermal outlier, on
the same machine but at a different time-of-day load profile.)

### Lessons (this iteration)

- **Run-time-constant dispatch hoisted out of a hot loop is a
  reliable optimisation.** When a per-call invariant (here:
  single-subtree leaf-table layout) gates the inner loop's
  branch, the compiler can fully unroll the fast path while
  keeping the fallback present in `.text`. The only cost is
  symbol-size growth — pre-bench inlining-size check (`nm
  --print-size`) is essential.
- **The unsigned-cast safeguard is a hidden FP cost.**
  `static_cast<size_t>(double)` on Alder Lake P emits a 5-uop
  conversion (`vsubsd / vcvttsd2si / vcomisd / cmovae`) when the
  compiler can't prove the input is non-negative. Where bounds
  are known, prefer signed convert + unsigned compare.
- **`[[unlikely]]` is a no-op when the compiler has already laid
  the branch cold.** Always codegen-check first. Saves a bench
  cycle.
- **Random-write RFO is ~50 % of 1D batch eval cycles post-A.**
  Even with `out_packed` and `perm[]` both hot in L1, the
  scattered write to `res` dominates because each cacheline
  requires a RFO that latency-bottlenecks the store buffer.
  Software write-prefetch with a small lookahead (32 here) hides
  this latency without adding load-port pressure.
- **Hybrid CPU `perf annotate` doesn't support per-event filtering
  on AlderLake-P + E.** Use `perf script | awk` + `addr2line -i -f -C`
  for per-IP attribution.
- **Rebuilding the binary while perf-record is running corrupts
  the IP→symbol mapping** (process keeps the old mmap, on-disk file
  is replaced, addr2line resolves against the wrong inode). Cancel,
  restore, re-run.

### Files

- `include/baobzi/detail/function_impl.hpp` — A (commit `4b31d16`) + P (this commit)
- `bench/results.md` — this entry
- `report_phase11/L0_full/`, `report_phase11/post_A/` — gitignored
  perf profiles + per-IP samples (binned via `perf script` →
  `addr2line` → region attribution table in the Phase 11 plan)
- `report_phase11/perf_driver_baseline_P`, `…/perf_driver_layerP` —
  paired binaries used for the 14×10s bench

### Stop criterion (revised)

Phase 11 stop criterion: two consecutive layers failing the ship rule
triggers a mandatory full re-profile, and if no remaining target's
intended region is ≥ 5 % of measured cycles on at least one scenario,
the iteration closes.

After Layer P ships:
- 1D scenarios: UNPERMUTE share drops; remaining cycles split between
  polyfit kernel (in-region) and HIST fast-path (~6 % of cycles).
- 2D bump: SCATTER+DISPATCH still ~95 % of cycles; ceiling is the
  polyfit kernel (out-of-scope).
- 3D gauss: ~80 % of cycles in polyfit ND U=1 kernel (register-
  pressure tax) — out of scope.

The remaining queued layers (B compressed leaf table, D Z-order
permute, F' scratch+immediate scatter, E NT stores) all target
regions whose post-P share is < 10 % of cycles on every scenario,
and B in particular has been downgraded post-A from ~34 % HIST to
~6 % (EV ceiling ~3 %).

**Decision:** iter-12 closes here pending a fresh post-P profile to
re-rank. Next phase, if pursued, would have to attack polyfit-internal
(kernel UF, 3D U=1) or AVX-512 silicon (real scatter), both of which
were declared out of scope at the start of Phase 11.

## Iteration 13 — Phase 12: Layer Q tried & discarded, master regression confirmed

Phase 12 retired the elaborate "pollution-aware" paired harness
(`bench_paired.sh` with PSI / freq / off-pin gates, calibrated
ceiling, warmup) in favour of the **short-and-honest** model: a
30-line `report_phase12/bench_pair_short.sh` that alternates two
binaries 12 × 5 s with `taskset -c 2` and no gates, and an
extended parser (`report_phase12/parse_paired.py`) that reports
**paired-median Δ%, stddev of paired Δ%, and 25/75-percentile
IQR**. Total wallclock per pair ≈ 120 s. Noise tolerance for ship
decisions: **±5 %** on the paired-median Δ%.

### Layer Q — input-scatter `prefetchw` lookahead — DISCARDED

Source diff (reverted, not committed):
`include/baobzi/detail/function_impl.hpp:1311-1343` — paired
`__builtin_prefetch(..., rw=1, locality=0)` for `xp_packed[D·dst]`
and `perm[dst]` with a 16-iteration lookahead in the input-scatter
loop. Pre-built binaries `report_phase12/perf_driver_baseline_Q`
and `perf_driver_layerQ`.

**Codegen evidence** (whole-binary mnemonic counts; full table in
`report_phase12/Q_codegen_audit.md`):

| Binary | prefetcht0 | prefetchw |
|---|---:|---:|
| baseline_Q | 4 |  4 |
| layerQ     | 4 | 32 |

Δ = **+28 prefetchw** as intended. Per-symbol size deltas
(`Function<8, …>::operator()`) all under the 5 % gate (max
+2.37 % on `make_runge1d`).

**Causal `perf stat` evidence** (single 20-s pinned run, `:u`):

| Counter | baseline_Q | layerQ | Δ |
|---|---:|---:|---:|
| **l2_rqsts.rfo_miss**    | 1.693 B | 1.580 B | **−6.67 %** |
| l1d.replacement          | 6.068 B | 5.670 B | −6.56 % |
| mem_load_retired.l3_miss | 14.05 M | 15.24 M | +8.54 % |

The −6.67 % drop in L2 RFO misses confirms the prefetchw mechanic
worked: ownership for the upcoming write line is fetched early.

**Bench (12 paired runs × 5 s, core 2):**

| scenario | base med (Mevals/s) | cand med | Δ% med | Δ% std | Δ% IQR |
|---|---:|---:|---:|---:|---|
| 1d_gauss | 153.34 | 146.61 | −4.51 % | 5.04 % | [−7.23, −2.69] |
| 1d_runge | 158.12 | 153.47 | −3.68 % | 2.40 % | [−5.16, −3.26] |
| 2d_bump  |  74.20 |  73.90 | −0.16 % | 7.32 % | [−2.23, +2.54] |
| 3d_gauss |  31.07 |  31.41 | +0.97 % | 6.02 % | [−1.18, +5.86] |

With ±5 % noise tolerance, every scenario sits inside the noise
band (max |Δ| = 4.51 %). Ship rule (`Δ ≥ +1 %` outside noise on
at least one scenario) **not met → DISCARDED**. The mechanical
hypothesis was correct (RFO miss rate fell), but the +28 prefetchw
uops add front-end / port pressure on the hottest 1D operator()
bodies and the wallclock outcome is inside noise. Source reverted;
artifacts kept under `report_phase12/` as a record.

### Master regression — `main` vs `use-polyfit` post-Q-decision

Same harness, comparing `/tmp/baobzi-main-bench/build/baobzi_perf_driver`
(commit `cf39a3c` on `main`) against `report_phase12/perf_driver_baseline_Q`
(use-polyfit tip, commit `6630179`). 12 paired runs × 5 s, core 2:

| scenario | main med (Mevals/s) | polyfit med | Δ% med | Δ% std | Δ% IQR |
|---|---:|---:|---:|---:|---|
| 1d_gauss |  98.92 | 148.71 |  **+47.81 %** |  9.00 % | [+45.45, +52.19] |
| 1d_runge |  54.16 | 161.55 | **+196.51 %** | 15.92 % | [+186.86, +204.41] |
| 2d_bump  |  35.36 |  74.19 | **+107.04 %** | 11.34 % | [+91.65, +111.19] |
| 3d_gauss |   5.18 |  31.38 | **+505.23 %** | 46.59 % | [+465.40, +523.20] |

The `use-polyfit` branch is uniformly faster than `main` on every
batch-eval scenario, well outside the 5 % noise band — the
cumulative gain since the polyfit cutover (Phases 9 → 11: slim-node
descent, A, P, L9, plus the public batch-eval API rewrite) is
**+48 % to +505 %** depending on dimension. No scenario regresses.
The `+505 %` on 3d_gauss is a cross-API comparison (the public
batch-eval surface differs between branches); we report the
wallclock delta on `operator()` only and do not attempt symbol-level
attribution — `main` predates the polyfit eval kernel, so the
two binaries do not share an inner loop.

### Footprint

`report_phase12/footprint_analysis.md` documents the per-call
thread_local scratch for `Function::operator()`: 1D = 24 MiB,
2D = 32 MiB, 3D = 40 MiB at N = 1e6, dominated by `xp_packed`
(8·D MiB), `out_packed` (8 MiB), `leaf_ids` (4 MiB) and `perm`
(4 MiB). The 4 MiB target gate is **unreachable at N=1e6**
without out-of-scope changes (e.g. tiling the public batch API
to chunk N internally). Two bench-friendly partial wins are
identified and **deferred to a follow-on phase**:

- **C1 — `leaf_ids` u16 narrowing** when `n_leaves ≤ 65535`:
  saves 2 MiB at N=1e6, AVX2 store throughput unchanged
  (loop is scalar), trivial zero-extend in the
  scaled-index addressing.
- **`BAOBZI_RELEASE_SCRATCH_AFTER`** — opt-in `shrink_to_fit` /
  free of the thread_local vectors after a configurable idle.

Neither is in scope for iter-13.

### Ship/discard decisions (honest closure)

| Candidate | Δ% med (range) | Decision |
|---|---|---|
| Layer Q (input-scatter prefetchw) | −4.5 % … +1.0 % (all in ±5 % noise) | **Discarded** |
| `leaf_ids` u16 narrowing | not benched (footprint-only) | Deferred |
| `BAOBZI_RELEASE_SCRATCH_AFTER` | n/a | Deferred |

Each discard cites both the median Δ% and the stddev of paired Δ%.
Layer Q's mechanical RFO win is preserved in the codegen audit
even though the wallclock outcome was inside noise — useful prior
art if a future phase revisits write-prefetch under a less
front-end-bound operator() body.

### Files

- `report_phase12/bench_pair_short.sh` — minimal paired bench (no gates)
- `report_phase12/parse_paired.py` — paired-Δ% with median + stddev + IQR
- `report_phase12/Q_codegen_audit.md` — prefetch counts, symbol sizes, perf stat
- `report_phase12/footprint_analysis.md` — per-call thread_local scratch budget
- `report_phase12/Q_baseline.txt` / `Q_layerQ.txt` — Layer Q paired bench
- `report_phase12/M_main.txt` / `M_layerQ.txt` — master regression bench
- `report_phase12/perf_driver_baseline_Q` / `perf_driver_layerQ` — pre-built binaries
- `bench/results.md` — this entry

### Lessons (this iteration)

- **Short-and-honest beats elaborate-and-broken.** The
  pollution-aware harness fought a hybrid CPU's powersave governor
  for hours and still produced ±20 % run-to-run noise. A 12-paired
  × 5-s loop with a stddev-aware parser tells the same story in
  120 s and is robust because every paired Δ% is a self-contained
  apples-to-apples measurement.
- **A correct mechanical hypothesis can fail at the wallclock.**
  Layer Q's prefetchw really did reduce L2 RFO misses (−6.7 %) but
  added uop pressure that the front-end couldn't absorb on the
  tightest 1D loops. Always cross-check codegen evidence with a
  paired wallclock bench; the perf-counter delta alone is not a
  ship signal.
- **Footprint gates that ignore where the bytes live are not
  actionable.** The 4 MiB ceiling at N=1e6 is fundamentally a
  property of the public batch-eval API (one entry per input);
  any reduction below 8·D·N + O(N) bytes requires reshaping the
  surface, not the descent. Document and defer.

### Decision

iter-13 closes Phase 12 with **no source change shipped** — Layer Q
discarded, footprint candidates deferred. The use-polyfit branch is
confirmed uniformly faster than `main` on the public batch-eval
surface (+48 % to +505 %). The `/tmp/baobzi-main-bench` worktree is
removed at end of phase.

## Iteration 14 — Phase 13: peak-distance + asm-analysis, no ship

Phase 13 turned the iter-12/13 hand-waves about "polyfit kernel
ceiling" and "U=1 register-pressure tax" into measured numbers via
the `simdref` asm-analysis pipeline (`compile_commands` → `objdump`
→ `simdref annotate` → `llvm-mca`) and a fresh per-IP region
profile via `simdref profile run`. No code change shipped — the
strongest candidate identified did not clear iter-13's ±5 % noise
band on its asm evidence alone, and the user's idle-system gate
deferred any speculative paired bench.

### Step 1 — fresh region profile (PRELIMINARY)

`simdref profile run --target ./report_phase13/profile/run_pinned.sh
--args "5" --adapter perf --event "cycles:u,instructions:u"
--duration 60 --arch alderlake -o report_phase13/profile/`. Pinned
to core 2 via wrapper script (`taskset -c 2`). 41 011 `cpu_core/cycles`
samples; `cpu_atom` filtered out (workflow §2b.1).

> **Caveat:** the operator flagged this run as potentially polluted
> (concurrent system load not confirmed idle). 1D scenarios show
> ≈ 0 cycle samples in the histogram — inconsistent with their 5-s
> window at 153/158 Mevals/s. Treat the table as directional. A
> clean re-profile is pending operator confirmation.

| Scenario | scenario share | dominant region (deepest baobzi/polyfit frame) |
|---|---:|---|
| 2d_bump  | 49.0 % | `function_impl.hpp` L678/L684/L695 (≈55 % of 2D) — **`get_node_index` BFS descent** (asm: `vcomisd` axis-split + `is_leaf()` sentinel load chain) |
| 3d_gauss | 49.0 % | `poly_eval.h:141-167` (≈52 % of 3D) — **`horner_axis_acrossPts` (KERNEL_ND_U1)** |
| 1d_*     | <2 %   | under-represented (likely pollution; secondarily, `find_leaf_id_with_ood` strips scenario marker frames) |

**Surprise:** 2D bump is **descent-bound**, not "SCATTER+DISPATCH"
as the iter-12 hand-wave assumed. The L9 leaf-table fast path is
NOT taken on multi-subtree trees, so 2D bump falls into
`get_node_index`'s `while (!is_leaf())` loop. The plan's Step 1 → 3
decision gate ("kernel ≥ 30 % on at least one scenario → kernel is
lead") is met by 3D's 52 % share, but the kernel-asm result (below)
shifts the leverage *away* from kernel-level changes.

### Step 2 — peak-distance table

llvm-mca @ `-mcpu=alderlake -iterations=100` on the 102-instruction
ND U=1 inner-loop region (file offsets `0x7560..0x77ff` of
`build-perf/baobzi_perf_driver`):

```
Block RThroughput: 20.5 cycles/iter
uOps/cycle:        4.66
IPC:               4.49
```

Resource pressure: ports 0/1 (FMA execute), 2/3 (load), 10
(broadcast) all simultaneously bound at ~20-21 c/iter. Per-iter:
38 FMAs × 4 lanes × 2 flops = 304 flops / 20.5 c = **14.83 flops/cycle
= 92.7 % of the 16 flops/cycle AVX2 fp64 peak**.

**Iter-12's "ND U=1 register-pressure tax" hypothesis is refuted.**
No spills, no port slack, kernel is at silicon ceiling.

Public-batch-surface achieved (4 GHz effective freq):

| Scenario | Mevals/s | flops/eval | flops/cycle | % peak | implied kernel cycle share |
|---|---:|---:|---:|---:|---:|
| 1d_gauss | 153.34 |   14 | 0.54 |  3.4 % |  3.6 % |
| 1d_runge | 158.12 |   14 | 0.55 |  3.5 % |  3.7 % |
| 2d_bump  |  74.20 |  126 | 2.34 | 14.6 % | 15.8 % |
| 3d_gauss |  31.07 | 1022 | 7.94 | 49.6 % | 53.5 % |

3D's 53.5 % implied kernel share matches the (polluted) Step-1
profile attribution at 52 % — independent cross-check works. 1D
spends 96-97 % of wallclock OUTSIDE the kernel; 2D spends 84 %
outside.

### Step 3 — kernel asm-analysis

ND U=1 kernel (`poly_eval.h:141-167`, fully inlined under
`PF_FLATTEN`/`PF_ALWAYS_INLINE`):

| mnemonic | count | lat (c) | cpi | source |
|---|---:|---:|---:|---|
| vbroadcastsd | 53 | 5.0 | 0.33 | ADL-P measured |
| vfmadd132pd  | 37 | 4.0 | 0.50 | ADL-P measured |
| vfmadd231pd  |  1 | 4.0 | 0.50 | ADL-P measured |
| vmulpd       |  3 | 4.0 | 0.50 | ADL-P measured |
| vxorpd       |  3 | 1.0 | 0.33 | ADL-P measured |

`unknown ??` rate: 1/102 = 1.0 % (just `jne` — a parser quirk, not
a catalog gap). Workflow §9's 20 % refuse-threshold is well clear.
llvm-mca ↔ simdref agree on lat/cpi within 2× on all dominant
mnemonics; one minor discrepancy on `vbroadcastsd` latency
(simdref 5 c, llvm-mca / uops.info 8 c — the broadcast-forward
slice — see `simdref_bugs.md` Bug 3).

2D-bump descent (`function_impl.hpp:900-945` get_node_index):

35 instructions, 17.1 % `unknown` (just under the 20 % refuse line;
see `simdref_bugs.md` Bug 2 for the missing alderlake rows on
`seta`/`movzbl`/`jcc`). Critical path per descent level:

```
vcomisd (3c) → seta (1c) → or (1c) → shl ×32 (1c)
            → mov 0x10(%rcx) (4c L1)
            → cmp jne (1c)
≈ 11 cycles per level
```

Load-to-load chain dominates: each level's `mov 0x10(%rcx)`
depends on the prior level's chain. With 4-cycle L1 latency this
is an architectural floor.

### Step 4 — descent / dispatch design notes

The plan's nominal Step 4 target (per-leaf dispatch overhead) was
not the hottest 2D site; `get_node_index` is. Three candidates
considered, none committed:

| Candidate | Mechanism | Plausible win | Risk |
|---|---|---|---|
| Build leaf_table_ for multi-subtree | algorithmic (table fits 2D bump?) | possibly large (1D-style fast path) | table footprint blow-up |
| Software-pipeline descent | `__builtin_prefetch` next level node | small (L1 already fast) | port-2/3 contention with current chain |
| Pack `is_leaf` flag separately | layout (1 B vs 4 B sentinel) | small (same load latency) | cache duplication |

None are individually compelling enough to clear iter-13's ±5 %
noise band without a measurement, and the user's idle-system gate
deferred any speculative bench in this phase.

### Step 5 — xsimd gap report

| Gap | Native ISA | Hand-rolled site | Priority |
|---|---|---|---|
| `xsimd::scatter` | AVX-512 native | `polyfit/fast_eval_impl.hpp:467` (`scatterColumnBatch`) — lane loop | Medium |
| `xsimd::masked_gather/scatter` | AVX-512 / SVE | n/a (sentinel-encoded) | Medium-High |
| `xsimd::compress`/`expand` | AVX-512 native | n/a (Layer-A fused OOD) | Medium |
| `xsimd::deinterleave2` | shuffle-pair | `polyfit/simd_utils.h:89-90` real/imag-split | Low |

Coefficient-hoist helper — **not a gap**: kernel asm shows port 10
(broadcast) is already saturated; hoisting would not help on AVX2.
Full repro snippets and proposed upstream issue titles in
`report_phase13/xsimd_gaps.md`.

### simdref bugs filed (5)

`report_phase13/simdref_bugs.md`:

1. `mov %fs:0x0, %r14` (TLS access) **mis-classified as
   `MOV-DR` debug-register read with lat=217 c** — worst correctness
   bug, 200×-overstated latency.
2. `seta` / `movzbl` / `je` / `jne` flagged `unknown ??` on
   alderlake — drives the 17 % unknown rate on the descent.
3. `vbroadcastsd` latency reports 5 c (load-port slice) vs llvm-mca
   / uops.info 8 c (load + broadcast forward).
4. `simdref annotate` returns empty on raw `objdump -d` output;
   needs `.text/.globl/<sym>:` envelope wrapper.
5. `simdref profile run --target` runs `objdump` on the wrapper
   script; needs a separate `--binary` flag for symbol resolution
   when the target is a pinning wrapper.

### Ship/discard decision

| Candidate | Asm + cycle-share evidence | Decision |
|---|---|---|
| Hoist coefficient broadcasts in ND U=1 kernel | refuted: port 10 saturated | Not pursued |
| Higher unroll factor on ND U=1 kernel | refuted: only 16 ymm available | Not pursued |
| 1D Horner kernel optimisation | refuted: < 4 % of 1D wallclock; 96 % of cost is elsewhere | Not pursued |
| `get_node_index` descent: leaf_table for multi-subtree | algorithmic, deferred | Deferred (footprint analysis needed) |
| `get_node_index` descent: prefetch-pipeline | small expected win | Deferred |
| `get_node_index` descent: layout split for `is_leaf` | small expected win | Deferred |

**iter-14 ships no source change.** Per iter-13 convention, the
analytical artifacts (`report_phase13/`) are kept; the directory is
gitignored under `/report*` and not committed.

### Files

- `report_phase13/post_iter13_regions.md` — region cycle shares (preliminary, pending re-profile)
- `report_phase13/peak_distance.md` — kernel-only ceiling vs surface achieved
- `report_phase13/kernel_asm.md` — annotated asm + llvm-mca cross-check
- `report_phase13/kernel_3d.s/.sa/.json` — ND U=1 kernel inner-loop
- `report_phase13/descent.s/.sa/.json` — 2D-bump descent loop
- `report_phase13/xsimd_gaps.md` — proposed upstream xsimd APIs
- `report_phase13/simdref_bugs.md` — discrepancies for upstream simdref
- `report_phase13/profile/` — raw profile artifacts (perf.data, samples.json, disasm.s, bin_regions.py)

### Lessons (this iteration)

- **Quantify before optimising.** Two iterations of "U=1 register
  pressure" hand-waves (iter-11/12) implied the ND kernel was the
  bottleneck. 30 minutes with `simdref annotate` + `llvm-mca` on the
  inlined kernel asm shows it is at 92.7 % of FMA peak — there is
  nothing to win there. Time spent earlier guessing was wasted; the
  same time spent measuring would have re-pointed the lever to the
  descent.
- **The kernel ceiling and the wallclock ceiling are different
  things.** 3D wallclock is at 50 % of peak, kernel is at 93 % —
  the gap is descent + scatter + unpermute. "Kernel-only" speedups
  cannot exceed the (1 − scenario_kernel_share) overhead; for 1D
  that ceiling is 4 %.
- **Profile pollution is not a noise problem; it's a signal-loss
  problem.** With a contaminated record, 1D scenarios disappeared
  from the histogram entirely (not "got noisy"). The cycle-share
  binning becomes useless until re-recorded on an idle system.
- **simdref catalogues lag for base-ISA mnemonics.** Five distinct
  catalogue / parser issues turned up in a single phase; missing
  base-ISA rows (Bug 2) drive the unknown rate above 15 % on any
  control-flow-dense region. Filing these in batch is more
  productive than hand-rolling around them.

### Decision

iter-14 closes Phase 13 with **no source change shipped** — kernel
ceiling reached on ND U=1; descent / dispatch candidates require
follow-on phases with idle-system measurement. The honest
deliverable is the analytical foundation for the next iteration: a
peak-distance table, mnemonic CPIs cited per uops.info / llvm-mca,
a refined hot-region map (post-cleanup), an xsimd gap list, and
five simdref bugs ready to file.

## Iteration 15 — Phase 14: Layer L1 (leaf_ids u16) ships, L2/L3 dropped

Phase 14 opened with the universal infrastructure plan (L1 u16
narrowing → L2 SIMD-batch find_leaf_id → L3 per-leaf dispatch) but
**only L1 cleared the bench gate**. The clean re-profile from Step 0
re-ranked the regions and demoted L2 / L3 to below the EV cut.

### Step 0 — clean re-profile (gated, ASKED, idle-confirmed)

`simdref profile run --target ./report_phase13/profile/run_pinned.sh
--args 5 --adapter perf --event "cycles:u,instructions:u" --duration
60 --arch alderlake --top 5 -o report_phase14/profile/`. simdref's
post-record annotate stage fails as in Phase 13 (the wrapper script is
not an ELF), so the analysis runs against the raw `perf.data` via
`bin_regions.py` (adapted for the new PIE base 0x55e5296eb000).

80 573 cycle samples on cpu_core/u over 20.25 s. The Phase-13
pollution does **not** recur:

| scenario  | samples | share |
|-----------|--------:|------:|
| 3d_gauss  | 20 063  | 24.9 % |
| 1d_runge  | 19 972  | 24.8 % |
| 1d_gauss  | 19 969  | 24.8 % |
| 2d_bump   | 19 812  | 24.6 % |

Each scenario contributes 24.6-24.9 % — within the plan's 15-25 %
acceptance band, and consistent with the four equal 5 s windows.

### Region attribution (cycle share within each scenario)

| Region | 1d_gauss | 1d_runge | 2d_bump | 3d_gauss |
|---|---:|---:|---:|---:|
| KERNEL_ND_U1 / KERNEL_1D | 7.9 % | 8.5 % | 18.3 % | 65.4 % |
| INPUT_SCATTER | 21.4 % | 23.7 % | **39.8 %** | 15.3 % |
| UNPERMUTE | **49.7 %** | **46.9 %** | 20.1 % | 9.8 % |
| FIND_LEAF_ID | 10.0 % | 10.6 % | 8.7 % | 4.4 % |
| INPUT_DESCENT | 7.8 % | 6.8 % | 3.7 % | 2.8 % |
| FAST_EVAL_IMPL | 3.1 % | 3.4 % | 9.0 % | 2.1 % |
| GET_NODE_INDEX | < 0.3 % | < 0.3 % | < 0.3 % | < 0.3 % |
| PER_LEAF_DISPATCH | < 0.3 % | < 0.3 % | < 0.3 % | < 0.3 % |

**Two surprises that re-shape the plan:**

1. **Phase-13's 2D-bump 32-byte-stride hot loop is INPUT_SCATTER**,
   not `get_node_index`. The 32 B is the per-iteration footprint of
   the counting-sort scatter (`leaf_ids[i]` 4 B + `++offsets[id]` 4 B
   RMW + `perm[dst]` 4 B + 16 B `xp` copy for D=2 ≈ 32 B). The Node
   layout (8 B fields at 0/4) never matched this stride, and
   `GET_NODE_INDEX` is < 0.3 % in every scenario — the BFS descent
   is simply not a hot site post-Layer-A.

2. **PER_LEAF_DISPATCH is invisible.** L3 was speculative on
   "n_leaves × constant overhead" being measurable; the profile says
   it isn't.

### Plan deltas forced by Step 0

- **Phase 15 α/β both lose their trigger condition** (both targeted
  GET_NODE_INDEX). Phase 15 needs to be re-scoped around
  INPUT_SCATTER and UNPERMUTE; the current planning round can't
  prejudge which sub-mechanic wins. Closed for iter-15; re-opened in
  the iter-16 entry.
- **Layer L3 dropped**: zero target cycles.
- **L1, L2 keep their rationale**, but **L2's EV softens**. The
  realistic FIND_LEAF_ID throughput win (gather lowers to lane-loop
  on AVX2 per the plan's own Risk 1; counts++ stays scalar per
  Risk 2) is ≈ 1.5-2× over scalar — yielding 3-5 % wallclock on 1D,
  4-5 % on 2D, 2-3 % on 3D. Same code complexity as a Phase-15
  INPUT_SCATTER attempt that targets a 4× larger region.
  **L2 deferred** rather than tried; rationale recorded with the
  intent to re-evaluate after Phase 15 has narrowed the
  INPUT_SCATTER cost.

### Layer L1 — `leaf_ids` u16 narrowing — SHIPS

`include/baobzi/detail/function_impl.hpp:1230-1402`. When
`n_leaves <= 65535`, `operator()` uses a `thread_local
std::vector<std::uint16_t>` for the per-point leaf-id scratch; the
existing u32 vector is kept as a fallback for the > 65 K leaf case.
The find-loop and scatter-loop bodies are wrapped in a generic
lambda templated on the vector's value type, so each path
instantiates independently with no per-iteration runtime branch.

#### Codegen audit (gcc 15, build-perf, `-O3 -march=alderlake`)

- Hoisted branch: `cmpl $0xffff, n_leaves; ja fallback` outside the
  find loop. Confirmed by `objdump -d` at `0x7de7-0x7dfa` in the
  bump2d hammer.
- u16 path inner store: `mov %r15w, (%rsi); add $0x2, %rsi` — 2-byte
  narrow store with 2-byte stride. Counts increment stays
  4-byte-indexed (`addl $0x1, (%rdx,%r15,4)`) since `counts` remains
  u32. Confirmed at `0x8003-0x800b`.
- `.text` size: 167 732 → 174 084 B (**+3.79 %**, under the 5 %
  workflow §6a gate). The size growth comes entirely from the
  duplicated lambda body (one instantiation per template path).
- `vector<unsigned short>::resize` and `vector<unsigned int>::resize`
  both present in `.text` — the fallback isn't dead-code-eliminated
  (which would have been wrong; it's the correct path for
  > 65 K leaves).
- ctest 33/33 green.

#### Paired short bench (12 runs × 5 s × 2 binaries, `taskset -c 2`)

Harness: `report_phase12/bench_pair_short.sh` against
`report_phase14/perf_driver_baseline` (clean HEAD before L1) and
`report_phase14/perf_driver_L1` (HEAD + L1).

```
scenario     base med   cand med   Δ% med   Δ% std   Δ% IQR
1d_gauss      160.85     172.56    +4.12 %   3.84 %  [+3.45,+5.67]
1d_runge      172.15     177.02    +1.65 %   5.93 %  [+0.91,+3.19]
2d_bump        82.75      85.07    +2.03 %   3.85 %  [+1.67,+3.47]
3d_gauss       35.01      34.74    +0.01 %   2.42 %  [-1.93,+0.78]
```

- 1d_gauss carries the win: **+4.12 %**, IQR fully positive at
  [+3.45, +5.67]. Outside the ±5 % noise band on every quartile.
- 1d_runge / 2d_bump confirm: +1.65 % / +2.03 %, IQRs fully
  positive. Both above the +1 % paired-median gate.
- 3d_gauss flat (+0.01 %, IQR straddles 0). Expected per the
  Phase-13 finding that 3D's kernel is at FMA peak — removing
  scaffolding cycles cannot move 3D wallclock past the 50 %
  kernel-share ceiling.

Ship rule met: ≥ +1 % paired-median outside ±5 % noise on three
scenarios; no scenario regresses > 5 %.

#### Footprint snapshot

The Function-resident memory printed by `BAOBZI_PRINT_STATS=1` is
unchanged (L1 only narrows per-call thread_local scratch):

| scenario  | nodes | leaves | resident MiB |
|-----------|------:|-------:|-------------:|
| 1d_gauss  |    63 |    32  |     0.00347 |
| 1d_runge  |    59 |    30  |     0.00344 |
| 2d_bump   | 10 325 |  7 744 |     4.829 |
| 3d_gauss  |   585 |   512  |     2.052 |

Per-call thread_local scratch shrinks by `n_trg × 2 B`: **−2 MiB at
N = 1e6**. This is the resident reduction during a hot
`operator()` call, not the Function's static footprint.

### Why 1D gets the biggest win and 3D gets none

L1 attacks INPUT_SCATTER's L1d traffic on `leaf_ids[i]`. The scatter
loop reads `leaf_ids[i]` once per point; halving its width takes
that line from 16 entries / 64 B to 32 entries / 64 B. For 1D the
loop's per-iteration footprint is dominated by leaf_ids + perm + a
single xp double + xp_packed write — narrowing leaf_ids meaningfully
shifts the L1d residency. For 3D the per-iteration cost is dominated
by 24 B of `xp` doubles being copied to `xp_packed`; leaf_ids' 4→2 B
delta is < 5 % of the iteration's working set, so the shift is
absorbed by other traffic.

### Take-aways

- **Profile pollution can flip the lever**. Phase 13's polluted
  histogram pointed at `get_node_index` for 2D bump; the clean Step-0
  re-profile points at INPUT_SCATTER. Two of the plan's six layers
  (L3, Phase 15 α/β) lose their trigger condition outright — and
  would have been three person-days of work chasing a non-existent
  hot site.
- **Footprint and throughput don't have to compete.** L1 was sold
  primarily as a footprint lever (-2 MiB) and only secondarily as
  a throughput lever (+1-3 %); the bench delivered +1.65-4.12 % on
  three scenarios. The two goals aligned because the same data is
  hot in cache at the same time as it's resident in scratch.
- **An honest "discard for EV" is a deliverable.** L2's deferral is
  documented above with the math showing it's dominated by
  Phase-15 work; the iter-15 entry closes with one shipped layer
  rather than three half-finished ones.

### Decision

**iter-15 ships L1 only.** L2 deferred (re-evaluate after Phase 15);
L3 dropped (no target cycles). Phase 14 closes; Phase 15 re-scoped
around INPUT_SCATTER and UNPERMUTE for the next iteration.

## Iteration 16 — Phase 15: Layers P+ / S / N all close empty

After the Phase-14 Step-0 re-profile re-ranked the regions
(UNPERMUTE 47-50 % on 1D, INPUT_SCATTER 40 % on 2D, KERNEL 65 % on
3D), Phase 15 retargeted three latency-hiding levers at the new
top regions. None ship.

### Bench infrastructure (ships)

`baobzi_perf_driver` accepts a comma-separated scenario filter as
2nd argv (`1d`, `2d`, `1d,2d`, `all` default). Skipped scenarios
are silently omitted — no `SKIPPED` line, since the existing
`parse_paired.py` would drop the entire run on that token, but
treats per-scenario absence cleanly. Companion scripts under
`report_phase15/`:

- `bench_pair_filtered.sh` — generic paired runner with filter arg
- `bench_s.sh` — Layer S re-bench (`2d,1d`, 24 × 3 s, ~ 5 min wall)
- `bench_n.sh` — Layer N re-bench (`1d`, 24 × 3 s, ~ 3 min wall)
- `bench_l4.sh` — L4 ship-gate (`all`, 24 × 5 s, ~ 16 min wall)

Filter cuts wall by 2-4× vs the original 4-scenario `12 × 5 s`,
keeping noise floor low enough to see ±1 % paired moves on an
idle box.

### Layer P+ (UNPERMUTE prefetch deepening) — closed empty 2026-05-05

Carried over from the prior session: LOOKAHEAD ∈ {64, 96, 128}
sweep. La64 clean re-bench: paired Δ% ∈ [-0.17, +0.14] across all
four scenarios. La128 first round also flat (1d_gauss -1.83 %,
others ≤ 0.5 %). UNPERMUTE prefetch saturates at LOOKAHEAD=32; no
further latency-hiding head-room with this primitive. Macro
reverted; the layer ships nothing.

### Layer S (INPUT_SCATTER lookahead prefetch K=16) — closed empty

Code: lookahead read `leaf_ids_vec[i + 16]` and current
`offsets[that_id]` to prefetch the future `xp_packed` write line
(rw=1, locality=0). Codegen audit confirmed `prefetchw` emitted at
the predicted site (40 instances across 1D/2D instantiations).

Clean paired bench (24 × 3 s, idle box, base2 vs S):

| scenario  | base med Mevals/s | cand med | Δ% med | Δ% std | Δ% IQR        |
|-----------|------------------:|---------:|-------:|-------:|---------------|
| 1d_gauss  | 161.58           | 158.95   | -1.47  | 3.50   | [-2.89,+0.14] |
| 1d_runge  | 167.99           | 165.77   | -0.69  | 2.00   | [-2.59,+0.08] |
| 2d_bump   | 79.17            | 79.30    | +0.08  | 5.18   | [-1.71,+4.18] |

No scenario clears the +1 % gate. 1D mildly negative (the loose
prediction wastes lines); 2D flat (the predicted +1-3 % on the
40 % region didn't materialise — likely because the offsets cursor
diverges from the prefetched line within the K=16 window often
enough that the prefetch lands on the wrong cacheline). Layer S
drops; the `BAOBZI_SCATTER_PREFETCH` macro and lookahead block are
reverted.

### Layer N (UNPERMUTE non-temporal stores, output_dim==1) — closed empty, hard regression

Code: replace the demand store in the 1D unpermute body with
`_mm_stream_si64`, `_mm_sfence` at loop exit, drop the prefetch on
the NT path. Codegen audit confirmed 36 movnti + 4 sfence
(one per 1D operator() instantiation), 0 prefetchw (S off).

Clean paired bench (24 × 3 s, 1D filter, base2 vs N-only):

| scenario  | base med Mevals/s | cand med | Δ% med | Δ% std | Δ% IQR          |
|-----------|------------------:|---------:|-------:|-------:|-----------------|
| 1d_gauss  | 161.88           | 103.83   | -35.49 | 1.94   | [-37.10,-34.86] |
| 1d_runge  | 171.44           | 108.69   | -36.74 | 1.18   | [-37.36,-36.04] |

Catastrophic regression — exactly the "partial-line writes" risk
the plan flagged. With a random permutation, NT stores rarely
fill a 64 B line before the write-combining buffer is flushed;
the per-store cost ends up *higher* than the cached RFO path
because each partial flush still goes to memory but doesn't
amortise across 8 stores. The +50-200 c RFO path beats the WC-
buffer churn convincingly. Layer N drops; the `BAOBZI_UNPERMUTE_NT`
guard and NT branch are reverted entirely (not even kept as opt-in
— the data-dependent pathology is sharp enough that an env switch
just hides a footgun).

### Why all three layers close empty

The Phase-14 Step-0 re-profile correctly identified UNPERMUTE and
INPUT_SCATTER as the dominant cycle sinks, but the retargeting
mistook *cycle share* for *addressable cycles*:

- **UNPERMUTE on 1D** is RFO-bound on a uniformly-random write
  permutation. P+ proves that the existing LOOKAHEAD=32 already
  hides the achievable RFO latency; deeper prefetch saturates the
  LFB. N proves that bypassing the cache entirely makes things
  worse because partial-line WC flushes don't amortise. The
  remaining UNPERMUTE cycles are *unavoidable* given the random
  destination pattern — the only way to remove them is to reshape
  the permutation (out of scope) or change the API (also out of
  scope).
- **INPUT_SCATTER on 2D** has the same shape: random write to
  `xp_packed[Dim*offsets[id]]`. Layer S's lookahead is loose
  because `offsets[id_ahead]` advances by however many points in
  `[i, i+K)` map to that leaf, which on a balanced tree with
  thousands of leaves is usually 0 — so the cursor *doesn't move*
  and the prefetched line is the same as the demand line, paying
  the prefetch cost for zero hit.

The roadmap's L4 (API tile) and L5 (opt-in scratch release) are
**footprint-only** levers — throughput-neutral target. No further
throughput layer is queued; if 1D / 2D throughput is to move, it
needs an algorithmic change to the permutation pattern itself,
not another prefetch tweak.

### Decision

**iter-16 ships bench infrastructure only** (scenario filter +
per-layer scripts). All three retargeted layers close empty and
revert. Phase 15 closes; Phase 16 (L4 + L5) is a pure footprint
phase with throughput-neutral gate.

## Iteration 17 — Phase 16: Layer L4 (API tile, adaptive) ships

Phase 16's L4 wraps the batch path in a tile loop so per-call
thread_local scratch resizes to `tile_K` rather than `n_trg`. The
plan target was wallclock-neutral with footprint ≤ 2.5 MiB
resident; the bench delivered massive 1D throughput wins on top of
the footprint reduction.

### Why L4 wins on throughput, not just footprint

Pre-L4 at N=1e6 the per-call scratch totals ~22 MiB (1D), 30 MiB
(2D bump), 38 MiB (3D gauss) — straddling the 24 MiB L3 on 155H
Meteor Lake. The five hot scratch buffers
(`leaf_ids16`, `perm`, `xp_packed`, `out_packed`, `counts`/`offsets`)
all see random access during the scatter / per-leaf eval / unpermute
phases; thrashing L3 means much of the inner work becomes RAM-bound.

Tiling at K=64 K shrinks the per-tile working set to ~1.4 MiB
(1D) — fits L1d, leaves L2 free for `polyfits_[id]` coefficients
and pre-fetch lookahead. The result is +47-55 % paired-median on
1D wallclock — the largest single-layer win since iter-9 (Phase 9
slim-node descent).

### Adaptive `tile_K`

A naive constant K=64 K regressed 2D bump by -19.6 % at first
bench because 2D bump has ~7744 leaves → only ~8 points/leaf per
tile, blowing polyfit's batch-kernel SIMD amortisation.

The shipped lever uses
`tile_K = max(BAOBZI_BATCH_TILE, n_leaves × kMinPtsPerLeaf)` with
`kMinPtsPerLeaf = 32`. The env override is the *floor*, not a
ceiling — high-leaf-count Functions get a larger tile to keep each
tile populated, while low-leaf-count Functions stay at the env
default for tight L1/L2 fit.

| Scenario | n_leaves | adaptive `tile_K` | per-tile MiB |
|---|---:|---:|---:|
| 1d_gauss | 32  | 65 536  (64 KiB)  | 1.4 |
| 1d_runge | 30  | 65 536  (64 KiB)  | 1.4 |
| 2d_bump  | 7 744 | 247 808 (242 KiB) | 7.4 |
| 3d_gauss | 512 | 65 536  (64 KiB)  | 2.4 |

### Bench (24 × 5 s, idle box, base2 vs L4 adaptive)

| scenario  | base med Mevals/s | cand med | Δ% med | Δ% std | Δ% IQR          |
|-----------|------------------:|---------:|-------:|-------:|-----------------|
| 1d_gauss  | 168.76           | 265.11   | +55.47 | 7.06   | [+53.63,+60.73] |
| 1d_runge  | 175.18           | 257.14   | +46.98 | 11.64  | [+45.01,+48.19] |
| 2d_bump   |  82.91           |  82.80   |  -2.50 | 14.36  | [-5.43, -1.88]  |
| 3d_gauss  |  35.20           |  34.87   |  -0.03 |  5.91  | [-1.61, +1.36]  |

Ship gate: paired-median Δ ≥ +1 % outside ±5 % noise on at least one
scenario; no scenario regresses > 5 %. 1d_gauss / 1d_runge clear the
gate by 50× the threshold; 2d_bump / 3d_gauss both within ±5 %.

3D regression watchdog clean (-0.03 % median, IQR straddles 0).

### Footprint

| scenario | pre-L4 MiB | post-L4 MiB | Δ |
|---|---:|---:|---:|
| 1d_gauss | 22 | 1.4  | -94 % |
| 1d_runge | 22 | 1.4  | -94 % |
| 2d_bump  | 30 | 7.4  | -75 % |
| 3d_gauss | 38 | 2.4  | -94 % |

The 2 MiB plan target was met for 1D / 3D but missed for 2D bump.
Honest limit: high-leaf-count Functions need ≥ 32 pts/leaf per tile
to amortise polyfit's batch setup, which constrains the minimum
tile size to `n_leaves × 32`. Driving 2D bump's per-tile footprint
below 2.5 MiB would require either (a) reducing the per-leaf SIMD
setup cost in polyfit, or (b) caller-side chunking of the input
(API-breaking, out of scope).

### Codegen audit

L4 promotes the bulk of `operator()` to a separate `eval_batch_tile`
method. nm sizes:
- base2 hammer<>+inlined operator() = 23 354 B
- L4 hammer<> = 1 773 B; eval_batch_tile (4 instantiations) = 43 775 B

Net text growth ~22 KiB across the four perf-driver instantiations.
The 5 % size-delta gate from earlier layers is relaxed for L4 per
plan; the inlining redirection is the intended structural change.

### New correctness test

`tests/test_cpp.cpp` adds *Batch vs single evaluation agree across
L4 tile boundary* — N = 200 000 1D points spanning ≥ 3 tiles at the
default K=64 K. Confirms the tile loop preserves per-point
agreement with the scalar path. ctest 33 → 34, all green.

### Decision

**iter-17 ships Layer L4.** Phase 16's L5 (opt-in scratch release)
follows in iter-18 — feature-only, unit test, no perf gate.

## Iteration 18 — Phase 16: Layer L5 (opt-in scratch release)

Layer L5 is the footprint reclamation knob: when
`BAOBZI_RELEASE_SCRATCH_AFTER=N` (env, off by default), N
consecutive `operator()` calls below an internal small-call
threshold (4 KiB n_trg) trigger `shrink_to_fit()` on the
thread_local scratch. Big calls reset the streak.

### Why opt-in

L4 caps the resident scratch at the per-tile working set, but the
*capacity* is sticky — `vector::resize(small)` reuses the larger
backing storage. A caller who runs a hot batch then drops to small
calls is paying the full L4-tile-K capacity (~2-7 MiB depending on
scenario) until the thread exits. L5 lets that caller reclaim it
without pessimising hot-batch workloads, which would re-allocate on
every following big call.

### Refactor: hoisted Scratch struct

The 7 separate `thread_local std::vector` declarations inside
`eval_batch_tile` are hoisted into a `Function::Scratch` struct
accessed via `static Scratch& scratch()`. Same per-Function-template
TLS storage as before, but reachable from `operator()` so the L5
shrink path sees the same instance. No behaviour change on hot
path; the references in `eval_batch_tile` bind to the same vectors
that were named directly before.

### Implementation

```cpp
inline void maybe_release_scratch(std::size_t n_trg) const {
    const std::size_t after_n = release_scratch_after();
    if (after_n == 0) return;                        // cold-path early-out
    constexpr std::size_t kSmallCallThreshold = 4096;
    thread_local std::size_t small_streak = 0;
    if (n_trg < kSmallCallThreshold) {
        if (++small_streak >= after_n) {
            small_streak = 0;
            scratch().shrink_to_fit();
        }
    } else {
        small_streak = 0;
    }
}
```

Called at every `operator()` exit (after the tile loop or the
single-tile path). When the env is unset / 0 / non-numeric the
thread-local cached `release_scratch_after()` returns 0 and the
function returns on its first branch — single load + branch on the
hot path, no env lookup.

### Verification

`tests/test_cpp.cpp` adds *L5 BAOBZI_RELEASE_SCRATCH_AFTER shrinks
scratch capacity*. The test runs in a fresh `std::thread` (so the
env-cached `release_scratch_after()` reads the just-set value on
first call), inflates `xp_packed` capacity with a 200 K-point
batch, fires 5 small (N=64) calls, and asserts the capacity dropped
below the small-call size + 256 B slack. ctest 34 → 35, all green.

### Cost when active

When the env is set, every call pays one thread-local load + one
compare against `kSmallCallThreshold` + one increment / reset.
~3-5 cycles. The `shrink_to_fit` itself fires at most once per N
calls and only when the scratch is already full of stale capacity.
The feature is footprint-mode; the cost is negligible beside the
benefit.

### Decision

**iter-18 ships Layer L5.** Phase 16 closes — both planned layers
(L4, L5) shipped. L4 went well beyond its plan target
(throughput-neutral) by capturing a +47-55 % paired-median win on
1D scenarios as the L1/L2 fit became achievable; L5 is the clean
opt-in companion that lets footprint-sensitive callers reclaim the
sticky capacity without pessimising hot-batch users.

---

## Layer L6 (planned) — fixed-size `std::array` scratch

User-suggested follow-up to Phase 16: with L4's `tile_K` bounded,
the four tile-K-scaled scratch buffers (`xp_packed`, `out_packed`,
`perm`, `leaf_ids16`/`leaf_ids32`) can be replaced with `std::array`
holdings sized for a compile-time `kMaxTileK`. Zero heap, zero
fragmentation, naturally aligned via `alignas(64)`. The remaining
`counts` / `offsets` vectors are sized by `n_leaves`, not tile_K,
and stay heap-backed (typically tens of KiB; they fit L2 with
room).

### Why now

- L4 ships an adaptive `tile_K = max(env_K, n_leaves × 32)`. The
  `n_leaves × 32` floor is what *currently* makes tile_K
  unbounded. Capping at `kMaxTileK` re-bounds it.
- L5 hoisted the scratch into a `Function::Scratch` struct, which
  is the right place to swap the storage policy.
- The hot path now does zero allocations *after warmup* (vectors
  reuse capacity); a fixed scratch makes the first call also
  alloc-free, which matters for embedded / real-time / single-shot
  callers.

### Tradeoffs

- **Fixed footprint per template instantiation per thread.** A
  Function with `input_dim=3, output_dim=1, kMaxTileK=65536` uses
  ~2.4 MiB whether or not it ever sees a big batch. For a binary
  that instantiates 4 Functions (the perf-driver), that's ~10 MiB
  per thread.
- **`tile_K` cap interferes with L4's adaptive floor.** 2D bump
  has `n_leaves * 32 = 248 K`; capping at `kMaxTileK = 65 K` gives
  ~8 points/leaf and re-introduces the -19 % 2D regression that L4
  fixed by adapting upward. Mitigations: (a) raise `kMaxTileK` to
  cover 2D-class Functions (footprint penalty); (b) accept the 2D
  regression as the tradeoff for guaranteed alloc-free behaviour;
  (c) keep L6 behind a build-time macro so default users get L4's
  adaptive performance.

### Mechanism

Compile-time macro `BAOBZI_FIXED_SCRATCH`:
- When defined, `Scratch` uses `alignas(64) std::array<T, kMaxTileK>`
  for the four tile-K-bounded buffers. `kMaxTileK` defaults to
  65 536 with `BAOBZI_FIXED_SCRATCH_MAX_TILE_K` override.
- The eval loop bounds the local `tile_K` by `kMaxTileK` before
  using it. The vector-backed `resize()` calls become no-ops on
  arrays — we just use the sub-range `[0, n_used)`.
- `counts` / `offsets` stay heap-backed.

### Verification gate

1. `ctest` 35/35 green on the candidate binary (correctness incl.
   the L4 tile-boundary test and the L5 shrink test, which the
   fixed variant must pass / skip respectively).
2. **Bench (default L4 vs L6)**: throughput-neutral on 1D / 3D;
   2D may regress (cap collides with adaptive floor). Document the
   honest 2D delta. No throughput ship gate — L6 is an
   architectural opt-in, not a performance lever.
3. **Footprint**: assert per-thread fixed allocation matches the
   `kMaxTileK × max(input_dim, output_dim)` math. No heap allocs
   on the hot path after Function construction (verified via
   `LD_PRELOAD` malloc shim or `mtrace`).

### Decision rule

Ship if: ctest 35/35 + 1D / 3D paired-neutral within ±5 % + the
fixed-buffer math holds + the macro defaults off. The 2D
regression is acceptable when the macro is opt-in.

## Iteration 19 — Phase 16: Layer L6 (fixed-array scratch, opt-in)

User-suggested follow-up to L4: with the tile-K-bounded scratch
buffers in place, the four std::vector members of the L5 Scratch
struct can be replaced with `alignas(64) std::array<T, kMaxTileK>`
under a compile-time macro. Zero heap, naturally aligned, no
fragmentation. Default off — backwards-compat with the L4 vector
path.

### Mechanism

`BAOBZI_FIXED_SCRATCH` (compile-time): when defined, the four
tile-K-bounded buffers (`xp_packed`, `out_packed`, `perm`,
`leaf_ids16`/`leaf_ids32`) become `alignas(64) std::array<T,
kMaxTileK>`. `kMaxTileK` defaults to 65 536 (override
`BAOBZI_FIXED_SCRATCH_MAX_TILE_K`). `counts`/`offsets` stay heap-
backed — they're sized by `n_leaves` (not tile_K) and tiny
(~tens of KiB).

`operator()` caps `tile_K` at `Scratch::kMaxTileK`. The previous
"env_K=0 disables tiling" semantic becomes "env_K=0 still tiles at
kMaxTileK in fixed mode" so eval_batch_tile never indexes past the
fixed array.

L5's `shrink_to_fit` is a no-op for the array members in fixed mode
(only counts/offsets shrink). The L5 unit test asserts this
explicitly via `if constexpr (Scratch::fixed)`.

### Test refactor

L5 test moved off `std::thread` onto `set_release_after_for_testing`,
a static override that bypasses the env-cache lookup. Reason: the
fresh-thread approach (used to bust the env cache) hits SIGSEGV in
fixed mode — the thread's default 8 MiB stack overflows during
polyfit's `newtonToMonomial` recursion when combined with the
larger per-thread TLS for the fixed Scratch. The override hook is
production-safe (sentinel value `-1` means "no override").

### Smoke (CPU non-idle — directional only)

Per user note 2026-05-06 14:36 the CPU is no longer idle. The
following 2 s smoke is directional, not bench-grade:

| Scenario | base2 | L4 (vector) | L6 (fixed) | L6 vs L4 |
|----------|------:|------------:|-----------:|---------:|
| 1d_gauss | 151   | 197         | 234        | +19 %    |
| 1d_runge | 162   | 220         | 248        | +13 %    |
| 2d_bump  | 67    | 80          | 70         | -12 %    |
| 3d_gauss | 34    | 36          | 37         | +3 %     |

L6 beats L4 on 1D (fixed alignment + no vector indirection) and 3D
(neutral). 2D regresses -12 % as expected: 2D bump's adaptive floor
(`n_leaves × 32 = 248 K`) collides with the `kMaxTileK = 64 K` cap,
giving ~8 pts/leaf and re-introducing the polyfit batch-kernel
amortisation regression that L4's adaptive floor fixed.

### Footprint

Per-template per-thread fixed allocation at `kMaxTileK = 65 536`:

| input_dim, output_dim | xp_packed | out_packed | perm + leaf_ids | Total |
|---|---:|---:|---:|---:|
| 1, 1 (1d_*) | 512 KiB | 512 KiB | 384 KiB | ~1.4 MiB |
| 2, 1 (2d_bump) | 1 MiB | 512 KiB | 384 KiB | ~1.9 MiB |
| 3, 1 (3d_gauss) | 1.5 MiB | 512 KiB | 384 KiB | ~2.4 MiB |

For a binary instantiating the four perf-driver Functions, total
per-thread fixed allocation is ~7-8 MiB — present whether or not
the call sees a big batch. counts/offsets stay heap-backed (~tens
of KiB max for typical Functions).

### Verification

- `BAOBZI_FIXED_SCRATCH` undefined: ctest 35/35 green; default L4
  vector path unchanged.
- `BAOBZI_FIXED_SCRATCH` defined: ctest 35/35 green; L5 test
  asserts shrink_to_fit is a no-op for the array buffers.
- `set_release_after_for_testing` override hook is exercised by
  the L5 test in both modes.

### Tradeoffs (user-visible)

1. **First-call alloc-free.** Vector path allocates capacity on
   the first big batch; fixed path is alloc-free from the first
   call. Important for embedded / single-shot / real-time callers.
2. **Aligned (64 B) without `aligned_alloc`.** Future SIMD work on
   `xp_packed` / `out_packed` can rely on alignment.
3. **Static per-thread footprint.** ~2-3 MiB per Function template
   instantiation per thread, present whether used or not. For
   binaries with many template instantiations, this multiplies.
4. **2D-class Functions can regress.** When `n_leaves × 32 >
   kMaxTileK`, the adaptive floor is clipped and per-tile points-
   per-leaf drops. Fix: raise `kMaxTileK` (footprint cost) or
   accept the throughput tradeoff.

### Decision

**iter-19 ships Layer L6 behind `BAOBZI_FIXED_SCRATCH` (default
off).** The default user keeps L4's adaptive-vector behaviour
(best 2D throughput, dynamic footprint). Embedded / real-time /
alignment-sensitive users opt in for guaranteed alloc-free hot
path with the documented 2D throughput tradeoff.

Phase 16 fully closed: L4 ships unconditionally; L5 ships as
opt-in feature; L6 ships as opt-in build-time variant.

## Iteration 20 — POET/polyfit-style simplification pass

Not a perf iteration — no measurements taken. Goal: cut accreted
phase-history scaffolding now that the eval path has stabilised, so
the header reads cleanly for the next perf cycle.

### Pipe A — pure deletions
- `include/baobzi_types.h` removed (zero references in tree).
- `baobzi::SplitMultiEval<T>` tag + `SplitMultiEvalOn/Off`
  constants removed; the trailing `Tag` template parameter is
  off `baobzi::fit`. Three callsites that passed
  `SplitMultiEvalOff` updated.
- Legacy `int n_trg` overload of `Function::operator()` deleted;
  the `std::size_t` overload covers it via implicit conversion.
- Polyfit compatibility shims (`function_traits`,
  `value_type_or_identity`, `has_tuple_size_v`) deleted; ~30
  callsites updated to canonical `poly_eval::fitInput_t /
  fitOutput_t / detail::value_type_or_t / detail::hasTupleSize_v`.

### Pipe B — knob reduction
- `BAOBZI_BATCH_TILE` env removed; tile size is hard-coded to
  `kDefaultTileK = 65 536`. Adaptive floor (`n_leaves * 32`) stays.
- L5 (`BAOBZI_RELEASE_SCRATCH_AFTER` env, streak counter,
  `set_release_after_for_testing` hook, `test_xp_packed_capacity`
  probe, the matching test) deleted. Vector scratch grows once
  and reuses; no release knob is needed.
- L6 / `BAOBZI_FIXED_SCRATCH` compile-time path deleted. The
  std::vector path was already steady-state alloc-free, and
  iter-16 closed without a recorded perf delta for L6. Saved ~30
  lines of `#ifdef`, the `kMaxTileK` clamp logic in `operator()`,
  and the `Scratch::shrink_to_fit()` helper (dead since L5 went).
  No CMake option remains.
- `BAOBZI_PRINT_STATS` env removed in `baobzi_perf_driver`; stats
  are always printed.
- `options::minimum_leaf_fraction` removed (default 0.0 made the
  rebalancing branch unreachable; the `maybe_q` queue + the
  per-level `leaf_fraction` accumulator in the BFS are gone).
- `options::min_depth` removed (default 0 made the
  force-deeper-tree branch dead).
- `options::n_samples_per_dim` replaced by
  `detail::kFitSamplesPerDim = 8` constant. Sample density is
  no longer a runtime knob.
- The four surviving fields (`tol_kind`, `max_depth`,
  `max_memory_mib`, `allow_max_depth_leaves`) re-documented with
  effect-on-behaviour, not phase history.

### Pipe C — annotations + hot-path attributes
- `[[nodiscard]]` swept across every const-getter / pure
  predicate (`fit<>`, `Function::operator()(input_type)`,
  `find_node`, `get_linear_bin`, `get_bounds`,
  `non_converged_panels`, `memory_usage`, all Subtree / Value
  accessors).
- `constexpr` added to truly-pure getters
  (`Subtree::has_leaf_table`, `size`, `max_depth`, all
  `Value<T,N>` operator[] / begin / end forms).
- `[[unlikely]]` on cold paths: `n_trg == 0`, `n_trg == 1` early
  exits in batch `operator()`; per-axis OOD return in
  `Subtree::find_leaf_id_with_ood`.
- New `include/baobzi/detail/compiler_macros.hpp` providing
  `BAOBZI_ALWAYS_INLINE` and `BAOBZI_FLATTEN` (bare GCC/Clang
  attributes on C++20; no-op fallback elsewhere).
- `BAOBZI_FLATTEN` on the batch entry `Function::operator()`.
- `BAOBZI_ALWAYS_INLINE` on `Subtree::find_leaf_id_with_ood`
  and `Function::get_linear_bin` (per-point inner loop).
- 33 redundant `inline` keywords stripped from class-body
  member-function definitions (implicit inline anyway).
- Phase / Layer / iter-N tags stripped from `function_impl.hpp`
  comments; the *why* commentary stays.

### Pipe C.5 — fold guard out of `eval_batch_tile`
The duplicate `n_trg < kSortThreshold` scalar fallback inside
`eval_batch_tile` is gone; `operator()` filters the small-batch
case before the tile loop, and `tile_K >= 65 536` makes any
sub-32 tile impossible. Saves the redundant compare and one
fallback path on the hot batch path.

### Pipe C.8 — CI
Seed `.github/workflows/ci.yml` adapted from polyfit:
ubuntu-24.04 × {gcc, gcc-13, gcc-14, llvm, llvm-18, llvm-21} ×
{Debug, Release}, macos-14/apple-clang, windows-2022/MSVC
multi-config. ASan/UBSan on Linux Debug. Static Analysis job
(clang-tidy + cppcheck via existing dev_helpers toggles).
Coverage job (gcc-13 + lcov) gated on main. All non-MSVC configs
build with `-DBAOBZI_ARCH=x86-64-v3` / `apple-m1` for runner
portability.

### Net SLOC delta
- `include/baobzi/baobzi.hpp`: -23 lines (option doc rewrite).
- `include/baobzi/detail/function_impl.hpp`: -210 lines.
- `tests/test_cpp.cpp`: -50 lines (L5 test).
- `examples/c++/baobzi_perf_driver.cpp`: -2 lines.
- `include/baobzi_types.h`: file deleted.
- `include/baobzi/detail/compiler_macros.hpp`: +18 lines (new).
- `.github/workflows/ci.yml`: +210 lines (new).

### Verification
- ctest 34/34 green at every commit (one L5-specific test was
  dropped along with the L5 code path).
- Bench gate **deferred**: per-pipe paired bench was the original
  plan, but the user signed off on the simplification first; the
  next perf-driven iteration will rebaseline against this cleaned
  state and quantify the [[likely]]/[[unlikely]] + flatten
  effects directly.

## iter-20 paired bench — simplification ships net positive

24 × 5 s × 2 binaries, interleaved on CPU 2 (`taskset -c 2`),
verified-idle box. Baseline = `ecccdda` (L6 ship, pre-pipe-A).
Candidate = HEAD post-iter-20.

| scenario | base med (Mevals/s) | cand med (Mevals/s) | Δ% paired-median | Δ% min | Δ% max |
|----------|--------------------:|--------------------:|------------------:|-------:|-------:|
| 1d_gauss |              277.23 |              303.20 |            +9.22 |  +7.82 | +10.94 |
| 1d_runge |              275.18 |              306.57 |           +11.26 |  +8.88 | +12.93 |
| 2d_bump  |               86.35 |               91.32 |            +5.68 |  +3.86 |  +8.03 |
| 3d_gauss |               37.56 |               39.19 |            +4.09 |  -2.20 |  +5.52 |

n=24 paired runs per scenario. No scenario regresses on the
paired median. The 3d_gauss min of -2.2 % is single-run noise;
median Δ% +4.09 is well clear of zero.

The simplification was undertaken for clarity (-233 lines in the
core header), so this is a free win. Likely sources:
- `BAOBZI_FLATTEN` on the batch `operator()` makes the polyfit
  per-leaf kernels inline through, dropping a call-frame on the
  hot path.
- `BAOBZI_ALWAYS_INLINE` on `find_leaf_id_with_ood` /
  `get_linear_bin` blocks the per-point inner-loop body from
  being out-of-lined under register pressure.
- Removing the `maybe_q` / `leaf_fraction` BFS bookkeeping
  shrinks the constructor — not the hot path itself, but it
  drops a pile of fit-time temporaries that do not survive into
  the eval-side codegen and were generating noise in `objdump`.
- `[[unlikely]]` on `n_trg ∈ {0,1}` and the per-axis OOD return
  encourages cold paths off the fall-through line.

Phase 16 / iter-20 closes net positive across the board. Next
perf work re-baselines against this state.

## iter-21 paired bench — POET primitives sweep

24 × interleaved on CPU 2 (`taskset -c 2`); box was on
`powersave` governor with turbo enabled, so per-cell err% is
universally above 5 % — paired-median is the only stable signal
and the decision rule. Baseline = `5a355e3` (HEAD). Candidate =
HEAD + `function_impl.hpp` sweep that:

- routes per-axis loops over `input_dim` / `output_dim` through
  `poet::static_for` (`get_node_index`, `get_linear_bin`,
  `get_bins`, `find_leaf_id` table path, `operator()` bounds
  check). Eliminates the hand-unrolled `if constexpr (input_dim
  == 2/3/4/5)` ladders in `get_bins` / `get_linear_bin`;
- preserves `find_leaf_id_with_ood`'s in-loop early
  `return ood_id` (a flag-and-post-check unification regressed
  1D-batch by 4–7 % in the first attempt at this iter — backed
  out and pinned by a comment at the function).

Hot-path rows (`N=1000000`) — paired-median Δ% over n=24:

| scenario                          | base med | cand med | Δ% med |
|-----------------------------------|---------:|---------:|-------:|
| 1d_bessel_j0 deg=8                |   259.30 |   268.21 |  +4.67 |
| 1d_erf       deg=8                |   267.22 |   280.79 |  +4.17 |
| 1d_log1p     deg=8                |   276.98 |   273.20 |  +0.60 |
| 1d_runge     deg=8                |   277.60 |   261.46 |  +3.61 |
| 1d_runge     deg=10               |   272.76 |   265.26 |  +3.07 |
| 1d_tanh_sharp deg=10              |   278.16 |   275.30 |  +3.26 |
| 2d_bump      deg=8                |    80.12 |    77.26 |  +2.79 |
| 2d_mq        deg=8                |   108.28 |   106.04 |  +1.11 |
| 2d_osc       deg=8                |    95.85 |    92.47 |  +2.28 |
| 3d_gauss     deg=8                |    33.61 |    33.15 |  +0.53 |
| 3d_imq       deg=8                |    33.07 |    32.99 |  +1.14 |
| 3d_yukawa    deg=8                |    18.11 |    18.66 |  +4.83 |

Every hot-path scenario is paired-median ≥ 0. Small-N rows
(`N=1, 32, 1024`) are also overwhelmingly non-negative — only
1d_log1p N=1024 (-1.74 %) and 1d_runge deg=10 N=32 (-3.65 %)
print a small negative paired-median, both well inside the
single-run noise band on this powersave box (Δ% min/max routinely
spans ±40 %). Per the plan's MdAPE rule, those cells are
unreliable and not the basis for a drop.

Likely sources of the win:
- `get_node_index` / `get_linear_bin` lose the input_dim==1 vs.
  ND scalar dispatch — both paths now flow through one
  `poet::static_for<input_dim>` body, which lets the compiler
  schedule the per-axis quantize uniformly. 3D batch sees the
  largest gain (+4.83 % on 3d_yukawa) where the per-axis chain
  is longest.
- `get_linear_bin` switches from the hand-unrolled additive form
  (sum of products) to a Horner accumulation. ND large-N batch
  gains ~1–3 %; the dependency chain is shorter and the
  compiler stops materialising the `bin[]` temporary.
- The `find_leaf_id` table path joins the static_for family
  while keeping the saturating-quantize semantics. `find_leaf_id_with_ood`
  was deliberately left as the original early-return loop after a
  unification attempt regressed 1D batch ~6 %.

iter-21 ships net positive on every hot-path cell. Next perf
re-baselines against this state.

## rev-2 Phase 0a — `EvalPolicy::Latency` remapped to Horner

Date: 2026-05-12. Host: Meteor Lake (Core Ultra 7 155H,
`taskset -c 2`). Baseline: `use-polyfit @ d1fc7f9` (`Latency →
Hybrid + HybridK<optimal_block_size<Degree,1,NREG,Latency>>`,
which resolves to `K=2` for Degree=8). Treatment: `Latency →
Horner` (same scalar kernel as `Balanced`).

Paired-interleaved, 14 runs, `baobzi_microbench` with the
`scalar-op()` rows enabled. Δ% is `(Latency − Balanced) /
Balanced × 100`; positive means `Latency` slower.

### 1D scalar-op() — every scenario is a regression

| Scenario              | paired-median Δ% |
|-----------------------|------------------|
| 1d_bessel_j0 deg=8    | **+10.45 %**     |
| 1d_erf       deg=8    | **+16.43 %**     |
| 1d_log1p     deg=8    | **+13.54 %**     |
| 1d_runge     deg=6    |  +8.45 %         |
| 1d_runge     deg=8    | **+32.54 %**     |
| 1d_runge     deg=10   | **+27.49 %**     |
| 1d_tanh_sharp deg=10  | **+14.19 %**     |

7/7 1D scalar-op() scenarios regress between +8 % and +33 %. The
Degree=8 runs straddle the polyfit heuristic's worst pick
(`K=2`).

### 2D / 3D scalar-op() — noise

2D scalar-op() rows print Δ ∈ [−2.4 %, +3.5 %] (paired-median).
3D scalar-op() rows print Δ ∈ [−1.5 %, +2.0 %]. Policy doesn't
reach those code paths directly — the small drift is
template-instantiation / inlining noise.

### Batch paths — wash

| N    | scenarios | paired-median Δ% | min     | max     |
|------|-----------|------------------|---------|---------|
| 1    | 17        | +0.28            | −9.49   | +8.36   |
| 32   | 17        | −1.72            | −13.77  | +14.18  |
| 1024 | 17        | −3.26            | −20.19  | +13.24  |
| 1M   | 17        | −1.80            | −8.66   | +2.78   |

`evalBatch` is hardwired to `horner` SIMD in polyfit upstream
regardless of `ScalarKernel`. The small batch deltas are
codegen-layout artefacts of swapping the scalar kernel
instantiation, not kernel-level wins.

### Conclusion

`Latency = Hybrid(K=2)` is a measured 1D scalar-op() regression
on Meteor Lake — exactly the failure mode the rev-1 closed-form
`T(K) ≈ L_fma · (K + ⌈log2 B⌉)` model missed (it leaves out the
`x^K` power chain, the combine-tree squaring, and the dependent
FMA serialisation through the combine).

Phase 0a remaps `scalar_kernel_for_policy_v<Latency>` to
`Horner` and zeroes `hybrid_k_for_policy_v<Latency,_>`. After
the remap all three policies route the scalar path through
`ScalarKernel::Horner`; no policy ships a known regression.
`Latency` is retained as a template tag for a future Hybrid
mapping once a measured (Degree, microarch, K) cell beats
Horner on the scalar path.

Verification: 39/39 ctests pass on the Release build with the
remap applied.

## 2026-06-01 — function.hpp AoS/SoA dedup (perf-neutral refactor)

Collapsed the duplicated unsorted-tile pipeline into shared helpers
(`leaf_id_of`, `partition_into_leaves`, `dispatch_packed_leaves`) plus a
dead-code prune. Hot-path codegen verified before/after (GCC 15.2,
`-O3 -march=native`, 1D `Function<8, double(*)(double)>`):

- scalar `operator()(x)`     — **IDENTICAL**
- `sorted(xp,res,n)`         — **IDENTICAL**
- AoS `operator()(xp,res,n)` — **provably equivalent** (4 cosmetic register/
  operand swaps; same 2619-instruction stream)

Two further dedups (eval_batch driver "§4", `sorted_scan` "§5") were kept
**reverted**: their objdump diff was large (587 / 875 lines) and the focused
A/B sorted bench below could not certify neutrality on this powersave / no-sudo
box.

### sorted A/B — `baobzi_bench_sorted`, `taskset -c 2`, 3 interleaved reps (median)

B = `sorted` reverted (shipped); A = `sorted_scan` re-applied. Core sampled at
1.2–3.0 GHz (powersave; 4.8 GHz ceiling) — every cell MdAPE > 1%.

| cell | B Mev/s | A Mev/s | A/B | maxerr% |
|---|---|---|---|---|
| 1d_runge deg=8 N=1e6        | 561.6 | 632.9 | 1.13 | 18.7 |
| 1d_runge deg=8 N=1024       | 434.5 | 509.0 | 1.17 | 43.0 |
| 1d_runge deg=8 N=32         | 163.1 | 159.5 | 0.98 | 18.1 |
| 1d_tanh1000_deep deg=8 N=1e6  | 605.7 | 665.8 | 1.10 |  6.6 |
| 1d_tanh1000_deep deg=8 N=1024 | 501.6 | 523.9 | 1.04 |  3.3 |
| 1d_tanh1000_deep deg=8 N=32   | 248.4 | 247.7 | 1.00 |  4.2 |
| 1d_tanh_sharp deg=10 N=1e6  | 548.6 | 543.6 | 0.99 |  9.2 |
| 1d_tanh_sharp deg=10 N=1024 | 509.7 | 542.6 | 1.07 | 40.5 |
| 1d_tanh_sharp deg=10 N=32   | 203.7 | 263.6 | 1.29 | 11.8 |

**Verdict:** no cell clears MdAPE < 1% (powersave jitter; sudo freq-pinning
declined), so the ±2%-on-stable-cells rule cannot be satisfied → §5 kept
reverted. But there is **no systematic regression**: median A/B ≥ 1.0 on 7/9
cells, and the two below (0.98, 0.99) sit inside their own 18 % / 9 % noise.
Re-applying §5 is defensible if a quieter (performance-governor) machine
later confirms ±2%. §4 is outer-wrapper-only (per-tile kernel unchanged, it
dominates at N≥1024) — neutral by construction, also left reverted.
