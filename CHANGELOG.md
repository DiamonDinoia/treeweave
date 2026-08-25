# Changelog

This file records every notable change to `treeweave`.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

The Release workflow (`.github/workflows/release.yml`) copies the `## [X.Y.Z]`
section verbatim into that release's GitHub Release notes.

## [Unreleased]

## [0.0.4] - 2026-08-25

### Added

- Python: `treeweave.fit` doubles as a decorator when the callable is omitted.
  `@treeweave.fit(a, b, tol)` above a `def` replaces the function with its
  fitted approximation, the `functools.cache` spelling. Every keyword option of
  the direct call applies unchanged, and the original callable stays reachable
  as `__wrapped__`.
- `.github/scripts/check_isa_leak.sh` fails a build whose shipped artifact
  contains a symbol that no ISA level owns yet holds instructions above the
  family baseline. A level owns a symbol when the mangled name carries the
  level, or when the symbol survives once per level: GCC emits an `.isra` or
  `.constprop` clone privately in each level, and an ELF-local reference
  cannot bind outside its own object, so every level reaches its own copy.
  The check runs on the installed C ABI library and on the packaged MEX. It
  reports and skips an artifact it cannot decide: one built for a non-x86
  family, and a Windows DLL, whose COFF symbol table is empty in a linked
  image so that objdump labels every internal function with the export that
  precedes it.

### Fixed

- The Windows MATLAB MEX in v0.0.1, v0.0.2 and v0.0.3 was built without
  optimization and linked against the debug C runtime (`MSVCP140D.dll`,
  `VCRUNTIME140_1D.dll`, `ucrtbased.dll`). Those DLLs ship only with Visual
  Studio, so that MEX could not load on a machine without it. The assets on
  those three tags have been rebuilt and replaced. Two defects produced it:
  CMake's MSVC platform module seeds `CMAKE_BUILD_TYPE_INIT` with `Debug`, so
  the variable is never empty after `project()` on Windows and treeweave's
  default-to-Release guard could never fire there; and nothing in the release
  path ever loaded the MEX it packaged. The default is now decided before
  `project()` and holds on every platform, and an explicit
  `-DCMAKE_BUILD_TYPE` still wins.
- The Python build requires `nanobind>=2.0,<3`. nanobind 3.0.0, released
  2026-08-22, declares its Python slot aliases as
  `inline constexpr ret (*name) args = &target;`. clang-cl rejects that: under
  the MSVC ABI the address of a `dllimport` function is not a constant
  expression. The Windows wheel builds with clang-cl, so an unpinned nanobind
  broke it three days after the 3.0 release while every other platform kept
  building.

- The multi-arch C ABI no longer shares symbols between ISA levels.
  `TREEWEAVE_C_MULTIARCH` compiles the same sources once per ISA level. Symbols
  the fan-out names carry the level in their mangled name and never collide.
  Symbols instantiated from headers at global scope do collide: every level
  emits the same weak name holding different code, and the linker keeps one
  arbitrary copy. Keep a copy from a higher level and it runs on every CPU that
  loads the binary, including CPUs that trap the instruction. Measured on the
  six variant translation units: 35 weak symbols per unit share a mangled name
  between the baseline and AVX-512 objects, 18 differ in code, and 17 instances
  across the six carry AVX-512 in the AVX-512 copy. One of them is
  `poly_eval::detail::newtonToMonomial<7>`, which is numeric work rather than
  an error path. The levels are emitted baseline-first, so a linker that keeps
  the first copy it sees keeps the baseline one. No linker promises that rule,
  so the check above decides the question on the linked artifact rather than on
  the order: of 1595 symbols in `libtreeweave_c.so`, the 201 that carry AVX-512
  registers all belong to a level that owns them by name.

### Changed

- The packaging job now runs `test_treeweave` against the staged MEX bundle on
  every platform, and refuses to package a Windows MEX that imports the debug
  CRT. `matlab.yml` additionally builds on `windows-2022` and runs the test
  under several MATLAB releases.
- The Windows MEX builds with clang-cl instead of cl.exe. cl.exe spends over 40
  minutes in its back end on a single dispatch translation unit at `/O2`, and
  the multi-arch fan-out is 24 of them, so an optimized cl.exe MEX is not
  reachable inside a CI job. clang-cl compiles the same target in 161 seconds
  and targets the same MSVC ABI, so the binaries stay interchangeable.
  Configuring the C ABI with cl.exe at anything other than `Debug` now prints a
  warning that names clang-cl.
- `-Werror` no longer applies under clang-cl: its MSVC-like driver enables an
  `-Weverything`-family set (`c++98-compat`, `pre-c++17-compat`,
  `unsafe-buffer-usage`) that the plain clang driver leaves off. Warning
  hygiene stays enforced under `-Werror` by the Linux clang rows.
- The multi-arch fan-out no longer shares a precompiled header and no longer
  enables IPO. A precompiled header is built with one target's flags; IPO
  widens what the optimizer may merge across the objects. Both exclusions are
  precautionary.

## [0.0.2] - 2026-08-13

### Changed

- Vendored dependency pins updated to the current releases: xsimd 14.2.0 →
  14.3.0 and POET `b55580f` → `v0.0.1`. Both are now on the same refs consumers
  are most likely to already have, so a project that already provides either
  dependency satisfies treeweave's copy instead of ending up with two
  incompatible sets of `xsimd` / `poet` targets in one build. The xsimd checkout
  is still verified against its pinned commit SHA at configure time to catch a
  moved tag.
