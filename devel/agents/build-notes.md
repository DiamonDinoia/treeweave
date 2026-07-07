# Build Notes

Developer notes moved from source-code comments during the ponytail-pass comment pruning.
Organized by file. Code-adjacent 1-2 line summaries remain in the files; the full
rationale lives here.

---

## cmake/treeweave_toolchain.cmake

### Module-scan PCH trap (CMAKE_CXX_SCAN_FOR_MODULES OFF)
treeweave uses no C++20 modules, so we disable Ninja's per-TU module dependency scan
(CMake 3.28+). Besides being wasted work, the scan runs clang-scan-deps, which on some
CI toolchains is a different LLVM build than the clang++ that produced our precompiled
header — clang-scan-deps then rejects the PCH with "built from a different branch" and
the C-ABI dispatch objects fail to scan. No modules ⇒ no scan ⇒ no clash.

### LTO gcc 14 ICE (`_ipo_supported` guard)
gcc 14's LTO chokes (ICE / "invalid tree code") on the heavy template instantiations in
the timing examples. We skip IPO on GCC until that's fixed upstream — clang's LTO handles
the same code without trouble. Emscripten is also excluded: its clang *can* LTO the WASM
module, but LTO there markedly slows the link for no eval-throughput gain (the C ABI is
the only TU and is already tiny).

### -march=native rationale
`-march=native` is critical: without it the compiler targets the x86-64 baseline ISA,
which has no FMA — every Horner step then becomes a libm fma() call instead of a single
`vfmadd231sd` / `vfmadd231pd`. Profiling the multi-leaf hot path showed ~50% of runtime
in libm's `__fma_fma3` before this was enabled. Override at configure time with
`-DTREEWEAVE_ARCH=...` to target a portable baseline (e.g. `x86-64-v3` for AVX2+FMA
without per-machine tuning).

### -mtune spelling constraint
`-mtune` accepts CPU names, not architecture names. gcc rejects `-mtune=x86-64-v{2,3,4}`
and `-mtune=armv8-a` (those are `-march=` levels / arch names) and deprecates the bare
`-mtune=x86-64` (fatal under -Werror), so we derive a tune value: pass through real CPU
names (`native`, `neoverse-n1`, …) but fall back to `generic` for the portable
`x86-64` / `x86-64-v*` and `armv*` architecture baselines.

### FP-contraction flag subset rationale
We enable `-ffp-contract=fast` (permits FMA contraction; a single `fma` is *more*
accurate than a separate mul+add) but deliberately omit the broader fast-math relaxations
(`-fassociative-math`, `-freciprocal-math`, `-fno-signed-zeros`, …): the eval hot path
already unrolls per-axis loops via `poet::static_for`, and the Horner kernel is
FMA-bound, so there is nothing left for them to extract. This was measured — the
finufft-style curated fast-math subset (NaN/Inf preserved, no `-ffinite-math-only`)
produced zero CodSpeed instruction-count change and a wash-to-slight-regression in
wall-clock. On MSVC, `-ffp-contract=fast` is GCC/Clang-driver syntax and is silently
ignored by cl.exe, but clang-cl (also an MSVC-style frontend) rejects it under
`-Werror,-Wunknown-argument`; skip it on MSVC — the default Windows FP model already
matches the long-standing cl.exe build.

---

## cmake/treeweave_deps.cmake

### xsimd local-clone override
Polyfit fetches xsimd internally via CPM (pinned to 14.0.0). We override that fetch to
upstream xsimd 14.2.0 (the latest release) by cloning it locally and pointing CPM's
per-package source override (`CPM_xsimd_SOURCE`) at the checkout before polyfit's
`CPMAddPackage(NAME xsimd ...)` runs. The checkout lives under `_deps_external/xsimd`
so it persists across CMake re-configures. The SHA is verified at configure time so a
tag-move attack is caught immediately. This used to redirect to a
`DiamonDinoia/xsimd:feat/dynamic-masks` fork for its masked-load primitives, but
neither treeweave nor the pinned polyfit calls them, so stock upstream xsimd suffices.

### Suppressing analysis and arch flags on FetchContent deps
Two separate problems, same solution pattern:
1. **Static analysis**: treeweave_deps is `include()`'d, so fetched deps inherit the
   global `CMAKE_CXX_CLANG_TIDY` / `CMAKE_CXX_CPPCHECK` set by dev_helpers. This
   spams the analysis build with warnings we cannot fix. We stash and clear those
   variables before the fetches, then restore them for our own targets at the bottom.
