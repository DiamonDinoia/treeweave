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
        "--project=${CMAKE_CURRENT_SOURCE_DIR}/Treeweave" "-e"
        "import Pkg; Pkg.test(coverage=false)"
)
set_tests_properties(
    julia_treeweave
    PROPERTIES ENVIRONMENT "LIBTREEWEAVE_C=$<TARGET_FILE:treeweave_c>"
)

foreach(_example IN ITEMS example_1d example_2d_scalar example_2d_vector)
    add_test(
        NAME "julia_example_${_example}"
        COMMAND
            "${TREEWEAVE_JULIA_EXECUTABLE}"
            "--project=${CMAKE_CURRENT_SOURCE_DIR}/Treeweave"
            "${CMAKE_CURRENT_SOURCE_DIR}/Treeweave/examples/${_example}.jl"
    )
    set_tests_properties(
        "julia_example_${_example}"
        PROPERTIES ENVIRONMENT "LIBTREEWEAVE_C=$<TARGET_FILE:treeweave_c>"
    )
endforeach()

message(STATUS "treeweave[julia]: test 'julia_treeweave' registered.")
