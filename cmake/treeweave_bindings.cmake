# treeweave_bindings.cmake — optional language wrappers over the C ABI.
# Each guarded by its own option (default OFF); missing toolchain → STATUS + skip.
# (see devel/agents/build-notes.md — "Bindings overview")

include_guard(GLOBAL)

option(
    TREEWEAVE_BUILD_PYTHON
    "Build the Python (nanobind) bindings + register pytest"
    OFF
)
option(TREEWEAVE_BUILD_JULIA "Register the Julia binding test suite" OFF)
option(
    TREEWEAVE_BUILD_MATLAB
    "Build the MATLAB/Octave (MEX) bindings + register the test"
    OFF
)
option(
    TREEWEAVE_BUILD_FORTRAN
    "Build the Fortran (iso_c_binding) binding test + example"
    OFF
)
option(
    TREEWEAVE_BUILD_JS
    "Build the JavaScript/TypeScript binding (native N-API addon, or WASM under emcc) + register the node test"
    OFF
)

if(
    NOT (
        TREEWEAVE_BUILD_PYTHON
        OR TREEWEAVE_BUILD_JULIA
        OR TREEWEAVE_BUILD_MATLAB
        OR TREEWEAVE_BUILD_FORTRAN
        OR TREEWEAVE_BUILD_JS
    )
)
    return()
endif()

if(NOT TARGET treeweave_c_static)
    message(
        WARNING
        "treeweave: bindings requested but the C ABI static target is absent "
        "(set TREEWEAVE_BUILD_C_API=ON). Skipping all bindings."
    )
    return()
endif()

enable_testing()

if(TREEWEAVE_BUILD_PYTHON)
    add_subdirectory(bindings/python)
endif()
if(TREEWEAVE_BUILD_JULIA)
    add_subdirectory(bindings/julia)
endif()
if(TREEWEAVE_BUILD_MATLAB)
    add_subdirectory(bindings/matlab)
endif()
if(TREEWEAVE_BUILD_FORTRAN)
    add_subdirectory(bindings/fortran)
endif()
if(TREEWEAVE_BUILD_JS)
    add_subdirectory(bindings/js)
endif()