2. **Arch/FP flags**: compiled deps (Catch2, nanobench, google_benchmark) would be
   built at our CPU baseline (`-march=native`, `-ffp-contract=fast`) for no reason.
   Header-only deps are unaffected; their code compiles under the including TU's flags.
   We stash/clear `COMPILE_OPTIONS` / `LINK_OPTIONS` from the directory properties
   for the duration of the fetch, then restore at the bottom. Warnings are
   target-scoped via dev_helpers and never bled, so they need no handling here.

### Polyfit `feat/parametric-block-size` branch
This branch extends the `estrin`-branch ScalarKernel selector with an opt-in
`HybridK<K>` block-size override plus a `consteval optimal_block_size<NCOEFFS,SIMD_W,
NREG,Policy>` picker and an `EvalPolicy { Latency, Throughput, Balanced }` enum.
Treeweave threads `EvalPolicy` through `Function` and routes the scalar kernel and K
choice per policy; today's default `Balanced` keeps the scalar `Horner` mapping to
avoid regressing the 1.3-1.6x scalar slowdown reported on Core Ultra 7 at
`xsimd::lane_w=4` until the K-sweep measurement campaign retunes the formula.

### nanobench fetch context
nanobench is a header-only microbench harness with proper warmup, MdAPE-based stability
checks, and TSC-frequency calibration. Only the benchmarks use it, so it is fetched only
when they are built. We fetch the source archive and expose the include directory only —
consumers get the impl by defining `ANKERL_NANOBENCH_IMPLEMENT` in exactly one TU (done
in `treeweave_microbench.cpp`).

### CodSpeed fetch
CodSpeed C++ supports only Google Benchmark. We fetch it from
`CodSpeedHQ/codspeed-cpp` (`SOURCE_SUBDIR google_benchmark`): their compat layer swaps
the instrumented runtime in behind the standard `benchmark::benchmark` target when
`CODSPEED_MODE` is set (CI passes `-DCODSPEED_MODE=simulation`; unset → plain Google
Benchmark for local runs).

---

## cmake/treeweave_c_api.cmake

### treeweave_headers INTERFACE target not installed
`treeweave::treeweave` carries the `include/` tree plus the transitive polyfit / POET
headers it instantiates against. Those deps are FetchContent-only (not separately
installable), so this target is for in-tree consumers (`add_subdirectory` /
`FetchContent`) — it is deliberately NOT part of the installed `find_package(treeweave)`
export set. The installed package ships the self-contained C ABI
(`treeweave::treeweave_c`) instead. See `treeweave_install.cmake`.

---

## cmake/treeweave_c_dispatch.cmake

### File overview
Owns everything ISA-conditional about the C ABI: generates the 6 `(dtype × input_dim)`
variant TUs from `src/capi/dispatch_variant.cpp.in` and compiles them as OBJECT
libraries — once at `TREEWEAVE_ARCH` when multi-arch dispatch is OFF, or once per family
`-march` level when ON (x86: a four-level ladder x86-64..x86-64-v4; non-Apple aarch64:
a single neon64; riscv64: a single rvv). Object-file generator expressions are published
on the global property `TREEWEAVE_C_OBJECT_EXPRS` for `treeweave_c_api.cmake` to
assemble into the shared and static libraries (so every TU is compiled exactly once and
shared between both libraries). Degree axis removed: degree is baked to
`chosen_degree<Arch,T,IN> = 7` everywhere (`arch_degree_table.hpp` + campaign results).

### COMDAT dedup fix (phantom Arch template parameter)
Bug #2: each per-arch variant carries the xsimd Arch type as a phantom template parameter
on the callable wrappers (`ArchTaggedScalar` / `ArchTaggedND` in
`include/treeweave/detail/c_binding_detail.hpp`), making the `poly_eval::FuncEval` /
`FuncEvalND` instantiations distinct types per `-march` so the linker cannot COMDAT-fold
the four per-arch eval kernels. No inline namespace is required; the type system solves
the problem cleanly. Note: the fit-time single-point `horner_nd_impl` lambda is NOT
arch-tagged and remains folded to the baseline scalar copy — an accepted fit-time-only
perf tradeoff (off the batch-eval hot path, no crash, no eval-throughput impact).

### Visibility setup for C-ABI object libraries
Objects are built hidden-by-default, so only the `treeweave.h` public surface (tagged
`TREEWEAVE_EXPORT`) is exported from `libtreeweave_c`. `TREEWEAVE_C_BUILD` switches
`TREEWEAVE_EXPORT` to its export form (`dllexport` / `visibility("default")`) while
compiling the library's own TUs — this is what makes MSVC emit the import library and
keeps the `.so`/`.dylib` surface to the C ABI alone. The heavy per-shape machinery does
not leak regardless — `EvalImpl` / `EvalFactory` live in an anonymous namespace (internal
linkage), and the thin arch-keyed `make_eval_for` glue is external but hidden (resolved
intra-DSO at link time).

