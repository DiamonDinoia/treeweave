# examples

C++ and C examples for treeweave. CMake builds them when treeweave is the
top-level project (`TREEWEAVE_BUILD_EXAMPLES=ON`). Each example is also a
ctest, and each self-checks by returning a non-zero exit code on failure.

## C++ examples (`c++/`)

| file | what it shows |
|------|---------------|
| `simple1d.cpp` | 1-D scalar fit |
| `simple2d.cpp` | 2-D scalar fit |
| `simple3d.cpp` | 3-D scalar fit |
| `with_options.cpp` | custom tolerance / options |
| `vector_output.cpp` | multi-output (vector-valued) fit |
| `hankel.cpp` | Hankel H0^(1), complex-valued / 2-output fit |

## C examples (`C/`)

Pure-C programs that link `libtreeweave_c`. Each is also a ctest
(`c_example_<name>`).

## standalone/

Header-only usage without CMake; see `standalone/Makefile`.

## Benchmarks

Performance drivers live in `../benchmarks/`.
