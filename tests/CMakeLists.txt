# tests/CMakeLists.txt — Catch2 test suite wiring. Added via add_subdirectory()
# from the top-level CMakeLists, guarded by TREEWEAVE_BUILD_TESTS there.

# Keep test executables in the build root (where they lived before tests/ became
# its own subdirectory) rather than under build/tests/.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")

# Static analysis targets the library, not the test suite. Test code legitimately
# uses idioms clang-tidy/cppcheck flag as smells (constant RNG seeds for
# reproducibility, C arrays to drive the C ABI, mutable instrumentation globals),
# so clear the analysis tools for this subdirectory only. add_subdirectory gives
# this CMakeLists its own scope, so the library targets are unaffected.
set(CMAKE_CXX_CLANG_TIDY "")
set(CMAKE_CXX_CPPCHECK "")

include(CTest)
include(Catch)

function(_treeweave_add_cpp_test name source)
    add_executable(${name} ${source})
    target_include_directories(${name} PRIVATE ${PROJECT_SOURCE_DIR}/include)
    target_link_libraries(
        ${name}
        PRIVATE Catch2::Catch2WithMain polyfit::polyfit poet::poet
    )
    treeweave_enable_warnings(${name})
    # Catch2's TEST_CASE expands to a use of __COUNTER__, which bleeding-edge
    # clang flags as a C2y extension; the warning is harmless and out of our
    # control (lives in Catch2 macros), so silence it on test targets only.
    target_compile_options(
        ${name}
        PRIVATE
            $<$<AND:$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>,$<VERSION_GREATER_EQUAL:$<CXX_COMPILER_VERSION>,19>>:-Wno-c2y-extensions>
    )
    catch_discover_tests(${name})
    # Record the target so the coverage target (defined in dev_helpers.cmake,
    # before this subdirectory) can depend on every test executable.
    set_property(GLOBAL APPEND PROPERTY TREEWEAVE_TEST_TARGETS ${name})
endfunction()

_treeweave_add_cpp_test(test_template test_template.cpp)
_treeweave_add_cpp_test(test_cpp      test_cpp.cpp)
_treeweave_add_cpp_test(test_quantize test_quantize.cpp)
_treeweave_add_cpp_test(test_greens   test_greens.cpp)
_treeweave_add_cpp_test(test_threadsafe test_threadsafe.cpp)
find_package(Threads REQUIRED)
target_link_libraries(test_threadsafe PRIVATE Threads::Threads)
_treeweave_add_cpp_test(test_allocator test_allocator.cpp)

# C ABI parity tests — link the C library built by treeweave_c_api.cmake.
if(TREEWEAVE_BUILD_C_API)
    _treeweave_add_cpp_test(test_c test_c.cpp)
    # test_c.cpp spins std::threads to assert treeweave_last_error() is
    # thread-local across the C ABI, so it needs the threading runtime too.
    target_link_libraries(test_c PRIVATE treeweave_c Threads::Threads)
endif()

# Make the coverage target build every test executable before it runs CTest.
if(TARGET coverage)
    get_property(_treeweave_test_targets GLOBAL PROPERTY TREEWEAVE_TEST_TARGETS)
    add_dependencies(coverage ${_treeweave_test_targets})
endif()