### Sanitizer exclusion for dispatch TUs
These generated dispatch TUs are the heaviest in the build; ASan/UBSan-instrumenting
them spikes compile time and peak memory (enough to OOM hosted CI runners). UBSan also
makes `libtreeweave_c.so` emit vptr-check symbols
(`__ubsan_handle_dynamic_type_cache_miss`, ...) that C example/test executables cannot
resolve when linking a C program. The Valgrind C-ABI job already covers this compiled
path for memory errors, and the C++ test TUs still sanitize the eval kernels through the
headers.

### Multi-arch family and fan-out design
Each family defines a set of `-march` levels (one per runtime variant TU) plus per-level
flags. `arch_dispatch.cpp`, compiled at the family *baseline*, selects the matching
dispatch `arch_list` (`dispatch_arch.hpp`) and the variant TUs emit `make_eval_for<
best_arch,…>` for each level. The fan-out is generic over the level list, so adding a
family is just a new branch that sets `_treeweave_arch_levels`, per-level flags, and
baseline flags. The MSVC `/arch:` ladder has no SSE4.2-only rung (SSE2 is the x64
default, then /arch:AVX, AVX2, AVX512). Apple aarch64 stays single-arch: Apple clang
rejects `-march=armv8-a`, has no SVE, and a one-entry neon64 ladder adds nothing over
`-mcpu=apple-m1`.

---

## cmake/dev_helpers.cmake

### Module overview
Provides CMake helpers for treeweave development:
- Compiler warnings (`treeweave_enable_warnings`)
- ASan/UBSan via StableCoder/cmake-scripts (applied globally when enabled)
- Static analysis (clang-tidy/cppcheck via `CMAKE_CXX_CLANG_TIDY`/`CPPCHECK`)
- Documentation generation (doxygen, sphinx, docs targets)
- Coverage reporting (coverage target)
These are development-only; they are not needed when using treeweave as a header-only library.

### Coverage block ordering constraint
The coverage instrumentation (`--coverage`) must be applied via `add_compile_options` /
`add_link_options` *before* the library / test targets are defined: these flags apply to
every target created afterwards, so the headers (instantiated in the test TUs) and the
C-ABI library all emit `.gcno`/`.gcda`. Without this ordering the option was inert: the
build stayed uninstrumented, ctest passed, and lcov aborted with "no .gcda files found".
The batch evaluator runs leaf kernels on multiple threads, so `-fprofile-update=atomic`
avoids counter races corrupting the `.gcda`.

---

## cmake/treeweave_install.cmake

### Install overview
Installs the C ABI (`treeweave::treeweave_c` / `treeweave::treeweave_c_static`) as a
self-contained installed package. Also installs the header-only C++ API via the
consolidated bundle from `treeweave_bundle.cmake`. `find_package` exposes only the C-ABI
CMake targets; the C++ headers are consumed by include path. The installed targets carry
no transitive `find_dependency()` (polyfit/POET are linked privately).

---

## cmake/treeweave_bundle.cmake

### Bundle overview
Consolidates the header-only C++ API and its deps (polyfit, POET, xsimd, mdspan) into
one include tree so non-CMake users get an xsimd-style single-flag build:
`g++ ... -I<build>/include`. The deps live wherever FetchContent / the CPM cache put
them and land in disjoint subdirs, so `copy_directory` merges them without collision.
This same tree is what `install()` ships. Built at configure time; re-run cmake to
refresh after a dep bump. Only useful top-level (a consumer via find_package /
add_subdirectory resolves deps itself).

---

## cmake/treeweave_generate_version.cmake

### Version composition logic
`TREEWEAVE_VERSION_FULL` is composed from the tracked `VERSION` file plus git state:
- If HEAD is the exact release tag (`git describe --exact-match --tags HEAD` stripped
  of a leading `v`), `TREEWEAVE_VERSION_FULL = BASE`.
- Otherwise the suffix is `-dev.N` where N = `git rev-list --count <v-tag>..HEAD` if a
  `v<BASE>` tag exists, else `git rev-list --count HEAD`.
- N is from committed history only — the staged index and working tree are ignored, so
  the count is stable across pre-commit retries of the same commit.

