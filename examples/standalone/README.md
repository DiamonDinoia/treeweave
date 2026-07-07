# Standalone C++ — header-only drop-in (no CMake)

Header-only C++ — no CMake. The release bundle carries all transitive headers (polyfit, POET, xsimd, mdspan) under one `include/`.

```bash
wget https://github.com/DiamonDinoia/treeweave/releases/latest/download/treeweave-cxx-headers.tar.gz
tar xzf treeweave-cxx-headers.tar.gz              # -> ./include/treeweave/..., ./include/polyfit/..., ...
make                                              # builds ./demo
./demo
```




```bash
g++ -std=c++20 -O3 -march=native demo.cpp -Iinclude -o demo
```

`demo.cpp` fits `zeta` once and prints `f(x)` plus the relative error (~1e-12).

Other ways in (all documented in [`../../docs/install.rst`](../../docs/install.rst)):

- **Already using CMake?** `FetchContent` / `CPMAddPackage` / `find_package` —
  see the [top-level README](../../README.md).
- **Building the repo yourself?** The same consolidated headers land in
  `build/include`, so `-Ibuild/include` works without a release download.
- **Need the C ABI too?** The per-platform `treeweave-<ver>-<platform>` tarballs
  carry the identical `include/` alongside `libtreeweave_c`.
