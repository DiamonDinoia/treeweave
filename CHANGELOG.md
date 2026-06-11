# Changelog

All notable changes to `treeweave` are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

The `## [X.Y.Z]` section matching a release is sliced verbatim into that
release's GitHub Release notes by the Release workflow (`.github/workflows/release.yml`).

## [Unreleased]

## [0.0.0] - 2026-06-11

- Initial public release of `treeweave`: an adaptive polynomial-tree function
  approximator with a header-only C++ API (`treeweave::treeweave`), a relocatable
  C ABI (`libtreeweave_c`, `find_package(treeweave)`), and Python, Julia,
  Fortran, MATLAB, and Octave wrappers.
- Prebuilt distributions: Python wheels on PyPI (x86-64 wheels dispatch
  SSE4.2 / AVX2 / AVX-512 at runtime; aarch64 + Apple-silicon wheels), and
  relocatable C-ABI tarballs attached to the GitHub Release for every supported
  OS/arch. The Julia package downloads the matching prebuilt `libtreeweave_c`
  from the Release on `Pkg.add`.