- `Debug` builds now default to `-Og` rather than CMake's `-O0`. The `debug-og`
  preset already did this, because `-O0` leaves the header-only, deeply-inlined
  code un-inlined and the compute-heavy tests slow to a crawl. A preset-less
  `-DCMAKE_BUILD_TYPE=Debug`, which is what a consumer's `FetchContent` build
  gets, did not. The default now applies to every `Debug` build including
  coverage. There, line attribution gets coarser wherever the optimizer
  inlines, and the instrumented run stops being the slowest job in CI.
  An explicit `CMAKE_CXX_FLAGS_DEBUG` still takes precedence, so
  `-DCMAKE_CXX_FLAGS_DEBUG='-O0 -g'` restores the strictest line data.
- Link-time optimization is now the opt-in option `TREEWEAVE_ENABLE_IPO`,
  default `OFF`, where it was on for every non-GCC compiler. The C++ API is
  header-only, so IPO changes nothing for a consumer that only includes the
  headers, and the C ABI objects feed the installed static archive as well as
  `libtreeweave_c.so`, and IPO fills that archive with compiler IL only one
  toolchain version can link. Every binding preset turns it back on, because a
  binding ships one shared artifact and no archive; the Emscripten preset stays
  excluded, as do GCC and MSVC even when the option is `ON`: GCC ICEs on these
  templates and MSVC `/LTCG` took over an hour to link one executable. clang-cl
  still gives Windows a ThinLTO path.

## [0.0.1] - 2026-07-20

### Fixed

- Julia bindings `Project.toml` carried `version = "0.0.0"`, which Julia's Pkg
  rejects as an invalid version (`Pkg.add` fails). Bumped to `0.0.1`.
- The Windows N-API prebuild now builds: the workflow fetches the node C headers
  (absent from the Windows node install) and `node.lib` from the node dist, and
  the addon delay-loads `node.exe` to resolve `napi_*` under MSVC (the node-gyp /
  cmake-js approach). Windows Node users now get the native backend instead of
  the WASM fallback.

### Added

- Documentation now leads with the released/prebuilt install paths, and moves
  source builds to the end of each language guide for development or unreleased
  changes.
- Added a CMake guide covering the minimal CPM/FetchContent usage, user-facing
  presets, user-facing CMake options, and targets by language.
- Added MATLAB/Octave install docs for both `mip` and direct release-bundle
  `curl`/`wget` installs.
- Added runtime ISA dispatch documentation for MSVC ABI compilers. MSVC x86
  builds use the `SSE2 -> AVX -> AVX2 -> AVX-512` ladder because there is no
  `/arch:SSE4.2`.
- Added the Practical HPC NUFFTs talk link to the acknowledgements/background
  material.
- Post-publish `release-install.yml` now verifies the published binaries on a
  full matrix: `pip install treeweave` from PyPI (linux/macOS/windows × py3.9 +
  py3.12), each per-platform C-ABI release tarball via `find_package`
  (linux-x86_64/-aarch64, macOS-arm64, windows-x64), and a new
  `npm install @flatironinstitute/treeweave` job (WASM-only, all three OSes).
- Added one-liner installs to the README and install guide: `pip install
  treeweave`, `npm install @flatironinstitute/treeweave`, release tarball
  `curl`/`wget` commands, Julia `Pkg.add`, and
  `CPMAddPackage("gh:DiamonDinoia/treeweave@stable")` for C++.
- `conda/recipe/meta.yaml` + `conda/README.md`: a conda-forge recipe prepared for
  submission to `conda-forge/staged-recipes` (manual follow-up).
- Each GitHub Release now carries the raw `treeweave.wasm` and the
  `treeweave.mjs` loader, so a web page can fetch them from a stable URL without npm.
- The npm package now ships prebuilt native N-API binaries (Linux x64/arm64,
  macOS arm64/x64, Windows x64), resolved by `node-gyp-build`, so Node consumers
  get the fast native backend automatically; the bundled WASM build remains the
  browser backend and the fallback for any unmatched host. Built by the new
  reusable `_build-node-prebuilds.yml` (Linux in manylinux_2_28, same as wheels)
  and bundled into the package by `release.yml`'s `build-js` job.

### Deferred (roadmap, not yet implemented)

- conda-forge: submit `conda/recipe` to `conda-forge/staged-recipes`, so that
  `conda install -c conda-forge treeweave` becomes the one-liner (add a conda
  badge).
- Julia General registry: register so `Pkg.add("Treeweave")` works. That needs a
  tagged release plus Registrator/JLL, or the bespoke `build.jl` stays.
- vcpkg and Conan C/C++ ports.
- MATLAB File Exchange / Add-On packaging.
- Fortran / Octave remain build-from-source (Octave has no stable MEX ABI).

## [0.0.0] - 2026-06-11

- Initial public release of `treeweave`: an adaptive polynomial-tree function
  approximator with a header-only C++ API (`treeweave::treeweave`), a relocatable
  C ABI (`libtreeweave_c`, `find_package(treeweave)`), and Python, Julia,
  Fortran, MATLAB/Octave, and JavaScript/TypeScript wrappers.
- Prebuilt distributions: Python wheels on PyPI (x86-64 wheels dispatch
  SSE4.2 / AVX2 / AVX-512 at runtime; aarch64 + Apple-silicon wheels), and
  relocatable C-ABI tarballs attached to the GitHub Release for every supported
  OS/arch. The Julia package downloads the matching prebuilt `libtreeweave_c`
  from the Release on `Pkg.add`.
