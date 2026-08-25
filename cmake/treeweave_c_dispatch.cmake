# treeweave_c_dispatch.cmake: C-ABI TU generation and per-arch fan-out.
# No precompiled header: a PCH is built with one target's flags, and this file
# compiles the same sources once per ISA level with different flags.
# 6 (dtype×dim) variant TUs; degree baked to 7. Multi-arch: 6×4=24 TUs on x86.
# COMDAT dedup via phantom Arch template param on wrappers.

include_guard(GLOBAL)
include(CheckCXXCompilerFlag)

# cl.exe optimizes these dispatch TUs at a rate that makes an optimized C ABI
# impractical: measured on windows-2022, one dispatch_f64_dim3 TU at /O2 spends
# 1 s in the front end and does not leave the back end in 40 min, and the
# multi-arch fan-out is 24 such TUs. clang-cl compiles the same target in 161 s.
# Both toolsets target the same MSVC ABI, so the MEX and the DLL stay
# interchangeable.
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC" AND NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(
        WARNING
        "treeweave[c-abi]: cl.exe needs hours to optimize the dispatch TUs. "
        "Configure with -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl "
        "(Visual Studio generator: -T ClangCL) for a build of the same ABI in minutes."
    )
endif()

set(_treeweave_variant_srcs "")
set(_treeweave_gen_dir "${CMAKE_CURRENT_BINARY_DIR}/capi_gen")
foreach(_dtype f64 f32)
    if(_dtype STREQUAL "f64")
        set(TREEWEAVE_VT double)
    else()
        set(TREEWEAVE_VT float)
    endif()
    foreach(_dim 1 2 3)
        set(TREEWEAVE_VIN ${_dim})
        set(_gen "${_treeweave_gen_dir}/dispatch_${_dtype}_dim${_dim}.cpp")
        configure_file(
            "${PROJECT_SOURCE_DIR}/src/capi/dispatch_variant.cpp.in"
            "${_gen}"
            @ONLY
        )
        list(APPEND _treeweave_variant_srcs "${_gen}")
    endforeach()
endforeach()

# GCC 15 spuriously warns -Wtemplate-body on absent intrinsics; suppress once.
check_cxx_compiler_flag("-Wno-template-body" _treeweave_has_wno_template_body)

# TREEWEAVE_C_BUILD switches TREEWEAVE_EXPORT to dllexport/visibility("default");
# objects are hidden-by-default so only the C ABI surface escapes the DSO.
function(_treeweave_configure_c_object tgt)
    target_include_directories(
        ${tgt}
        PRIVATE $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    )
    target_link_libraries(
        ${tgt}
        PRIVATE
            $<BUILD_INTERFACE:polyfit::polyfit>
            $<BUILD_INTERFACE:poet::poet>
    )
    target_compile_definitions(${tgt} PRIVATE TREEWEAVE_C_BUILD)
    # std::getenv (arch_dispatch.cpp's TREEWEAVE_FORCE_ARCH hook) is standard C++,
    # but MSVC flags it C4996 ("unsafe"); under /WX that fails the build. Silence
    # the CRT deprecation for treeweave TUs rather than dropping warnings-as-errors.
    if(MSVC)
        target_compile_definitions(${tgt} PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    set_target_properties(
        ${tgt}
        PROPERTIES
            POSITION_INDEPENDENT_CODE ON
            C_VISIBILITY_PRESET hidden
            CXX_VISIBILITY_PRESET hidden
            VISIBILITY_INLINES_HIDDEN ON
    )
    treeweave_enable_warnings(${tgt})
    if(_treeweave_has_wno_template_body)
        target_compile_options(${tgt} PRIVATE -Wno-template-body)
    endif()
    # These generated dispatch TUs force-inline (TREEWEAVE_FLATTEN) deep eval
    # templates; under a Debug `-g` build GCC's var-tracking blows its size
    # limit and silently retries the whole pass without it, ~doubling compile
    # time on the dim3 variants. Skip that wasted first pass (debug-info quality
    # only, no codegen impact). GCC-only; Clang has no such flag.
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(
            ${tgt}
            PRIVATE $<$<CONFIG:Debug>:-fno-var-tracking-assignments>
        )
    endif()
    # Exclude dispatch TUs from sanitizers: compile/memory cost + UBSan vptr
    # symbols break C linkers. Valgrind covers this path instead.
    if(TREEWEAVE_ENABLE_SANITIZERS)
        target_compile_options(${tgt} PRIVATE -fno-sanitize=address,undefined)
    endif()
    # test_c drives the C ABI from concurrent threads, so these shared objects
    # need atomic coverage counters even though the rest of the suite is serial.
    treeweave_coverage_atomic_counters(${tgt})
endfunction()

function(_treeweave_add_c_object_lib name)
    add_library(${name} OBJECT ${ARGN})
    _treeweave_configure_c_object(${name})
    set_property(
        GLOBAL
        APPEND
        PROPERTY TREEWEAVE_C_OBJECT_EXPRS "$<TARGET_OBJECTS:${name}>"
    )
endfunction()

set_property(GLOBAL PROPERTY TREEWEAVE_C_OBJECT_EXPRS "")

set(_treeweave_is_x86 FALSE)
set(_treeweave_is_aarch64 FALSE)
set(_treeweave_is_riscv64 FALSE)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64|i[3-6]86)$")
    set(_treeweave_is_x86 TRUE)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
    set(_treeweave_is_aarch64 TRUE)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(riscv64|riscv)$")
    set(_treeweave_is_riscv64 TRUE)