### Shallow clone behavior
A shallow clone (CI's default `fetch-depth: 1`) only has HEAD, so the commit count is
wrong. In that case the committed header is trusted verbatim: it is not rewritten and the
pre-commit check does not fail. The committed value was produced by a full clone at commit
time.

### Script-mode usage
Can be run standalone: `cmake -P cmake/treeweave_generate_version.cmake`. With
`-DCHECK=ON`, exits 1 if the generated file would change — the pre-commit hook then
re-stages the newly regenerated file.

### Release override
When `TREEWEAVE_RELEASE_VERSION` is set non-empty (the release workflow passes
`-DTREEWEAVE_RELEASE_VERSION=<version>`), every version field is pinned to that exact
value and the git/dev-suffix logic is skipped. This keeps shipped C/C++ headers at clean
`X.Y.Z` even though artifacts are built before the tag is pushed.

---

## cmake/treeweave_bindings.cmake

### Bindings overview
Each language binding is guarded by its own option (all default OFF) and implemented in
its own `bindings/<lang>/CMakeLists.txt`, so a plain `cmake ..` never pays for a
toolchain the user doesn't have. A missing toolchain degrades to a STATUS message and a
skipped test. All wrappers reuse the already-instantiated C ABI (Python/MATLAB link
`treeweave_c_static`; Julia dlopen()s the shared library; Fortran links the shared
`treeweave_c`), so nothing here re-instantiates the heavy template shapes.

---

## tests/test_quantize.cpp

### File overview and invariants
The batch evaluator bins each point to a leaf id via a vectorized quantize
(`PolyTree::for_each_leaf_id_batch`), exposed as `Function::leaf_ids`. Two invariants
are tested for every input:
1. **PARITY** (bit-exact, all inputs): the vectorized `leaf_ids` stream equals the scalar
   `leaf_id` oracle point-for-point — in-domain, on exact cell boundaries, OOD on both
   sides, and for NaN / +-Inf.
2. **DESCENT** (at cell centers): the quantize-then-table-lookup id equals the tree-descent
   id (`find_node(x).poly_eval_id()`). Cell centers are the points the leaf table was
   built from, so this verifies the quantize addresses the table correctly. Bit-exact
   descent parity is NOT claimed at exact cell boundaries — `PolyTree::get_node_index`
   recomputes mids by a different chain than the table build (see polytree.hpp:388).

### check_large_leaves design notes
With many leaves (2^13 = 8192) the flat counting sort's `counts[]` histogram and the
random `xp_packed` scatter stress the caches; this guards the sort's distinct failure
modes — wrong `counts[]` boundaries, a bad `perm_inv`, or a dropped/duplicated point —
each of which routes a point's slot to a *different* point's value (a large, O(1) error)
or to garbage/NaN. The draw is capped at the upper bound `hi` (no *finite* high-OOD):
with the closed upper endpoint the batch quantize clamps finite `x > hi` to the last
leaf (returns a value) while scalar `operator()` keeps its inclusive `<=` guard (returns
NaN), so the two paths diverge there by design. That finite-high-OOD clamp parity is
pinned by the quantize-parity test (`check_1d`). NaN and -Inf still agree (both classify
OOD → NaN); +Inf is not injected because its float→int convert is arch-dependent under
the clamp (x86 → INT_MIN → OOD/NaN; ARM saturates → last leaf → finite), an accepted
divergence.

---

## tests/test_threadsafe.cpp

### Thread-safety test strategy
Contract: a single `Function` built once and not mutated may be called concurrently from
many threads provided each thread's `xp`/`res` slices do not overlap. Race-free behaviour
is asserted by *bit-exact* equality of the threaded output across `kRepeats` repeats with
the same chunking — any race on shared state would surface as flapping bits. The serial
reference comparison uses a tight relative tolerance rather than bit-exact: changing the
chunking changes per-leaf counts, which changes the SIMD-batch vs scalar-tail mix in
polyfit's `FuncEvalND::operator()` — a deterministic but path-dependent ~1 ULP drift
that is NOT a race; it is non-associative FP.

---

## tests/test_greens.cpp

### File overview
Realistic Green's-function / layer-potential kernels: the goal is to confirm that
treeweave's adaptive paneling converges on workloads with algebraic decay, exponential
envelope, or fast oscillation. Each case fits a regularised kernel away from its
singularity, asserts max-relative error on an interior sample, and (where
oscillation / decay force paneling) asserts that the tree actually subdivided.

---

## bindings/python/src/_treeweave.cpp

### Trampoline architecture
A typed trampoline (f64 and f32 variants) bridges the C callback into a Python callable.
The trampoline is always called on the thread that called Python, which already holds the
GIL; we re-acquire it defensively with `nb::gil_scoped_acquire` (re-entrant, effectively
a no-op when the GIL is already held by this thread). On callback failure we latch the
error flag, stash the live Python exception (via `PyErr_GetRaisedException` on 3.12+ or
`PyErr_Fetch/Restore` on 3.9-3.11), fill `y[]` with NaN, and short-circuit all
subsequent invocations so the C fit drains quickly without touching Python again. After
`treeweave_fit_*` returns we re-raise the stashed exception (via `nb::raise_python_error`)
so the original Python exception propagates intact to the caller. `TreeweaveFunction`
wraps a `treeweave_t` handle; its C++ destructor calls `treeweave_free`, so Python GC
suffices for cleanup.

---

## bindings/matlab/CMakeLists.txt

### MATLAB/Octave binding overview
`treeweave.mw` is the mwrap source of truth. mwrap's output (`treeweave_mex_gen.cpp` +
`tw_*.m`) is platform-independent (no arch/OS flags), so it is generated once and
committed to `generated/`. By default the build just compiles those checked-in files
(`TREEWEAVE_MATLAB_USE_PREGENERATED=ON`): no CPM, no mwrap, no bison/flex/m4, and
(critically) no MSVC link of mwrap's flex/bison globals that used to block Windows.
Set it OFF to fetch+run mwrap (via CPM) and refresh `generated/` with the
`treeweave_mw_regen` target after a `treeweave.mw` edit. There is no separate CMake
option for Octave vs MATLAB: both presets set `TREEWEAVE_BUILD_MATLAB=ON`; backend
selection is driven by toolchain discovery (mkoctfile on PATH → Octave MEX; MATLAB found
→ MATLAB MEX; both → both are built).

