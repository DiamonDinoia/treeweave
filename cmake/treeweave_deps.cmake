# treeweave_deps.cmake — fetch header-only dependencies and mark their include
# trees as system headers so consumer warnings (-Werror) don't fire on them.

include_guard(GLOBAL)
include(FetchContent)

# Polyfit fetches xsimd internally via CPM (pinned to 14.0.0). We override that
# fetch to upstream xsimd 14.2.0 (the latest release) by cloning it locally and
# pointing CPM's per-package source override at the checkout before polyfit's
# CPMAddPackage(NAME xsimd ...) runs. The checkout lives under
# _deps_external/xsimd so it persists across CMake re-configures.
#
# This used to redirect to a DiamonDinoia/xsimd:feat/dynamic-masks fork for its
# masked-load primitives, but neither treeweave nor the pinned polyfit calls
# them (both handle SIMD tails with scalar remainders / fixed-size aligned
# loads), so stock upstream xsimd suffices — no fork to maintain.
set(_treeweave_xsimd_src "${PROJECT_BINARY_DIR}/_deps_external/xsimd")
if(NOT EXISTS "${_treeweave_xsimd_src}/.git")
    message(
        STATUS
        "treeweave: cloning xtensor-stack/xsimd:80c23624 (14.2.0) → ${_treeweave_xsimd_src}"
    )
    # Clone with tag first (--depth=1 --branch) then verify the resulting commit
    # matches the pinned SHA so a tag-move attack is caught at configure time.
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
    # Pinned commit SHA for xtensor-stack/xsimd tag 14.2.0
    set(_treeweave_xsimd_pinned_sha "80c23624ce008d937da7e845e528e82ce0cbf4e0")
    if(NOT _treeweave_xsimd_actual_sha STREQUAL _treeweave_xsimd_pinned_sha)
        message(
            FATAL_ERROR
            "treeweave: xsimd HEAD ${_treeweave_xsimd_actual_sha} != pinned SHA "
            "${_treeweave_xsimd_pinned_sha} — tag 14.2.0 may have moved"
        )
    endif()
endif()
# Tell CPM (used inside polyfit) to source xsimd from our local checkout.
set(CPM_xsimd_SOURCE "${_treeweave_xsimd_src}" CACHE PATH "" FORCE)

# Polyfit's `feat/parametric-block-size` branch extends the `estrin`-branch
# ScalarKernel selector with an opt-in `HybridK<K>` block-size override plus
# a consteval `optimal_block_size<NCOEFFS,SIMD_W,NREG,Policy>` picker and an
# `EvalPolicy { Latency, Throughput, Balanced }` enum. Treeweave threads
# `EvalPolicy` through `Function` and routes the scalar kernel and K choice
# per policy (see include/treeweave/detail/eval_policy.hpp); today's default
# `Balanced` keeps the scalar `Horner` mapping to avoid regressing the
# 1.3-1.6x scalar slowdown reported on Core Ultra 7 at xsimd lane_w=4 until
# the K-sweep measurement campaign retunes the formula.
# Don't run clang-tidy / cppcheck on fetched third-party sources: they are
# add_subdirectory'd here, so they would otherwise inherit the global
# CMAKE_CXX_CLANG_TIDY / CMAKE_CXX_CPPCHECK set by dev_helpers and spam the
# analysis build with warnings we cannot fix. Stash and clear them for the
# duration of the fetch, then restore for our own targets (built after this
# module). include() shares the caller's scope, so the restore at the bottom
# takes effect for the rest of the configure.
set(_treeweave_saved_clang_tidy "${CMAKE_CXX_CLANG_TIDY}")
set(_treeweave_saved_cppcheck "${CMAKE_CXX_CPPCHECK}")
unset(CMAKE_CXX_CLANG_TIDY)
unset(CMAKE_CXX_CPPCHECK)

# Same story for the global -march/-mtune/-ffp-contract flags added by
# treeweave_toolchain: they live on the directory's COMPILE_OPTIONS/LINK_OPTIONS,
# and FetchContent add_subdirectory's each dep, so a compiled dep (Catch2,
# nanobench, google_benchmark) would otherwise be built at our CPU baseline for
# no reason. Header-only deps are unaffected (their code compiles under the
# including TU's flags). Stash and clear the directory options for the fetch,
# then restore them for treeweave's own targets at the bottom. (Warnings are
# target-scoped via dev_helpers, so they never bled and need no handling here.)
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

# Treat transitive CPM-fetched dependencies (xsimd, mdspan, …) as system
# headers too so their warnings don't gate our -Werror build.
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
    # nanobench: header-only microbench harness with proper warmup,
    # MdAPE-based stability checks, and TSC-frequency calibration. Only the
    # benchmarks use it, so it is fetched only when they are built.
    # We fetch the source archive and expose the include directory only —
    # consumers get the impl by defining ANKERL_NANOBENCH_IMPLEMENT in
    # exactly one TU (done in treeweave_microbench.cpp).
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
    # CodSpeed C++ supports only Google Benchmark. We fetch it from
    # CodSpeedHQ/codspeed-cpp (SOURCE_SUBDIR google_benchmark): their compat
    # layer swaps the instrumented runtime in behind the standard
    # benchmark::benchmark target when CODSPEED_MODE is set (CI passes
    # -DCODSPEED_MODE=simulation; unset → plain Google Benchmark for local runs).
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

# Restore the analysis tools and the global arch/FP flags for treeweave's own
# targets (configured after this module is included).
set(CMAKE_CXX_CLANG_TIDY "${_treeweave_saved_clang_tidy}")
set(CMAKE_CXX_CPPCHECK "${_treeweave_saved_cppcheck}")
set_directory_properties(
    PROPERTIES
        COMPILE_OPTIONS "${_treeweave_saved_compile_options}"
        LINK_OPTIONS "${_treeweave_saved_link_options}"
)
