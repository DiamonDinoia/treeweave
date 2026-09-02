# benchmarks/CMakeLists.txt: performance benchmarks (nanobench drivers, hand-
# rolled timing harness, CodSpeed, and the C zeta reference).
# Gated by TREEWEAVE_BUILD_BENCHMARKS / TREEWEAVE_BUILD_CODSPEED; never built
# in a normal test or example build.

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")

if(TREEWEAVE_BUILD_BENCHMARKS)
    # Extra libs (e.g. nanobench) are passed as ARGN so one function covers both
    # nanobench-linked benchmarks and plain-exe timing harnesses.
    function(_tw_add_bench name source)
        add_executable(${name} "${CMAKE_CURRENT_SOURCE_DIR}/${source}")
        target_link_libraries(${name} PRIVATE treeweave::treeweave ${ARGN})
        treeweave_enable_warnings(${name})
    endfunction()

    _tw_add_bench(treeweave_timing_cpp treeweave_timing.cpp)
    _tw_add_bench(treeweave_microbench treeweave_microbench.cpp nanobench)
    _tw_add_bench(treeweave_bench_pack_scatter treeweave_bench_pack_scatter.cpp nanobench)
    _tw_add_bench(treeweave_bench_sorted treeweave_bench_sorted.cpp nanobench)
    _tw_add_bench(treeweave_bench_binsort treeweave_bench_binsort.cpp nanobench)
    _tw_add_bench(treeweave_ci_bench treeweave_ci_bench.cpp nanobench)
    # perf_driver is a hand-rolled timing harness; it does not use nanobench.
    _tw_add_bench(treeweave_perf_driver treeweave_perf_driver.cpp)

    add_executable(zeta_bench "${CMAKE_CURRENT_SOURCE_DIR}/zeta_bench.cpp")
    target_link_libraries(zeta_bench PRIVATE treeweave::treeweave)
    treeweave_enable_warnings(zeta_bench)

    if(TREEWEAVE_BUILD_C_API)
        enable_language(C)
        enable_testing()
        find_library(_tw_bench_libm m)
        add_executable(
            treeweave_c_zeta_bench
            "${CMAKE_CURRENT_SOURCE_DIR}/zeta_bench.c"
        )
        set_target_properties(
            treeweave_c_zeta_bench
            PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON
        )
        target_include_directories(
            treeweave_c_zeta_bench
            PRIVATE ${PROJECT_SOURCE_DIR}/include
        )
        target_link_libraries(treeweave_c_zeta_bench PRIVATE treeweave_c)
        if(_tw_bench_libm)
            target_link_libraries(
                treeweave_c_zeta_bench
                PRIVATE ${_tw_bench_libm}
            )
        endif()
        add_test(NAME bench_c_zeta COMMAND treeweave_c_zeta_bench)
    endif()

    # The C++ arm of the cross-language interpolator comparison. Boost.Math's
    # interpolators are header-only, so this needs headers and no link step.
    # Skipped, not failed, when Boost is absent: the other arms still build.
    find_package(Boost 1.75 QUIET)
    if(Boost_FOUND)
        _tw_add_bench(compare_interpolators_cpp compare_interpolators.cpp Boost::headers)
        if(TREEWEAVE_BUILD_TESTS)
            add_test(
                NAME compare_interpolators_cpp_self_test
                COMMAND compare_interpolators_cpp --self-test
            )
        endif()
    else()
        message(
            STATUS
            "Boost not found: skipping compare_interpolators_cpp"
        )
    endif()
endif()

# No treeweave_enable_warnings: Google Benchmark / codspeed-cpp are third-party.
if(TREEWEAVE_BUILD_CODSPEED)
    add_executable(
        treeweave_codspeed_bench
        "${CMAKE_CURRENT_SOURCE_DIR}/treeweave_codspeed_bench.cpp"
    )
    target_include_directories(
        treeweave_codspeed_bench
        PRIVATE ${PROJECT_SOURCE_DIR}/include
    )
    target_link_libraries(
        treeweave_codspeed_bench
        PRIVATE polyfit::polyfit poet::poet benchmark::benchmark
    )
endif()