### MATLAB MEX plain-link requirement
`matlab_add_mex()` already linked the MATLAB libs with CMake's *plain*
`target_link_libraries` signature. CMake forbids mixing the keyword and plain signatures
on one target, so `target_link_libraries(treeweave_mex_matlab treeweave_c_static)` must
also be plain (no `PRIVATE`/`PUBLIC`/`INTERFACE` keyword). This is the standard plain-
link pattern for MATLAB MEX targets in CMake.

---

## bindings/fortran/CMakeLists.txt

### Fortran compiler selection footgun
CMake's `check_language`/`enable_language` picks whichever Fortran compiler appears
first in PATH. On Flatiron / RHEL 8 the system default is `/usr/bin/f95` (gfortran 8.5),
but the binding uses `error stop <integer>` which requires Fortran 2018 (not 2008). With
gfortran 8 that compiles without warning but produces wrong behavior. Always pass a
modern compiler explicitly:
  ```
  cmake ... -DCMAKE_Fortran_COMPILER=$(which gfortran)  # after module load gcc/13.3.0
  ```
or set `FC` in the environment before running CMake. Verify with
`gfortran --version` (must be >= 10; >= 13 preferred).

### Fortran 2018 requirement (`error stop <integer>`)
The binding uses `error stop <integer-expression>`, a Fortran 2018 feature. In Fortran
2008 the stop-code must be a default-integer scalar constant expression, which the
integer literal satisfies, but the non-constant-expr form and the negative-stop-code
behaviour are 2018. `CMAKE_Fortran_STANDARD 2018` is declared so compilers emit the
correct diagnostics when the standard is violated.

### Single static library for the binding module
`treeweave.f90` is compiled into a small static library; both the test and the example
link it. Compiling `treeweave.f90` in two targets would make Ninja's Fortran dyndep see
two rules generating `treeweave.mod`. Linking the library also propagates its module
directory to consumers.

---

## bindings/js/CMakeLists.txt

### JS binding overview
Two toolchain-selected backends:
- **Native** (default): a Node-API `.node` addon linking `treeweave_c_static`, exactly
  like the Python nanobind extension. Headers are located via `node-addon-api`
  (header-only, over the ABI-stable `node_api.h` that ships with Node) — no cmake-js.
- **WASM** (under emcc): the same C ABI compiled to a single WASM module; C ABI symbols
  are exported by name and the Emscripten runtime drives them.
Artifacts land in `dist/` (gitignored): `treeweave.node` (native), `treeweave.mjs` +
`treeweave.wasm` (WASM), and the tsc output (`*.js` + `*.d.ts`).

---

## bindings/js/src/treeweave_napi.cpp

### Native backend design notes
This is the *native* backend of the JS binding: a `.node` addon that links
`treeweave_c_static` (exactly like the Python nanobind extension). The browser
backend is a separate Emscripten/WASM build of the same C ABI (see
`wasm_glue.cpp`); the TypeScript layer (`src/backend.ts`) picks one at runtime.

