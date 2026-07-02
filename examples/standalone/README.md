# Standalone C++ — header-only drop-in (no CMake)

The simplest possible way to use treeweave from C++: download the headers,
extract, `-Iinclude`, compile. No CMake, no build of treeweave itself — the C++
API is header-only, and the release bundle already carries every transitive
dependency (polyfit, POET, xsimd, mdspan) under one `include/`.

```bash
wget https://github.com/DiamonDinoia/treeweave/releases/latest/download/treeweave-cxx-headers.tar.gz
tar xzf treeweave-cxx-headers.tar.gz              # -> ./include/treeweave/..., ./include/polyfit/..., ...
make                                              # builds ./demo
./demo
```

The `latest/download` URL always resolves to the newest release; pin a version
with `.../releases/download/vX.Y.Z/treeweave-cxx-headers.tar.gz`.

`make` is a one-line convenience; the compile it runs is just:

```bash
g++ -std=c++20 -O3 -march=native demo.cpp -Iinclude -o demo
```

`demo.cpp` fits `zeta` once and prints `f(x)` plus the relative error (~1e-12).
This directory sits *next to* the extracted `include/` on purpose — it models
your own project, so it can be copied anywhere.

Other ways in (all documented in [`../../docs/install.rst`](../../docs/install.rst)):

- **Already using CMake?** `FetchContent` / `CPMAddPackage` / `find_package` —
  see the [top-level README](../../README.md).
- **Building the repo yourself?** The same consolidated headers land in
  `build/include`, so `-Ibuild/include` works without a release download.
- **Need the C ABI too?** The per-platform `treeweave-<ver>-<platform>` tarballs
  carry the identical `include/` alongside `libtreeweave_c`.
