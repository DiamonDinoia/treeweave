# Changelog

All notable changes to `treeweave` are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

The `## [X.Y.Z]` section matching a release is sliced verbatim into that
release's GitHub Release notes by the Release workflow (`.github/workflows/release.yml`).

## [Unreleased]

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
  preset already did this — `-O0` leaves the header-only, deeply-inlined code
  un-inlined and the compute-heavy tests slow to a crawl — but a preset-less
  `-DCMAKE_BUILD_TYPE=Debug`, which is what a consumer's `FetchContent` build
  gets, did not. The default now applies to every `Debug` build including
  coverage, where line attribution becomes slightly coarser wherever the
  optimizer inlines but the instrumented run stops being the slowest job in CI.
  An explicit `CMAKE_CXX_FLAGS_DEBUG` still takes precedence, so
  `-DCMAKE_CXX_FLAGS_DEBUG='-O0 -g'` restores the strictest line data.
- Link-time optimization is now the opt-in option `TREEWEAVE_ENABLE_IPO`,
  default `OFF`, where it was on for every non-GCC compiler. The C++ API is
  header-only, so IPO changes nothing for a consumer that only includes the
  headers, and the C ABI objects feed the installed static archive as well as
  `libtreeweave_c.so` — IPO fills that archive with compiler IL only one
  toolchain version can link. Every binding preset turns it back on, because a
  binding ships one shared artifact and no archive; the Emscripten preset and
  GCC stay excluded. This also removes an MSVC `/LTCG` link that took over an
  hour per test executable.

## [0.0.1] - 2026-07-20

### Fixed

- Julia bindings `Project.toml` carried `version = "0.0.0"`, which Julia's Pkg
  rejects as an invalid version (`Pkg.add` fails). Bumped to `0.0.1`.
- The Windows N-API prebuild now builds: the workflow fetches the node C headers
  (absent from the Windows node install) and `node.lib` from the node dist, and
  the addon delay-loads `node.exe` to resolve `napi_*` under MSVC (the node-gyp /
  cmake-js approach). Windows Node users get the native backend, not just the
  WASM fallback.

### Added

- Documentation now defaults to released/prebuilt install paths, with source
  builds moved to the end of each language guide for development or unreleased
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
- Post-publish `release-install.yml` now verifies the **published** binaries on a
  full matrix: `pip install treeweave` from PyPI (linux/macOS/windows × py3.9 +
  py3.12), each per-platform C-ABI release tarball via `find_package`
  (linux-x86_64/-aarch64, macOS-arm64, windows-x64), and a new
  `npm install @flatironinstitute/treeweave` job (WASM-only, all three OSes).
- One-liner installs surfaced in the README and install guide: `pip install
  treeweave`, `npm install @flatironinstitute/treeweave`, release tarball
  `curl`/`wget` commands, Julia `Pkg.add`, and
  `CPMAddPackage("gh:DiamonDinoia/treeweave@stable")` for C++.
- `conda/recipe/meta.yaml` + `conda/README.md`: a conda-forge recipe prepared for
  submission to `conda-forge/staged-recipes` (manual follow-up).
- The raw `treeweave.wasm` + `treeweave.mjs` loader are attached to each GitHub
  Release, so a web page can fetch them from a stable URL without npm.
- The npm package now ships **prebuilt native N-API binaries** (Linux x64/arm64,
  macOS arm64/x64, Windows x64), resolved by `node-gyp-build`, so Node consumers
  get the fast native backend automatically; the bundled WASM build remains the
  browser backend and the fallback for any unmatched host. Built by the new
  reusable `_build-node-prebuilds.yml` (Linux in manylinux_2_28, same as wheels)
  and bundled into the package by `release.yml`'s `build-js` job.

### Deferred (roadmap — not yet implemented)

- **conda-forge:** submit `conda/recipe` to `conda-forge/staged-recipes`; then
  `conda install -c conda-forge treeweave` is the one-liner (add a conda badge).
- **Julia General registry:** register so `Pkg.add("Treeweave")` works (needs a
  tagged release + Registrator/JLL, or keep the bespoke `build.jl`).
- **vcpkg / Conan:** C/C++ ports.
- **MATLAB File Exchange / Add-On** packaging.
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
