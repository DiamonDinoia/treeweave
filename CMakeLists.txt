cmake_minimum_required(VERSION 3.25...3.31)

# Single source of truth for the version is the tracked VERSION file; project()
# takes its MAJOR.MINOR.PATCH from it, and treeweave_generate_version.cmake
# (re)writes the committed include/treeweave_version.h from it + git state.
file(
    STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION"
    TREEWEAVE_BASE_VERSION
    LIMIT_COUNT 1
)
string(STRIP "${TREEWEAVE_BASE_VERSION}" TREEWEAVE_BASE_VERSION)
project(treeweave VERSION "${TREEWEAVE_BASE_VERSION}" LANGUAGES CXX)
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/treeweave_generate_version.cmake")

# Options default ON only at top level; add_subdirectory/FetchContent consumers
# get a clean build without tests/examples unless they opt in.
option(TREEWEAVE_BUILD_EXAMPLES "Build C++ examples" ${PROJECT_IS_TOP_LEVEL})
option(TREEWEAVE_BUILD_TESTS "Build tests" ${PROJECT_IS_TOP_LEVEL})
option(TREEWEAVE_BUILD_C_API "Build the C ABI (libtreeweave_c) + C example" ON)
# OFF by default; turned ON by the bench preset/workflow only.
option(TREEWEAVE_BUILD_BENCHMARKS "Build performance benchmarks" OFF)
# CodSpeed (Google Benchmark + codspeed-cpp); turned ON by codspeed.yml workflow.
option(TREEWEAVE_BUILD_CODSPEED "Build the CodSpeed benchmark" OFF)

# Quality / tooling
option(TREEWEAVE_WARNINGS_AS_ERRORS "Treat warnings as errors" ON)
option(TREEWEAVE_ENABLE_SANITIZERS "Enable ASan/UBSan" OFF)
option(TREEWEAVE_ENABLE_STATIC_ANALYSIS "Run clang-tidy/cppcheck" OFF)
option(TREEWEAVE_ENABLE_COVERAGE "Build with coverage instrumentation" OFF)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

include(treeweave_toolchain) # C++ standard, IPO, -march/-mtune, FP-contraction
include(dev_helpers) # warnings / sanitizers / static analysis helpers
include(treeweave_deps) # fetch polyfit, POET, Catch2
include(treeweave_c_api) # C ABI (libtreeweave_c) — built before examples/tests
include(treeweave_bundle) # consolidate C++ headers + deps into <build>/include

if(TREEWEAVE_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
if(TREEWEAVE_BUILD_BENCHMARKS OR TREEWEAVE_BUILD_CODSPEED)
    add_subdirectory(benchmarks)
endif()
if(TREEWEAVE_BUILD_TESTS)
    add_subdirectory(tests)
endif()

include(treeweave_bindings)
include(treeweave_install)
