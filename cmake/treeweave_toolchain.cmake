# treeweave_toolchain.cmake — language standard, build-type default, IPO, and
# global -march/-mtune + FP-contraction flags.

include_guard(GLOBAL)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_EXTENSIONS OFF)

# No C++20 modules → disable Ninja's per-TU scan (CMake 3.28+). Prevents
# clang-scan-deps/PCH mismatch on CI. (see devel/agents/build-notes.md)
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

include(GNUInstallDirs)
include(CheckIPOSupported)
check_ipo_supported(RESULT _ipo_supported OUTPUT _ipo_error)
# Skip IPO on gcc (ICE on heavy templates) and Emscripten (link-time cost,
# no eval-throughput gain). (see devel/agents/build-notes.md)
if(
    _ipo_supported
    AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU"
    AND NOT EMSCRIPTEN
)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
elseif(NOT _ipo_supported)
    message(WARNING "IPO is not supported: ${_ipo_error}")
endif()

# -march=native is required for FMA codegen; profiling showed ~50% of hot-path
# in libm fma() without it. (see devel/agents/build-notes.md)
set(TREEWEAVE_ARCH
    "native"
    CACHE STRING
    "CPU target for -march (e.g. native, x86-64-v3)"
)
# -mtune accepts CPU names, not arch names; x86-64-v*/armv* baselines fall back
# to 'generic'. (see devel/agents/build-notes.md)
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
    # Apple clang on AArch64 rejects `-march=apple-*` / `-mtune=` — it expects
    # `-mcpu=` for that microarch family. Use the right spelling per platform.
elseif(APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        # Homebrew GCC on Apple Silicon doesn't know the `apple-m1` CPU name
        # (only Apple clang does); `-mcpu=native` targets the runner's core.
        set(_treeweave_arch_flags -mcpu=native)
    else()
        set(_treeweave_arch_flags -mcpu=${TREEWEAVE_ARCH})
    endif()
else()
    set(_treeweave_arch_flags -march=${TREEWEAVE_ARCH} -mtune=${TREEWEAVE_TUNE})
endif()
# -ffp-contract=fast only; broader fast-math omitted (measured: no gain on FMA-
# bound path). Skipped on MSVC (clang-cl rejects the GCC flag under -Werror).
# (see devel/agents/build-notes.md)
if(MSVC)
    set(_treeweave_fp_flags "")
else()
    set(_treeweave_fp_flags -ffp-contract=fast)
endif()
add_compile_options(${_treeweave_arch_flags} ${_treeweave_fp_flags})
add_link_options(${_treeweave_arch_flags} ${_treeweave_fp_flags})
