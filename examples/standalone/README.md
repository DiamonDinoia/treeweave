# Standalone C++: header-only drop-in (no CMake)

The release bundle carries every transitive header (polyfit, POET, xsimd, mdspan) under one `include/`.

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

Other ways to reach the same headers, all covered in
[`../../docs/install.rst`](../../docs/install.rst):

- A CMake project uses `FetchContent`, `CPMAddPackage` or `find_package`, see
  the [top-level README](../../README.md).
- A source build of this repo lands the same consolidated headers in
  `build/include`, so `-Ibuild/include` works without a release download.
- The per-platform `treeweave-<ver>-<platform>` tarballs carry the identical
  `include/` alongside `libtreeweave_c`, for projects that also need the C
  ABI.
