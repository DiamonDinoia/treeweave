# treeweave_toolchain.cmake — language standard, build-type default, IPO, and the
# global -march/-mtune + FP-contraction flags. These are internal build
# mechanics; the top-level CMakeLists keeps only project() + user-facing
# option()s and the module wiring.

include_guard(GLOBAL)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_EXTENSIONS OFF)

# treeweave uses no C++20 modules, so disable the Ninja generator's per-TU
# module dependency scan (CMake 3.28+; harmlessly ignored by older CMake).
# Besides being wasted work, the scan runs clang-scan-deps, which on some CI
# toolchains is a different LLVM build than the clang++ that produced our
# precompiled header — clang-scan-deps then rejects the PCH with "built from a
# different branch" and the C-ABI dispatch objects fail to scan. No modules =>
# no scan => no clash.
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
# gcc 14's LTO chokes (ICE / "invalid tree code") on the heavy template
# instantiations in the timing examples. Skip IPO on gcc until that's
# fixed upstream — clang's LTO handles the same code without trouble.
if(_ipo_supported AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
elseif(NOT _ipo_supported)
    message(WARNING "IPO is not supported: ${_ipo_error}")
endif()

# Performance-critical flags for ND polyfit codegen.
#
# `-march=native` is critical: without it the compiler targets the x86-64
# baseline ISA, which has no FMA — every Horner step then becomes a libm
# fma() call instead of a single `vfmadd231sd` / `vfmadd231pd`. Profiling
# the multi-leaf hot path showed ~50% of runtime in libm's __fma_fma3
# before this was enabled.
#
# Override at configure time with `-DTREEWEAVE_ARCH=...` to target a portable
# baseline (e.g. `x86-64-v3` for AVX2+FMA without per-machine tuning).
set(TREEWEAVE_ARCH
    "native"
    CACHE STRING
    "CPU target for -march (e.g. native, x86-64-v3)"
)
# `-mtune` accepts CPU names, not architecture names. gcc rejects
# `-mtune=x86-64-v{2,3,4}` and `-mtune=armv8-a` (those are `-march=` levels /
# arch names) and deprecates the bare `-mtune=x86-64` (fatal under -Werror), so
# we derive a tune value: pass through real CPU names (native, neoverse-n1, …)
# but fall back to `generic` for the portable `x86-64` / `x86-64-v*` and
# `armv*` architecture baselines.
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
# Apple clang on AArch64 rejects `-march=apple-*` / `-mtune=` — it expects
# `-mcpu=` for that microarch family. Use the right spelling per platform.
if(APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
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
# Global compile flags. We deliberately stay conservative here:
#   - Arch flags are needed for FMA codegen at all.
#   - `-ffp-contract=fast` permits FMA contraction (a single `fma` is
#     *more* accurate than a separate mul+add, so it is monotonic for
#     fit accuracy).
# We do *not* enable `-funroll-loops` or the broader fast-math relaxations
# (`-fassociative-math`, `-freciprocal-math`, `-fno-signed-zeros`, …): the eval
# hot path already unrolls the per-axis loops via `poet::static_for`, and the
# Horner kernel is FMA-bound, so there is nothing left for them to extract. This
# was measured — the finufft-style curated fast-math subset (NaN/Inf preserved,
# no `-ffinite-math-only`) produced zero CodSpeed instruction-count change and a
# wash-to-slight-regression in wall-clock, so it is intentionally omitted.
# `-ffp-contract=fast` is GCC/Clang-driver syntax: cl.exe silently ignores it,
# but clang-cl (also an MSVC-style frontend) rejects it under
# `-Werror,-Wunknown-argument`. Skip it on MSVC — the default Windows FP model
# already matches the long-standing cl.exe build, which never saw the flag.
if(MSVC)
    set(_treeweave_fp_flags "")
else()
    set(_treeweave_fp_flags -ffp-contract=fast)
endif()
add_compile_options(${_treeweave_arch_flags} ${_treeweave_fp_flags})
add_link_options(${_treeweave_arch_flags} ${_treeweave_fp_flags})
