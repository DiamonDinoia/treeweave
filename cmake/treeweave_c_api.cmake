# treeweave_c_api.cmake: C ABI (libtreeweave_c) for the new fit/options surface.
# Builds a shared and a static library from src/capi/*.cpp (each per-(dtype,
# input_dim) dispatch TU compiles in parallel) plus the extern "C" shim, and
# wires the C smoke example. The global -march/-ffp-contract flags from the
# top CMakeLists already apply; nothing ISA-specific is added here.

include_guard(GLOBAL)

# In-tree only; NOT in the installed export set (deps are FetchContent-only).
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

# Object files compiled once in treeweave_c_dispatch, shared by both libraries.
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

# Emscripten has no dynamic linking; JS binding uses treeweave_c_static instead.
get_property(_tw_shared_ok GLOBAL PROPERTY TARGET_SUPPORTS_SHARED_LIBS)
if(_tw_shared_ok)
    add_library(treeweave_c SHARED ${_treeweave_c_objects})
    add_library(treeweave::treeweave_c ALIAS treeweave_c)
    _treeweave_configure_c_target(treeweave_c)
    set_target_properties(
        treeweave_c
        PROPERTIES VERSION ${PROJECT_VERSION} SOVERSION ${PROJECT_VERSION_MAJOR}
    )
    set_property(GLOBAL APPEND PROPERTY TREEWEAVE_INSTALL_TARGETS treeweave_c)
endif()

add_library(treeweave_c_static STATIC ${_treeweave_c_objects})
add_library(treeweave::treeweave_c_static ALIAS treeweave_c_static)
_treeweave_configure_c_target(treeweave_c_static)
# On Windows the import .lib collides with the static .lib; use distinct basename.
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

# TREEWEAVE_VERIFY_RUNGS (see treeweave_c_dispatch.cmake): run the cross-rung
# symbol gate before either library links, so any `cmake --build` of a
# consumer target — even `--target <binding>` — fails on an ISA leak.
# (`cmake --install` alone builds nothing and runs no gate.)
if(TARGET treeweave_verify_rungs)
    if(TARGET treeweave_c)
        add_dependencies(treeweave_c treeweave_verify_rungs)
    endif()
    add_dependencies(treeweave_c_static treeweave_verify_rungs)
endif()
# COFF rung rename (treeweave_c_dispatch.cmake): both libraries link the rung
# objects, so both must wait for the objects to carry their private names. The
# gate is optional, this is not.
if(_treeweave_rename_tgts)
    if(TARGET treeweave_c)
        add_dependencies(treeweave_c ${_treeweave_rename_tgts})
    endif()
    add_dependencies(treeweave_c_static ${_treeweave_rename_tgts})
endif()
set_property(
    GLOBAL
    APPEND
    PROPERTY TREEWEAVE_INSTALL_TARGETS treeweave_c_static
)

# enable_testing() here is idempotent with treeweave_tests.cmake; needed early
# because C examples register as ctests before that module runs.
if(TREEWEAVE_BUILD_EXAMPLES OR TREEWEAVE_BUILD_TESTS)
    enable_language(C)
    enable_testing()
    find_library(TREEWEAVE_LIBM m) # C needs libm explicitly, unlike C++

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
    )
        _treeweave_add_c_program(treeweave_c_${_ex} examples/C/${_ex}.c)
        add_test(NAME c_example_${_ex} COMMAND treeweave_c_${_ex})
        set_property(
            GLOBAL
            APPEND
            PROPERTY TREEWEAVE_TEST_TARGETS treeweave_c_${_ex}
        )
    endforeach()
endif()

if(TREEWEAVE_BUILD_TESTS)
    _treeweave_add_c_program(test_c_abi tests/test_c_abi.c)
    add_test(NAME test_c_abi COMMAND test_c_abi)
    set_property(GLOBAL APPEND PROPERTY TREEWEAVE_TEST_TARGETS test_c_abi)

    # Skip valgrind under sanitizers: valgrind cannot run an ASan/UBSan-
    # instrumented binary (the two shadow-memory schemes collide), so the run
    # spuriously fails even though the program is clean. The sanitizer build
    # already covers the same memory checks.
    #
    # valgrind also has no AVX-512 support: a binary whose baseline is AVX-512
    # (TREEWEAVE_ARCH=native on a recent Xeon, or x86-64-v4) dies with SIGILL in
    # the first wide kernel, which reads as a leak-check failure and makes the
    # documented `ctest --test-dir build/dev-release` fail on the developer's own
    # machine. Register the check only where valgrind can execute the binary;
    # `cmake --preset valgrind` pins x86-64-v3 for exactly this reason.
    find_program(TREEWEAVE_VALGRIND valgrind)
    include(CheckCXXSourceCompiles)
    string(JOIN " " _tw_probe_flags ${_treeweave_arch_flags})
    # The probe result is cached, so key the cache entry on the flags it was
    # measured with: a reconfigure with a different TREEWEAVE_ARCH then probes
    # again instead of reusing the old answer.
    string(
        MAKE_C_IDENTIFIER
        "TREEWEAVE_VALGRIND_RUNNABLE_${_tw_probe_flags}"
        _tw_probe_var
    )
    set(_tw_saved_required_flags "${CMAKE_REQUIRED_FLAGS}")
    set(CMAKE_REQUIRED_FLAGS "${_tw_probe_flags}")
    check_cxx_source_compiles(
        "#ifdef __AVX512F__
#error AVX-512 baseline: valgrind cannot run this binary
#endif
int main() {}"
        ${_tw_probe_var}
    )
    set(CMAKE_REQUIRED_FLAGS "${_tw_saved_required_flags}")
    if(
        TREEWEAVE_VALGRIND
        AND ${_tw_probe_var}
        AND NOT TREEWEAVE_ENABLE_SANITIZERS
    )
        add_test(
            NAME test_c_abi_valgrind
            COMMAND
                ${TREEWEAVE_VALGRIND} --error-exitcode=1 --leak-check=full
                --errors-for-leak-kinds=definite $<TARGET_FILE:test_c_abi>
        )
    endif()
endif()
