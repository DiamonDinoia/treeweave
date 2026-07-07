# bindings/fortran — a thin iso_c_binding module (treeweave.f90) plus a
# self-checking test and an example, both linking the shared treeweave_c. A
# missing Fortran compiler degrades to a STATUS message and a skipped test.
# Added by cmake/treeweave_bindings.cmake when TREEWEAVE_BUILD_FORTRAN=ON.
#
# COMPILER SELECTION FOOTGUN: CMake probe (check_language/enable_language) picks
# whichever Fortran compiler appears first in PATH. On Flatiron / RHEL 8 the
# system default is /usr/bin/f95 (gfortran 8.5), but the binding uses
# `error stop <integer>` which requires Fortran 2018 (not 2008). With gfortran 8
# that compiles without warning but produces wrong behavior. Always pass a
# modern compiler explicitly:
#
#   cmake ... -DCMAKE_Fortran_COMPILER=$(which gfortran)  # after `module load gcc/13.3.0`
#
# or set FC in the environment before running CMake. Verify with:
#   gfortran --version   # must be >= 10; >= 13 preferred

include(CheckLanguage)
check_language(Fortran)
if(NOT CMAKE_Fortran_COMPILER)
    message(STATUS "treeweave[fortran]: no Fortran compiler found — skipping.")
    return()
endif()

enable_language(Fortran)

# The binding uses `error stop <integer-expression>`, a Fortran 2018 feature
# (in Fortran 2008 the stop-code must be a default-integer scalar constant
# expression, which the integer literal satisfies, but the non-constant-expr
# form and the negative-stop-code behaviour are 2018). Declare 2018 so that
# compilers emit the correct diagnostics when the standard is violated.
set(CMAKE_Fortran_STANDARD 2018)
set(CMAKE_Fortran_STANDARD_REQUIRED ON)

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

add_executable(
    treeweave_fortran_zeta_bench
    "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/zeta_bench.f90"
)
target_link_libraries(treeweave_fortran_zeta_bench PRIVATE treeweave_fortran)

add_test(NAME fortran_treeweave COMMAND treeweave_fortran_test)
add_test(NAME fortran_example COMMAND treeweave_fortran_example)
add_test(NAME fortran_zeta_bench COMMAND treeweave_fortran_zeta_bench)
message(
    STATUS
    "treeweave[fortran]: tests 'fortran_treeweave', 'fortran_example', 'fortran_zeta_bench' registered "
    "(${CMAKE_Fortran_COMPILER})."
)
