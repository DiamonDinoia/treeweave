# treeweave_deps.cmake — fetch header-only dependencies and mark their include
# trees as system headers so consumer warnings (-Werror) don't fire on them.

include_guard(GLOBAL)
include(FetchContent)

# Override polyfit's CPM-pinned xsimd (14.0.0) with 14.2.0 via a local clone;
# SHA verified at configure time to detect tag-move.
set(_treeweave_xsimd_src "${PROJECT_BINARY_DIR}/_deps_external/xsimd")
if(NOT EXISTS "${_treeweave_xsimd_src}/.git")
    message(
        STATUS
        "treeweave: cloning xtensor-stack/xsimd:80c23624 (14.2.0) → ${_treeweave_xsimd_src}"
    )
    execute_process(
        COMMAND
            git clone --depth=1 --branch 14.2.0
            https://github.com/xtensor-stack/xsimd.git "${_treeweave_xsimd_src}"
        RESULT_VARIABLE _treeweave_xsimd_clone_rc
    )
    if(NOT _treeweave_xsimd_clone_rc EQUAL 0)
        message(
            FATAL_ERROR
            "treeweave: failed to clone xsimd (rc=${_treeweave_xsimd_clone_rc})"
        )
    endif()
    execute_process(
        COMMAND git -C "${_treeweave_xsimd_src}" rev-parse HEAD
        OUTPUT_VARIABLE _treeweave_xsimd_actual_sha
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    set(_treeweave_xsimd_pinned_sha "80c23624ce008d937da7e845e528e82ce0cbf4e0")
    if(NOT _treeweave_xsimd_actual_sha STREQUAL _treeweave_xsimd_pinned_sha)
        message(
            FATAL_ERROR
            "treeweave: xsimd HEAD ${_treeweave_xsimd_actual_sha} != pinned SHA "
            "${_treeweave_xsimd_pinned_sha} — tag 14.2.0 may have moved"
        )
    endif()
endif()
set(CPM_xsimd_SOURCE "${_treeweave_xsimd_src}" CACHE PATH "" FORCE)

# Stash analysis vars and arch/FP directory flags before fetching deps so they
# don't inherit our -Werror/-march settings. Restored at bottom of file.
set(_treeweave_saved_clang_tidy "${CMAKE_CXX_CLANG_TIDY}")
set(_treeweave_saved_cppcheck "${CMAKE_CXX_CPPCHECK}")
unset(CMAKE_CXX_CLANG_TIDY)
unset(CMAKE_CXX_CPPCHECK)

get_directory_property(_treeweave_saved_compile_options COMPILE_OPTIONS)
get_directory_property(_treeweave_saved_link_options LINK_OPTIONS)
set_directory_properties(PROPERTIES COMPILE_OPTIONS "" LINK_OPTIONS "")

FetchContent_Declare(
    polyfit
    GIT_REPOSITORY https://github.com/DiamonDinoia/polyfit.git
    GIT_TAG 828582f2523678d206b2f76281088237427ff5b3
    SYSTEM
)
set(POLYFIT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(POLYFIT_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(polyfit)

FetchContent_Declare(
    poet
    GIT_REPOSITORY https://github.com/DiamonDinoia/POET.git
    GIT_TAG b55580fd1f17df500abb8afb16cd4983e66a27ac
    SYSTEM
)
set(POET_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(POET_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(POET_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(POET_BUILD_DOCS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(poet)

function(_treeweave_make_system target)
    if(NOT TARGET "${target}")
        return()
    endif()
    get_target_property(_aliased "${target}" ALIASED_TARGET)
    if(_aliased)
        set(target "${_aliased}")
    endif()
    get_target_property(_incl "${target}" INTERFACE_INCLUDE_DIRECTORIES)
    if(_incl)
        set_target_properties(
            "${target}"
            PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_incl}"
        )
    endif()
endfunction()
foreach(
    _t
    IN
    ITEMS polyfit xsimd mdspan std::mdspan poet
)
    _treeweave_make_system("${_t}")
endforeach()

if(TREEWEAVE_BUILD_BENCHMARKS)
    # Header-only; one TU defines ANKERL_NANOBENCH_IMPLEMENT (treeweave_microbench.cpp).
    FetchContent_Declare(
        nanobench
        GIT_REPOSITORY https://github.com/martinus/nanobench.git
        GIT_TAG v4.3.11
        SYSTEM
    )
    FetchContent_MakeAvailable(nanobench)
    _treeweave_make_system(nanobench)
endif()

if(TREEWEAVE_BUILD_CODSPEED)
    # CodSpeedHQ/codspeed-cpp wraps Google Benchmark; swaps in instrumented
    # runtime when CODSPEED_MODE is set.
    FetchContent_Declare(
        google_benchmark
        GIT_REPOSITORY https://github.com/CodSpeedHQ/codspeed-cpp.git
        GIT_TAG v2.4.0
        SOURCE_SUBDIR
        google_benchmark
        SYSTEM
    )
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(google_benchmark)
endif()

if(TREEWEAVE_BUILD_TESTS)
    FetchContent_Declare(
        catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.9.0
        SYSTEM
    )
    FetchContent_MakeAvailable(catch2)
endif()

# Restore analysis tools and arch/FP flags for treeweave's own targets.
set(CMAKE_CXX_CLANG_TIDY "${_treeweave_saved_clang_tidy}")
set(CMAKE_CXX_CPPCHECK "${_treeweave_saved_cppcheck}")
set_directory_properties(
    PROPERTIES
        COMPILE_OPTIONS "${_treeweave_saved_compile_options}"
        LINK_OPTIONS "${_treeweave_saved_link_options}"
)
