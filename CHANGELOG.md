# Changelog

All notable changes to `treeweave` are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

The `## [X.Y.Z]` section matching a release is sliced verbatim into that
release's GitHub Release notes by the Release workflow (`.github/workflows/release.yml`).

## [Unreleased]

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