endif()

# Multi-arch fan-out: x86 (any compiler) or non-MSVC aarch64/riscv64. Apple clang
# accepts the aarch64 -march levels below (verified on clang 21); the per-level
# probe further down is the guard for any toolchain that doesn't. MSVC non-x86
# has no /arch: ladder.
set(_treeweave_multiarch_family FALSE)
if(
    TREEWEAVE_C_MULTIARCH
    AND (
        _treeweave_is_x86
        OR (_treeweave_is_aarch64 AND NOT MSVC)
        OR (_treeweave_is_riscv64 AND NOT MSVC)
    )
)
    set(_treeweave_multiarch_family TRUE)
endif()

if(_treeweave_multiarch_family)
    if(_treeweave_is_x86 AND MSVC)
        # MSVC x86: /arch: ladder (no -march); no SSE4.2 rung (SSE2→AVX→AVX2→AVX512).
        # best_arch levels must match dispatch_arch.hpp's MSVC x86 list.
        set(_treeweave_arch_levels sse2 avx avx2 avx512)
        set(_treeweave_flags_sse2 "") # SSE2 is the MSVC x64 baseline
        set(_treeweave_flags_avx /arch:AVX)
        set(_treeweave_flags_avx2 /arch:AVX2)
        set(_treeweave_flags_avx512 /arch:AVX512)
        set(_treeweave_baseline_flags "") # dispatcher: SSE2 baseline, no flag
    elseif(_treeweave_is_x86)
        set(_treeweave_arch_levels x86-64 x86-64-v2 x86-64-v3 x86-64-v4)
        set(_treeweave_flags_x86-64 -march=x86-64 -mtune=generic)
        set(_treeweave_flags_x86-64-v2 -march=x86-64-v2 -mtune=generic)
        set(_treeweave_flags_x86-64-v3 -march=x86-64-v3 -mtune=generic)
        set(_treeweave_flags_x86-64-v4 -march=x86-64-v4 -mtune=generic)
        set(_treeweave_baseline_flags -march=x86-64 -mtune=generic)
    elseif(_treeweave_is_aarch64)
        # Single rung: NEON64 is mandatory on ARMv8-A; SVE excluded.
        set(_treeweave_arch_levels neon64)
        set(_treeweave_flags_neon64 -march=armv8-a -mtune=generic)
        set(_treeweave_baseline_flags -march=armv8-a -mtune=generic)
    else() # riscv64
        # Best-effort / untested: no RISC-V CI runner.
        set(_treeweave_arch_levels rvv)
        set(_treeweave_flags_rvv -march=rv64gcv -mrvv-vector-bits=zvl)
        set(_treeweave_baseline_flags -march=rv64gcv -mrvv-vector-bits=zvl)
    endif()

    # Probe each level's flags early so a missing assembler surfaces at configure.
    # Empty-flag levels (MSVC SSE2 baseline) are skipped.
    foreach(_lvl IN LISTS _treeweave_arch_levels)
        if(NOT _treeweave_flags_${_lvl})
            continue()
        endif()
        # check_cxx_compiler_flag needs a space-separated string; join the list.
        string(REPLACE ";" " " _treeweave_probe "${_treeweave_flags_${_lvl}}")
        check_cxx_compiler_flag("${_treeweave_probe}" _treeweave_flagok_${_lvl})
        if(NOT _treeweave_flagok_${_lvl})
            message(
                FATAL_ERROR
                "TREEWEAVE_C_MULTIARCH=ON needs '${_treeweave_flags_${_lvl}}', which this "
                "toolchain rejects. Upgrade the compiler/assembler, or configure with "
                "-DTREEWEAVE_C_MULTIARCH=OFF for a single-arch C ABI."
            )
        endif()
    endforeach()

    # Each rung compiles the same headers with its own ISA flag. Symbols the
    # fan-out names carry the rung in their mangled name and never collide.
    # Symbols instantiated from headers at global scope do collide: every rung
    # emits the same weak name with different code and the linker keeps one
    # arbitrary copy, so a higher rung's body can end up on a lower rung's path.
    # Localizing every symbol but the factory gives each rung its own copy
    # instead; see treeweave_localize.cmake. An OBJECT library takes no
    # POST_BUILD command, so the localization runs as a separate target the
    # consumers depend on; see treeweave_c_api.cmake.
    # ELF only: COFF has no per-symbol binding to rewrite this way and
    # llvm-objcopy's Mach-O support is partial, so those two rely on the check
    # in .github/scripts/check_isa_leak.sh.
    set(TREEWEAVE_C_LOCALIZE_OBJECTS "")
    set(TREEWEAVE_C_LOCALIZE_TARGETS "")
    set(_treeweave_localize FALSE)
    if(NOT MSVC AND NOT APPLE)
        find_program(TREEWEAVE_OBJCOPY NAMES llvm-objcopy objcopy)
        if(TREEWEAVE_OBJCOPY)
            set(_treeweave_localize TRUE)
        else()
            message(
                STATUS
                "treeweave: no objcopy found; the per-arch fan-out keeps its "
                "shared weak symbols"
            )
        endif()
    endif()

    foreach(_lvl IN LISTS _treeweave_arch_levels)
        string(REPLACE "-" "_" _tag "${_lvl}")
        _treeweave_add_c_object_lib(treeweave_c_variants_${_tag} ${_treeweave_variant_srcs})
        if(_treeweave_localize)
            list(
                APPEND
                TREEWEAVE_C_LOCALIZE_OBJECTS
                "$<TARGET_OBJECTS:treeweave_c_variants_${_tag}>"
            )
            list(APPEND TREEWEAVE_C_LOCALIZE_TARGETS treeweave_c_variants_${_tag})
        endif()

        # Last ISA flag wins; pins this variant regardless of TREEWEAVE_ARCH.
        if(_treeweave_flags_${_lvl})
            target_compile_options(
                treeweave_c_variants_${_tag}
                PRIVATE ${_treeweave_flags_${_lvl}}
            )
        endif()
    endforeach()

    # Baseline dispatcher + shim: no SIMD, built at the portable family baseline.
    _treeweave_add_c_object_lib(treeweave_c_baseline
      "${PROJECT_SOURCE_DIR}/src/capi/arch_dispatch.cpp"
      "${PROJECT_SOURCE_DIR}/src/capi/treeweave.cpp"
    )
    if(_treeweave_baseline_flags)
        target_compile_options(
            treeweave_c_baseline
            PRIVATE ${_treeweave_baseline_flags}
        )
    endif()
    message(
        STATUS
        "treeweave: C ABI multi-arch dispatch ON on ${CMAKE_SYSTEM_PROCESSOR} "
        "(levels: ${_treeweave_arch_levels})"
    )
else()
    if(TREEWEAVE_C_MULTIARCH)
        message(
            STATUS
            "treeweave: TREEWEAVE_C_MULTIARCH has no runtime fan-out on "
            "${CMAKE_SYSTEM_PROCESSOR}; building the C ABI single-arch at "
            "TREEWEAVE_ARCH=${TREEWEAVE_ARCH}"
        )
    endif()
    _treeweave_add_c_object_lib(treeweave_c_objects
      ${_treeweave_variant_srcs}
      "${PROJECT_SOURCE_DIR}/src/capi/arch_single.cpp"
      "${PROJECT_SOURCE_DIR}/src/capi/treeweave.cpp"
    )
endif()
