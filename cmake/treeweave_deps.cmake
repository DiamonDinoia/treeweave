# treeweave_deps.cmake: fetch header-only dependencies and mark their include
# trees as system headers so consumer warnings (-Werror) don't fire on them.

include_guard(GLOBAL)
include(FetchContent)

# Stash analysis vars and arch/FP directory flags before fetching deps so they
# don't inherit the treeweave -Werror/-march settings. Restored at bottom of file.
set(_treeweave_saved_clang_tidy "${CMAKE_CXX_CLANG_TIDY}")
set(_treeweave_saved_cppcheck "${CMAKE_CXX_CPPCHECK}")
unset(CMAKE_CXX_CLANG_TIDY)
unset(CMAKE_CXX_CPPCHECK)

get_directory_property(_treeweave_saved_compile_options COMPILE_OPTIONS)
get_directory_property(_treeweave_saved_link_options LINK_OPTIONS)
set_directory_properties(PROPERTIES COMPILE_OPTIONS "" LINK_OPTIONS "")

# polyfit is the single pin site for xsimd and poet as well as for itself: it
# fetches both through CPM, and CPM declares them before anything here can, so a
# competing FetchContent_Declare for either is accepted silently and then
# ignored -- first declaration of a name wins. Move xsimd or poet by bumping
# polyfit, never by adding a declaration here.
FetchContent_Declare(
    polyfit
    GIT_REPOSITORY https://github.com/DiamonDinoia/polyfit.git
    GIT_TAG v0.0.5
    GIT_SHALLOW TRUE
    SYSTEM
)
set(POLYFIT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(POLYFIT_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(polyfit)

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
        GIT_SHALLOW TRUE
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
        GIT_SHALLOW TRUE
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
