# examples

Every file in `c++/` and `C/` is a ctest and exits nonzero on a wrong answer, so
a broken example is a red build, not a stale page. CMake registers each one by
globbing the directory (`cpp_example_<name>`, `c_example_<name>`), so adding a
file is enough to add a test. They build when treeweave is the top-level
project, `TREEWEAVE_BUILD_EXAMPLES=ON`.

`quickstart/` holds four complete consumer projects, one per install route,
which `tools/ci/install-test.sh` configures, builds and runs on every pull
request.

Read [`docs/guides/cpp.rst`](../docs/guides/cpp.rst) for the C++ API and the
build recipes, [`docs/guides/c.rst`](../docs/guides/c.rst) for the C ABI.
Performance drivers live in [`../benchmarks/`](../benchmarks/).