Architecture mirrors the Python binding (`bindings/python/src/_treeweave.cpp`):
- `fit` runs the C fit with a typed trampoline that calls the user's JS callback
  synchronously on the calling thread — `treeweave_fit` invokes the callback
  inline, so a plain `Function::Call` works (no `ThreadSafeFunction`).
- A JS exception thrown inside the callback must not unwind through the C ABI:
  the trampoline catches it, latches an error flag, NaN-fills `y[]`, and
  short-circuits the remaining probes; `fit()` rethrows it afterwards.
- `fit` returns a plain JS object whose eval methods are closures over a shared
  `FnState` that owns the `treeweave_t` handle. The handle is freed by `~FnState`
  (when the last closure is GC'd) or eagerly by `free()`.

Eval batches are zero-copy: the C eval reads/writes straight through the JS
TypedArray's backing store (Node never moves ArrayBuffer storage during a
synchronous call), and an `out` array is written in place and returned as-is.

---

## bindings/js/src/wasm_glue.cpp

### WASM backend link-unit rationale
The browser backend is the same treeweave C ABI compiled to WASM; xsimd
auto-selects its `xsimd::wasm` SIMD128 backend under emcc.

There is deliberately almost nothing here: the public surface is the C ABI
itself, exported by name via the linker's `-sEXPORTED_FUNCTIONS` list (see
`CMakeLists.txt`), which also pulls the needed members out of the
`treeweave_c_static` archive. This file only needs to exist so the link has a
root object; it intentionally has no `main` (the module is a library, built
with `--no-entry`). Including the header keeps the version macros and the ABI
declarations visible to anyone extending this glue later.

---

## tests/test_c.cpp

### C API thread-local error state
The C ABI keeps its error message in a `thread_local std::string`
(`src/capi/treeweave.cpp`), so an error raised on one thread must be invisible
on another. The test runs two threads concurrently: one forces an error
(unsupported `input_dim` → NULL), the other does a clean fit; each must observe
only its own thread's error state. This lives in the C++ test rather than the
pure-C test because C11 `<threads.h>` is omitted from Apple's SDK.

---

## tests/test_cpp.cpp

### Sorted-1D: above-b OOD behaviour
Above `b`: finite `x > b` is out-of-domain and returns NaN on every path,
identical to the scalar `operator()`. The leaf-table fast path used to clamp
`x > b` to the last leaf and extrapolate a finite value; it no longer does, so
the batch and sorted APIs are now byte-identical to the scalar API here. (`x == b`
is the closed upper endpoint, handled separately.)

### C1 — leaf_ids[] removal rationale
The batch path no longer materialises a `leaf_ids[]` buffer; the scatter loop
recomputes the leaf id via `PolyTree::quantize_one`. This test pins the result
of that recompute path: 1D input, `output_dim=2`, 1000 deterministic random
points must match per-point scalar evaluation — tightening the 2D-in coverage to
the 1D batch path that owns the SIMD quantize fast path.

### SoA batch overload AoS equivalence
Same `Function`, same inputs — assert `aos_out[2*k + d] == soa[d][k]` bitwise
across both the unsorted and sorted 1D paths. The two paths share the per-leaf
polyfit kernel (FuncEvalND P2 AoS vs P2 SoA) so the math is identical and only
the store layout differs.

### Shifted-domain precision floor
The domain is a unit-width interval translated by 1e6. This catches places where
the implementation relies on the domain being centred near zero (e.g. computing
`b - a` losing precision, or assuming small magnitudes of `x` in the leaf-table
quantize). The shift consumes ~6 digits of relative input precision, so
verification tolerances reflect that floor.

### Memory budget test split rationale
Two cheap checks cover the budget guard without the slow ~33 MiB fit the old
version built (157 s at -O0; >1500 s under MSVC checked iterators / coverage).
The first pins the dimension-scaled auto default at compile time — no fit, zero
cost — so an unintentional tree blow-up still hits a small guardrail by default.

### D1 — min_uniform_depth rationale
`min_uniform_depth` forces uniform refinement so the leaf-table fast path is
exercised on smooth functions (where tol-based refinement would otherwise stop at
depth 1-2 and skip the table). With `min_uniform_depth=2` on a near-trivial 1D
fit the tree has at least 2^2=4 leaves and the table is live.

### TST1/COV-G3 f32 tolerance choice
Comparison tolerance is `tol_f * 100`: a flat 1e-3 is too loose (masks
regressions on functions where the approximation error is much smaller); `tol_f *
100` tracks actual fit quality — tight for 1e-5 fits but relaxes proportionally
when the caller requests a looser tol. The sorted section covers COV-G3 (f32
`sorted` untested elsewhere).

