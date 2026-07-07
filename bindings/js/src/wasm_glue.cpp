// wasm_glue.cpp — root TU for the Emscripten/WASM backend (--no-entry library).
// The public surface is the C ABI, exported via -sEXPORTED_FUNCTIONS in CMakeLists.txt; this file exists only to anchor
// the link. (see devel/agents/build-notes.md)

#include <treeweave.h>
