# treeweave_bindings.cmake — optional host-language wrappers (Python / Julia /
# MATLAB / Fortran) over the C ABI (libtreeweave_c).
#
# Each language is guarded by its own option (all default OFF) and implemented
# in its own nested bindings/<lang>/CMakeLists.txt, so a plain `cmake ..` never
# pays for a toolchain the user doesn't have. A missing toolchain degrades to a
# STATUS message and a skipped test rather than a hard configure error.
#
# The wrappers all reuse the already-instantiated C ABI:
#   * Python links the treeweave_c_static archive into a nanobind extension;
#   * Julia dlopen()s the treeweave_c shared library at runtime;
#   * MATLAB/Octave link treeweave_c_static into an mwrap-generated MEX gateway;
#   * Fortran links the shared treeweave_c via a thin iso_c_binding module.
# So nothing here re-instantiates the heavy template shapes.

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

# Bindings link the static C ABI archive; the shared treeweave_c is optional and
# absent where the platform has no dynamic linking (Emscripten/WASM JS backend).
if(NOT TARGET treeweave_c_static)
    message(
        WARNING
        "treeweave: bindings requested but the C ABI static target is absent "
        "(set TREEWEAVE_BUILD_C_API=ON). Skipping all bindings."
    )
    return()
endif()

enable_testing()

# Each binding lives in its own nested CMakeLists.txt under bindings/<lang>.
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