---

## bindings/julia/Treeweave/deps/build.jl

### deps/build.jl — library resolution and prebuilt download
Run automatically by `Pkg.build("Treeweave")` (triggered by `Pkg.add`).
Resolution order: (1) `LIBTREEWEAVE_C` env var; (2) a sibling CMake
`build*/libtreeweave_c.<ext>` (in-repo developers); (3) a prebuilt C-ABI release
binary matching this package's version, downloaded from the GitHub Release and
cached under `deps/usr/`. This keeps prebuilt-binary installs working WITHOUT a
committed `Artifacts.toml` — so cutting a release adds no commits to the default
branch. If nothing is found, a `deps.jl` with an empty path is still written so
the runtime resolver in `src/Treeweave.jl` can fall through to its own search;
a build failure must never block precompilation.

---

## benchmarks/treeweave_bench_binsort.cpp

### Bin-sort phase harness design
Measurement harness for the bin-sort (point→tile binning) optimization work.
Originally the Phase-0 "where does time go" probe; now also the regression guard
for what shipped:
- Phase 1 (f32 int32 quantize, `vcvttps2dq`): shipped — quantizes cell numbers.
- Phase 2 (descent `!table` leaf-id materialization): shipped — `run_descent`.
- Phase 3 (2-level radix): measured 2–5× SLOWER than the flat counting sort on
  2 MiB-L2 hardware and REVERTED; the flat sort is the sole large-leaf path.

Three measurements: (1) per-phase split (quantize/histogram/scatter) over one
tile via the macro-guarded `Function::bench_partition_phases` hook (x86 `__rdtsc`);
(2) full unsorted-batch throughput `operator()(xp,res,n)` (nanobench MEvals/s)
over `kFullN`; (3) `run_descent` — full throughput on deep no-leaf-table fits
(depth 17-18), the descent path Phase 2 targets. `n_leaves` is pinned exactly to
`2^depth` via `options.min_uniform_depth` (1D leaf table is built up to depth 16
→ 64K leaves). Pin to a P-core for stable cycles:
`taskset -c 2 ./treeweave_bench_binsort`.

### run_descent measurement rationale
Phase 2 measurement: the `!table` descent fallback. A uniform tree deeper than
16 levels (1D) exceeds the 64K-entry leaf-table cap, so no table is built and
`partition_into_leaves` walks `get_node_index` (tree descent) twice per point —
once to histogram, once to scatter. This times the full unsorted batch on such a
fit; comparing against a build that materialises leaf ids in pass 1 (read back in
pass 2) isolates the double-walk cost.

---

## benchmarks/treeweave_bench_pack_scatter.cpp

### File header
Microbench harness for `Function::eval_pack<N>` (compile-time-N pack eval:
poet-unrolled fan-out for N≤16, plain for-loop for N<1024, batch-path delegation
for N≥1024) and `treeweave::eval_scatter_sorted` (multi-Function scattered eval;
counting-sort groups by `fit_id` then dispatches per fit). Each surface is
benched against a hand-rolled scalar baseline so the nanobench `relative` column
shows the win directly. Pin process to one core for stability:
`taskset -c 2 ./treeweave_bench_pack_scatter > /tmp/bench_after.txt`. Diff vs
baseline: `./bench/compare_nb.py bench/baseline_pack_scatter_nb.txt /tmp/bench_after.txt`.

---

## benchmarks/treeweave_bench_sorted.cpp

### File header
Focused A/B microbench for the 1D sorted-input batch path (`Function::sorted`).
No other bench exercises it, so this is its canonical A/B harness — used to
decide whether sharing the AoS/SoA `sorted` scan skeleton is perf-neutral. Stable
measurement: pin to a P-core and warm it first so the governor has ramped to its
sustained ceiling before timing. High `minEpochTime` keeps each cell's MdAPE <~1%.
Diff: `./bench/compare_nb.py /tmp/sorted_before.txt /tmp/sorted_after.txt`.

---

## benchmarks/treeweave_ci_bench.cpp

### File header and JSON template design
CI regression bench for continuous tracking. Runs a fixed handful of
representative batch-eval cases and renders them as JSON for
`benchmark-action/github-action-benchmark` in `customSmallerIsBetter` mode: a
flat array of `{name, unit, value}` where `value` is the wall time of one
batch-eval call (smaller is better). Keep the case set stable across commits
so the published history stays comparable; add cases rather than renaming.

