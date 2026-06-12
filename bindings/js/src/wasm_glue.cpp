// wasm_glue.cpp — translation unit for the Emscripten/WASM backend of the JS
// binding. The browser backend is the same treeweave C ABI compiled to WASM;
// xsimd auto-selects its `xsimd::wasm` SIMD128 backend under emcc.
//
// There is deliberately almost nothing here: the public surface is the C ABI
// itself, exported by name via the linker's -sEXPORTED_FUNCTIONS list (see
// CMakeLists.txt), which also pulls the needed members out of the
// treeweave_c_static archive. This file only needs to exist so the link has a
// root object; it intentionally has no `main` (the module is a library, built
// with --no-entry). Including the header keeps the version macros and the ABI
// declarations visible to anyone extending this glue later.

#include <treeweave.h>
