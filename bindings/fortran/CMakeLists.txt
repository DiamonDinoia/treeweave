# bindings/fortran — a thin iso_c_binding module (treeweave.f90) plus a
# self-checking test and an example, both linking the shared treeweave_c. A
# missing Fortran compiler degrades to a STATUS message and a skipped test.
# Added by cmake/treeweave_bindings.cmake when TREEWEAVE_BUILD_FORTRAN=ON.

include(CheckLanguage)
check_language(Fortran)
if(NOT CMAKE_Fortran_COMPILER)
    message(STATUS "treeweave[fortran]: no Fortran compiler found — skipping.")
    return()
endif()

enable_language(Fortran)

# Compile the binding module once into a small static library; both the test and
# the example link it. (Compiling treeweave.f90 in two targets would make
# Ninja's Fortran dyndep see two rules generating treeweave.mod.) Linking this
# library also propagates its module directory to consumers.
add_library(
    treeweave_fortran
    STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/treeweave.f90"
)
target_link_libraries(treeweave_fortran PUBLIC treeweave_c)

add_executable(
    treeweave_fortran_test
    "${CMAKE_CURRENT_SOURCE_DIR}/test_treeweave.f90"
)
target_link_libraries(treeweave_fortran_test PRIVATE treeweave_fortran)

add_executable(
    treeweave_fortran_example
    "${CMAKE_CURRENT_SOURCE_DIR}/example.f90"
)
target_link_libraries(treeweave_fortran_example PRIVATE treeweave_fortran)

add_test(NAME fortran_treeweave COMMAND treeweave_fortran_test)
message(
    STATUS
    "treeweave[fortran]: test 'fortran_treeweave' + example registered "
    "(${CMAKE_Fortran_COMPILER})."
)