JSON value is the median wall time of one batch-eval call (nanobench's
per-iteration `elapsed`, in seconds); each call evaluates `kBatch` points, so
per-point cost is `value/kBatch`. MdAPE (measurement noise) rides along in `extra`.

### kDeep constants rationale
Force a uniformly-refined deep tree → large (2^(K×depth)-entry) leaf table,
exercising the fast-path lookup at a leaf count that spills L1/L2 — the regime
small-leaf cases miss. Depths sit at/just under the 64K-entry per-subtree table
cap (K×depth ≤ 16): 1D→14 (16384), 2D→8 (65536), 3D→5 (32768). Mirrors
`treeweave_codspeed_bench`.

---

## benchmarks/treeweave_codspeed_bench.cpp

### File header and kDeep constants
Google Benchmark twin of `treeweave_ci_bench` (nanobench). CodSpeed C++ supports
only Google Benchmark, so it mirrors the exact same four cases — same case names,
seeds, N=1<<16 — so the CodSpeed (instruction-count) dashboard lines up with the
nanobench → gh-pages (wall-time) dashboard. Keep the cases in sync when either
changes. CodSpeed runs the binary once under a simulated CPU
(`CODSPEED_MODE=simulation`); locally it is a plain Google Benchmark.

kDeep: same rationale as `treeweave_ci_bench` (see above) — forces a large leaf
table to exercise the fast-path at a leaf count that spills L1/L2. Depths: 1D→14
(16384), 2D→8 (65536), 3D→5 (32768).

---

## benchmarks/treeweave_microbench.cpp

### File header
Microbench harness driven by martinus/nanobench: handles warmup, MdAPE stability
checks, TSC-frequency calibration, and produces machine-readable output. Sweeps
{1D, 2D, 3D} × scientific-kernel × {deg 6, 8, 10} × N ∈ {1, 32, 1024, 10⁶}.
Pin the process to one core (`taskset -c 2 ./treeweave_microbench`) for stable
numbers — nanobench reports MdAPE, so unstable measurements surface as a high
error percentage rather than silent noise.

### libc++ gate
`libc++` (Apple clang) does not implement C++17 special functions in `<cmath>`,
so `std::cyl_bessel_j` is unavailable there. The bench skips that kernel on
`libc++`; a placeholder `make_j0_1d` keeps the call site well-formed under
`if constexpr (false)` discard.

### sweep_multi_fit_1d rationale
Multi-fit scattered-access case modelled on the TRIQS/diagmc bench_chebfun
workload: R independent 1D fits over [0, beta], evaluated at scattered
`(r_idx, tau)` pairs. Exercises the scalar `Function::operator()` (NOT the
batched path), which is what production callers in TRIQS/diagmc hit when filling
Wick matrices or Green-function lookups.

---

## benchmarks/treeweave_perf_driver.cpp

### Filter argv design
Optional 2nd argv: comma-separated dim list, e.g. `"1d"`, `"1d,2d"`, `"all"`
(default). Skipped scenarios are silently omitted; `parse_paired.py` treats
missing scenarios as no-data (it only drops a run when an explicit "SKIPPED" line
is printed, which is avoided).

---

## benchmarks/zeta_bench.c

### File header
C11 treeweave-vs-fair-baseline Riemann-zeta bench, via the C ABI. `ζ(s) = Σ_k
k^(-s)` summed until the tail is negligible (rel 1e-10, ≤160 terms) yet smooth
on [2,10]: fit once. Times single/multi/sorted; the native rate is sampled over
`n_native` and reused. `TREEWEAVE_BENCH_YAML=path` emits YAML. Also exercises
the C ABI directly. (See `zeta_bench.cpp` for the rationale.)

---

## benchmarks/zeta_bench.cpp

### File header
C++ Riemann-zeta bench: same workload as `zeta_bench.c` but using the C++ API.
`ζ(s)` is smooth on [2,10], expensive to eval directly (sum to negligible tail),
and fit once — so a polynomial eval wins in every mode. Times single/multi/sorted;
native rate sampled over `n_native` and reported mode-independently.
`TREEWEAVE_BENCH_YAML=path` emits YAML. C++ member of the cross-language family
(C, Fortran, Python, Julia, Octave, JS).

### zeta_partial rationale
A fair native baseline: accumulates `k^(-s)` until a term contributes less than
`kEps` relative to the running total, capped at `kMaxTerms`. A competent
hand-written zeta stops early once the tail is negligible — so this is an honest
cost, not a fixed-iteration strawman. Used as both the fit callback and the
baseline so the comparison is apples-to-apples.
