# treeweave_c_api.cmake — C ABI (libtreeweave_c) for the new fit/options surface.
#
# Builds a shared and a static library from src/capi/*.cpp (each per-(dtype,
# input_dim) dispatch TU compiles in parallel) plus the extern "C" shim, and
# wires the C smoke example. The global -march/-ffp-contract flags from the
# top CMakeLists already apply; nothing ISA-specific is added here.

include_guard(GLOBAL)

# ---------------------------------------------------------------------------
# Header-only C++ template API.
#
# `treeweave::treeweave` carries the `include/` tree plus the transitive polyfit /
# POET headers it instantiates against. Those deps are FetchContent-only (not
# separately installable), so this target is for in-tree consumers
# (add_subdirectory / FetchContent) — it is deliberately NOT part of the
# installed `find_package(treeweave)` export set. The installed package ships the
# self-contained C ABI (`treeweave::treeweave_c`) instead. See treeweave_install.cmake.
# ---------------------------------------------------------------------------
add_library(treeweave_headers INTERFACE)
add_library(treeweave::treeweave ALIAS treeweave_headers)
target_include_directories(
    treeweave_headers
    INTERFACE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(treeweave_headers INTERFACE polyfit::polyfit poet::poet)
target_compile_features(treeweave_headers INTERFACE cxx_std_20)

if(NOT TREEWEAVE_BUILD_C_API)
    return()
endif()

# The C-ABI translation units (generation + per-arch fan-out) are owned by
# treeweave_c_dispatch.cmake, which compiles them into OBJECT libraries and
# publishes their object-file generator expressions on TREEWEAVE_C_OBJECT_EXPRS.
# Both libraries below are assembled from that one object set, so every TU is
# compiled exactly once and shared between the shared and static libraries.
include(treeweave_c_dispatch)
get_property(_treeweave_c_objects GLOBAL PROPERTY TREEWEAVE_C_OBJECT_EXPRS)

function(_treeweave_configure_c_target tgt)
    target_include_directories(
        ${tgt}
        PUBLIC
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    set_property(TARGET ${tgt} PROPERTY POSITION_INDEPENDENT_CODE ON)
endfunction()

add_library(treeweave_c SHARED ${_treeweave_c_objects})
add_library(treeweave::treeweave_c ALIAS treeweave_c)
_treeweave_configure_c_target(treeweave_c)
set_target_properties(
    treeweave_c
    PROPERTIES VERSION ${PROJECT_VERSION} SOVERSION ${PROJECT_VERSION_MAJOR}
)
set_property(GLOBAL APPEND PROPERTY TREEWEAVE_INSTALL_TARGETS treeweave_c)

add_library(treeweave_c_static STATIC ${_treeweave_c_objects})
add_library(treeweave::treeweave_c_static ALIAS treeweave_c_static)
_treeweave_configure_c_target(treeweave_c_static)
# On Unix the shared (libtreeweave_c.so) and static (libtreeweave_c.a) archives differ
# by extension, so both can be named treeweave_c. On Windows the shared library's
# *import* library is also treeweave_c.lib, which would collide with the static
# archive's treeweave_c.lib in the same output directory — and the clobbered file
# breaks consumers (they link the static archive but expect the DLL's __imp_
# symbols). Keep the static archive's basename distinct there.
if(WIN32)
    set_target_properties(
        treeweave_c_static
        PROPERTIES OUTPUT_NAME treeweave_c_static
    )
else()
    set_target_properties(treeweave_c_static PROPERTIES OUTPUT_NAME treeweave_c)
endif()
# Consumers of the static archive must see TREEWEAVE_EXPORT as no-op (no dllimport):
# the symbols are linked directly, not imported from a DLL.
target_compile_definitions(treeweave_c_static INTERFACE TREEWEAVE_STATIC)
set_property(
    GLOBAL
    APPEND
    PROPERTY TREEWEAVE_INSTALL_TARGETS treeweave_c_static
)

# The C examples and the pure-C conformance test are compiled with a C (not
# C++) compiler against the installed-shape header, proving the surface is
# C-clean and that nothing throws across the boundary. `add_test` below needs
# CTest enabled; this include runs before treeweave_tests.cmake (the C lib must
# exist before test_c links it), so enable testing here. enable_testing() is
# idempotent with the later include(CTest) in treeweave_tests.cmake.
if(TREEWEAVE_BUILD_EXAMPLES OR TREEWEAVE_BUILD_TESTS)
    enable_language(C)
    enable_testing()
    # C TUs call exp()/fabs()/etc.: C, unlike C++, needs libm linked explicitly.
    find_library(TREEWEAVE_LIBM m)

    # Build a C11 executable linking treeweave_c (+ libm). Deliberately no
    # treeweave_enable_warnings: that profile is C++-oriented (-Wold-style-cast,
    # etc.) and these are C translation units.
    function(_treeweave_add_c_program name source)
        add_executable(${name} ${source})
        set_target_properties(
            ${name}
            PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON
        )
        target_include_directories(
            ${name}
            PRIVATE ${PROJECT_SOURCE_DIR}/include
        )
        target_link_libraries(${name} PRIVATE treeweave_c)
        if(TREEWEAVE_LIBM)
            target_link_libraries(${name} PRIVATE ${TREEWEAVE_LIBM})
        endif()
    endfunction()
endif()

# C examples: each self-checks and returns EXIT_FAILURE on a bad result, so it
# doubles as a ctest.
if(TREEWEAVE_BUILD_EXAMPLES)
    foreach(
        _ex
        simple
        simple2d
        simple3d
        vector_output
        sorted
        with_options
        with_context
        float32
        lgamma_bench
    )
        _treeweave_add_c_program(treeweave_c_${_ex} examples/C/${_ex}.c)
        add_test(NAME c_example_${_ex} COMMAND treeweave_c_${_ex})
        # Register with the test-target list so the coverage target builds these
        # CTest executables before it runs CTest (see tests/CMakeLists.txt).
        set_property(
            GLOBAL
            APPEND
            PROPERTY TREEWEAVE_TEST_TARGETS treeweave_c_${_ex}
        )
    endforeach()
endif()

# Pure-C ABI conformance test (+ an optional valgrind run proving treeweave_free
# is leak-clean, registered only when valgrind is available).
if(TREEWEAVE_BUILD_TESTS)
    _treeweave_add_c_program(test_c_abi tests/test_c_abi.c)
    add_test(NAME test_c_abi COMMAND test_c_abi)
    # See above: let the coverage target build this C executable before CTest.
    set_property(GLOBAL APPEND PROPERTY TREEWEAVE_TEST_TARGETS test_c_abi)

    # Skip valgrind under sanitizers: valgrind cannot run an ASan/UBSan-
    # instrumented binary (the two shadow-memory schemes collide), so the run
    # spuriously fails even though the program is clean. The sanitizer build
    # already covers the same memory checks.
    find_program(TREEWEAVE_VALGRIND valgrind)
    if(TREEWEAVE_VALGRIND AND NOT TREEWEAVE_ENABLE_SANITIZERS)
        add_test(
            NAME test_c_abi_valgrind
            COMMAND
                ${TREEWEAVE_VALGRIND} --error-exitcode=1 --leak-check=full
                --errors-for-leak-kinds=definite $<TARGET_FILE:test_c_abi>
        )
    endif()
endif()
