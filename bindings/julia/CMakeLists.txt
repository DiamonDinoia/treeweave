# bindings/julia — no build step; the package dlopen()s libtreeweave_c at test
# time. We just point it at the freshly-built shared library via LIBTREEWEAVE_C.
# Added by cmake/treeweave_bindings.cmake when TREEWEAVE_BUILD_JULIA=ON.

find_program(TREEWEAVE_JULIA_EXECUTABLE julia)
if(NOT TREEWEAVE_JULIA_EXECUTABLE)
    message(STATUS "treeweave[julia]: julia not on PATH — skipping.")
    return()
endif()

add_test(
    NAME julia_treeweave
    COMMAND
        "${TREEWEAVE_JULIA_EXECUTABLE}"
        "--project=${CMAKE_CURRENT_SOURCE_DIR}/Treeweave"
        "${CMAKE_CURRENT_SOURCE_DIR}/Treeweave/test/runtests.jl"
)
set_tests_properties(
    julia_treeweave
    PROPERTIES ENVIRONMENT "LIBTREEWEAVE_C=$<TARGET_FILE:treeweave_c>"
)
message(STATUS "treeweave[julia]: test 'julia_treeweave' registered.")
