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

# ---------------------------------------------------------------------------
# User-facing options
# ---------------------------------------------------------------------------
# Default ON only when treeweave is the top-level project. A consumer that pulls
# treeweave in via add_subdirectory()/FetchContent() gets neither the examples
# nor the Catch2 test suite (nor its GoogleTest/Catch2 fetch) unless it asks —
# `find_package`/FetchContent stay baggage-free. PROJECT_IS_TOP_LEVEL is set by
# project() on CMake >= 3.21 (we require 3.25).
option(TREEWEAVE_BUILD_EXAMPLES "Build C++ examples" ${PROJECT_IS_TOP_LEVEL})
option(TREEWEAVE_BUILD_TESTS "Build tests" ${PROJECT_IS_TOP_LEVEL})
option(TREEWEAVE_BUILD_C_API "Build the C ABI (libtreeweave_c) + C example" ON)
# Performance benchmarks (nanobench/perf drivers) are slow, heavily-templated
# TUs that no normal build needs — OFF by default so a plain `cmake` build (and
# the whole CI test matrix) stays fast. The dedicated `bench` preset/workflow
# turns it ON.
option(TREEWEAVE_BUILD_BENCHMARKS "Build performance benchmarks" OFF)
# CodSpeed benchmark (Google Benchmark + CodSpeedHQ/codspeed-cpp compat layer)
# for instruction-count perf gating in CI (codspeed.yml). OFF by default — the
# dedicated `codspeed` preset/workflow turns it ON; a normal build never fetches
# Google Benchmark.
option(TREEWEAVE_BUILD_CODSPEED "Build the CodSpeed benchmark" OFF)

# Quality / tooling
option(TREEWEAVE_WARNINGS_AS_ERRORS "Treat warnings as errors" ON)
option(TREEWEAVE_ENABLE_SANITIZERS "Enable ASan/UBSan" OFF)
option(TREEWEAVE_ENABLE_STATIC_ANALYSIS "Run clang-tidy/cppcheck" OFF)
option(TREEWEAVE_ENABLE_COVERAGE "Build with coverage instrumentation" OFF)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

# ---------------------------------------------------------------------------
# Wiring — each module's internals live in its own cmake/*.cmake file, and
# the buildable subtrees (examples, tests) in their own CMakeLists.txt.
# ---------------------------------------------------------------------------
include(treeweave_toolchain) # C++ standard, IPO, -march/-mtune, FP-contraction
include(dev_helpers) # warnings / sanitizers / static analysis helpers
include(treeweave_deps) # fetch polyfit, POET, Catch2
include(treeweave_c_api) # C ABI (libtreeweave_c) — built before examples/tests

if(TREEWEAVE_BUILD_EXAMPLES)
    add_subdirectory(examples) # C++ examples
endif()
if(TREEWEAVE_BUILD_TESTS)
    add_subdirectory(tests) # Catch2 suites
endif()

include(treeweave_bindings) # optional Python / Julia / MATLAB wrappers (default OFF)
include(treeweave_install) # install rules
