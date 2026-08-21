# treeweave_bindings.cmake: optional language wrappers over the C ABI.
# Options live in the top-level CMakeLists.txt; missing toolchain → STATUS + skip.

include_guard(GLOBAL)

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
