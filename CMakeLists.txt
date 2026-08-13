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

# BEGIN TREEWEAVE_CMAKE_OPTIONS_DOCS
# Build example programs. Defaults ON for a direct treeweave checkout so
# developers can run examples immediately; defaults OFF for FetchContent and
# add_subdirectory consumers so vendoring treeweave stays small.
option(TREEWEAVE_BUILD_EXAMPLES "Build C++ examples" ${PROJECT_IS_TOP_LEVEL})
# Build the C++/C test suites. Defaults ON only for a direct treeweave checkout;
# consumers opt in explicitly when they want to test the vendored dependency.
option(TREEWEAVE_BUILD_TESTS "Build tests" ${PROJECT_IS_TOP_LEVEL})
# Build the C ABI libraries. This provides treeweave::treeweave_c
# (shared, when the platform supports shared libraries) and
# treeweave::treeweave_c_static. Most non-C++ bindings link through this ABI.
option(TREEWEAVE_BUILD_C_API "Build C ABI" ON)
# Build the Python nanobind extension and register the pytest smoke/parity
# target. Normal Python users should prefer the PyPI wheel.
option(TREEWEAVE_BUILD_PYTHON "Build Python binding" OFF)
# Register the Julia binding test suite against the C ABI. Normal Julia users
# should prefer Pkg.add, which downloads the release C ABI automatically.
option(TREEWEAVE_BUILD_JULIA "Build Julia tests" OFF)
# Build the MATLAB/Octave MEX binding and register its smoke test. The
# bindings-matlab and bindings-octave presets are the preferred entry points.
option(TREEWEAVE_BUILD_MATLAB "Build MATLAB/Octave MEX" OFF)
# Build the Fortran iso_c_binding module plus its example and tests. Requires a
# modern Fortran compiler on PATH or CMAKE_Fortran_COMPILER set explicitly.
option(TREEWEAVE_BUILD_FORTRAN "Build Fortran binding" OFF)
# Build the JavaScript/TypeScript binding. With a normal compiler this builds
# the native N-API addon; with the Emscripten preset it builds the WASM backend.
option(TREEWEAVE_BUILD_JS "Build JS/TS binding" OFF)
# Build local performance benchmark executables. OFF by default: the benchmark
# sources are not clang-tidy clean, so a top-level default would drag them into
# the analysis preset. Turn on via the bench preset.
option(TREEWEAVE_BUILD_BENCHMARKS "Build benchmarks" OFF)
# Build the CodSpeed benchmark used by CI. Prefer the codspeed preset.
option(TREEWEAVE_BUILD_CODSPEED "Build CodSpeed benchmark" OFF)
# Add documentation targets: doxygen, sphinx, and docs. Requires Doxygen,
# sphinx-build, and the Python packages from docs/requirements.txt.
option(TREEWEAVE_GENERATE_DOCS "Build docs targets" OFF)
# Build the C ABI with runtime ISA dispatch where the platform supports it. On
# x86 this builds a portable baseline plus wider variants and selects at runtime
# (GCC/Clang: SSE2/SSE4.2/AVX2/AVX-512; MSVC ABI: SSE2/AVX/AVX2/AVX-512). On
# unsupported platforms it falls back to the normal single-arch C ABI build.
option(TREEWEAVE_C_MULTIARCH "Runtime ISA dispatch" OFF)
# Treat project warnings as errors. Dependency warnings are kept out of this
# policy; this is intended to keep treeweave's own code clean.
option(TREEWEAVE_WARNINGS_AS_ERRORS "Treat warnings as errors" ON)
# Enable ASan/UBSan for project targets. Prefer sanitizer presets where
# available because they pick compiler/build settings known to fit CI memory.
option(TREEWEAVE_ENABLE_SANITIZERS "Enable ASan/UBSan" OFF)
# Run clang-tidy/cppcheck. OFF by default: as a global CMAKE_CXX_CLANG_TIDY it
# would also lint vendored deps configured outside the FetchContent guard (e.g.
# nanobind in the Python preset). Turn on via the analysis preset.
option(TREEWEAVE_ENABLE_STATIC_ANALYSIS "Run static analysis" OFF)
# Build with coverage instrumentation. Prefer the coverage preset.
option(TREEWEAVE_ENABLE_COVERAGE "Build coverage" OFF)
# Link-time optimization, OFF by default: the header-only C++ API gains nothing
# and IPO fills the installed static archive with single-toolchain IL. gcc and
# MSVC ignore it when ON; see cmake/treeweave_toolchain.cmake.
option(TREEWEAVE_ENABLE_IPO "Link-time optimization" OFF)
# END TREEWEAVE_CMAKE_OPTIONS_DOCS

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

include(treeweave_toolchain) # C++ standard, opt-in IPO, -march/-mtune, FP
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
