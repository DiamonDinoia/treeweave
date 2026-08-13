# treeweave_toolchain.cmake — language standard, build-type default, IPO, and
# global -march/-mtune + FP-contraction flags.

include_guard(GLOBAL)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_EXTENSIONS OFF)

# No C++20 modules → disable Ninja's per-TU scan (CMake 3.28+). Prevents
# clang-scan-deps/PCH mismatch on CI.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "No build type specified. Defaulting to Release.")
    set(CMAKE_BUILD_TYPE
        Release
        CACHE STRING
        "Valid options: Debug, RelWithDebInfo, Release"
        FORCE
    )
endif()

# CMake's default Debug is -O0, which leaves this header-only, deeply-inlined
# code un-inlined and makes the compute-heavy tests crawl -- the slowest single
# test runs longer than the whole suite does at Release. -Og keeps -g and the
# asserts without that cost, so it is the default for every Debug build,
# coverage included: the line data stays good enough to be useful and the
# instrumented run is no longer the slowest job in CI. A caller that spelled
# CMAKE_CXX_FLAGS_DEBUG out itself still wins. MSVC has no -Og.
#
# CMake pre-seeds these cache entries from *_INIT ("-g" for GNU/Clang), so
# "did the caller set it?" is a comparison against that seed, not a NOT test.
# The seed is stored with the surrounding whitespace CMake strips on the way
# into the cache, so both sides are stripped before comparing.
if(NOT MSVC)
    foreach(_lang CXX C)
        string(STRIP "${CMAKE_${_lang}_FLAGS_DEBUG}" _tw_dbg_cur)
        string(STRIP "${CMAKE_${_lang}_FLAGS_DEBUG_INIT}" _tw_dbg_init)
        if(_tw_dbg_cur STREQUAL _tw_dbg_init)
            set(CMAKE_${_lang}_FLAGS_DEBUG
                "-Og -g"
                CACHE STRING
                "Flags used by the ${_lang} compiler during DEBUG builds."
                FORCE
            )
        endif()
    endforeach()
endif()

include(GNUInstallDirs)
include(CheckIPOSupported)
check_ipo_supported(RESULT _ipo_supported OUTPUT _ipo_error)
# Skip IPO on gcc (ICE on heavy templates), MSVC (/LTCG spends over an hour
# per executable on these templates) and Emscripten (link-time cost, no
# eval-throughput gain). clang-cl keeps IPO: ThinLTO links in seconds.
if(
    _ipo_supported
    AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU"
    AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC"
    AND NOT EMSCRIPTEN
)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
elseif(NOT _ipo_supported)
    message(WARNING "IPO is not supported: ${_ipo_error}")
endif()

# -march=native is required for FMA codegen; profiling showed ~50% of hot-path
# in libm fma() without it.
set(TREEWEAVE_ARCH
    "native"
    CACHE STRING
    "CPU target for -march (e.g. native, x86-64-v3)"
)
# -mtune accepts CPU names, not arch names; x86-64-v*/armv* baselines fall back
# to 'generic'.
if(
    TREEWEAVE_ARCH MATCHES "^x86-64(-v[0-9]+)?$"
    OR TREEWEAVE_ARCH MATCHES "^armv"
)
    set(_treeweave_tune "generic")
else()
    set(_treeweave_tune "${TREEWEAVE_ARCH}")
endif()
set(TREEWEAVE_TUNE
    "${_treeweave_tune}"
    CACHE STRING
    "CPU target for -mtune (defaults to TREEWEAVE_ARCH, or generic for x86-64-v*)"
)
# Emscripten's clang rejects -march=/-mtune= entirely; the only knob that
# matters for WASM is SIMD128 (xsimd then auto-selects its `xsimd::wasm`
# backend). No -mtune. -ffp-contract=fast (below) is fine — emcc is clang.
if(EMSCRIPTEN)
    set(_treeweave_arch_flags -msimd128)
elseif(MSVC)
    # MSVC and clang-cl use /arch, and have no exact spelling for x86-64-v2 or
    # native. Keep those at the compiler baseline; map the useful x86 tiers.
    if(TREEWEAVE_ARCH STREQUAL "x86-64-v3")
        set(_treeweave_arch_flags /arch:AVX2)
    elseif(TREEWEAVE_ARCH STREQUAL "x86-64-v4")
        set(_treeweave_arch_flags /arch:AVX512)
    elseif(TREEWEAVE_ARCH STREQUAL "avx")
        set(_treeweave_arch_flags /arch:AVX)
    else()
        set(_treeweave_arch_flags "")
    endif()
elseif(APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
    # Apple clang splits the two spellings: -march= takes the portable armv*
    # arch levels (and rejects apple-*/native), while -mcpu= takes the CPU
    # names (native, apple-m1) and rejects the armv* levels. -mcpu is also the
    # only one that sets Apple-pipeline scheduling *and* full feature detection:
    # on M2, -mcpu=native = apple-m1 + BF16/MATMUL_INT8, whereas -march=native
    # drops FP16/crypto and tunes generic. So use -mcpu for CPU names, -march
    # only for the armv* baselines used by the multiarch dispatch variants.
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        # Homebrew GCC doesn't know the apple-* CPU names; native targets the core.
        set(_treeweave_arch_flags -mcpu=native)
    elseif(TREEWEAVE_ARCH MATCHES "^armv")
        set(_treeweave_arch_flags -march=${TREEWEAVE_ARCH})
    else()
        set(_treeweave_arch_flags -mcpu=${TREEWEAVE_ARCH})
    endif()
else()
    set(_treeweave_arch_flags -march=${TREEWEAVE_ARCH} -mtune=${TREEWEAVE_TUNE})
endif()
# -ffp-contract=fast only; broader fast-math omitted (measured: no gain on FMA-
# bound path). Skipped on MSVC (clang-cl rejects the GCC flag under -Werror).
if(MSVC)
    set(_treeweave_fp_flags "")
else()
    set(_treeweave_fp_flags -ffp-contract=fast)
endif()
add_compile_options(${_treeweave_arch_flags} ${_treeweave_fp_flags})
# GCC/Clang want -march on the link line too (LTO codegen). MSVC/clang-cl
# /arch:* is compile-only — lld-link reads it as an input file and errors;
# ThinLTO picks the ISA up from the per-function attributes instead.
if(NOT MSVC)
    add_link_options(${_treeweave_arch_flags} ${_treeweave_fp_flags})
endif()
