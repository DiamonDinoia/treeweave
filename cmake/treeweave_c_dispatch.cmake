# treeweave_c_dispatch.cmake — C-ABI translation-unit generation and per-arch
# fan-out for libtreeweave_c.
#
# Owns everything ISA-conditional about the C ABI: it generates the 6
# (dtype × input_dim) variant TUs from src/capi/dispatch_variant.cpp.in
# and compiles them as OBJECT libraries — once at TREEWEAVE_ARCH when multi-arch
# dispatch is OFF, or once per family `-march` level when ON (x86: a four-level
# ladder x86-64..x86-64-v4; non-Apple aarch64: a single neon64; riscv64: a
# single rvv). The resulting object-file generator expressions are published on
# the global property TREEWEAVE_C_OBJECT_EXPRS for treeweave_c_api.cmake to
# assemble into the shared and static libraries (so every object is compiled
# exactly once and shared between both libraries).
#
# Degree axis removed: degree is baked to chosen_degree<Arch,T,IN> = 7
# everywhere (arch_degree_table.hpp + campaign results). The previous
# 18 TUs (dtype × dim × degree) collapse to 6 (dtype × dim). Total variant
# TU count: 6 (OFF) or 24 (ON, 6 × 4 arch levels), down from 18/72.
#
# Bug #2 fix (COMDAT dedup): each per-arch variant carries the xsimd Arch type
# as a phantom template parameter on the callable wrappers (ArchTaggedScalar /
# ArchTaggedND in include/treeweave/detail/c_binding_detail.hpp), making the
# poly_eval::FuncEval / FuncEvalND instantiations distinct types per -march so
# the linker cannot COMDAT-fold the four per-arch eval kernels. No inline
# namespace is required; the type system solves the problem cleanly.
# Note: the fit-time single-point horner_nd_impl lambda is NOT arch-tagged and
# remains folded to the baseline scalar copy — this is an accepted fit-time-only
# perf tradeoff (off the batch-eval hot path, no crash, no eval-throughput
# impact).
#
# All ON/OFF and variant-count logic lives here, never in the C++ sources.

include_guard(GLOBAL)
include(CheckCXXCompilerFlag)

option(
    TREEWEAVE_C_MULTIARCH
    "Build the C ABI with runtime multi-architecture dispatch (x86 ladder / aarch64 neon64 / riscv rvv)"
    OFF
)

# ---------------------------------------------------------------------------
# Generate the 6 variant TUs (dtype × input_dim). Degree axis removed.
# ---------------------------------------------------------------------------
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

# ---------------------------------------------------------------------------
# Object-library helpers.
# ---------------------------------------------------------------------------
# GCC 15 spuriously warns (-Wtemplate-body) inside xsimd template bodies that
# name intrinsics absent at a variant's `-march`. Probe once and suppress.
check_cxx_compiler_flag("-Wno-template-body" _treeweave_has_wno_template_body)

# Compile-time usage requirements shared by every C-ABI object library: the
# treeweave headers, the (BUILD_INTERFACE-only) header-only polyfit/POET deps, and
# PIC so the objects can land in the shared library.
#
# Visibility: the objects are built hidden-by-default, so only the treeweave.h
# public surface (tagged TREEWEAVE_EXPORT) is exported from libtreeweave_c. TREEWEAVE_C_BUILD
# switches TREEWEAVE_EXPORT to its export form (dllexport / visibility("default"))
# while compiling the library's own TUs — this is what makes MSVC emit the
# treeweave_c import library, and keeps the .so/.dylib surface to the C ABI alone.
# The heavy per-shape machinery does not leak regardless — EvalImpl / EvalFactory
# live in an anonymous namespace (internal linkage), and the thin arch-keyed
# make_eval_for glue is external but hidden (resolved intra-DSO at link time).
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
    # Do not sanitize these generated dispatch TUs even when
    # TREEWEAVE_ENABLE_SANITIZERS is on. They are the heaviest TUs in the build,
    # so ASan/UBSan-instrumenting them spikes compile time and peak memory
    # (enough to OOM hosted CI runners). UBSan also makes libtreeweave_c.so emit
    # vptr-check symbols (__ubsan_handle_dynamic_type_cache_miss, ...) that the C
    # example/test executables cannot resolve when linking a C program. The
    # Valgrind C-ABI job already covers this compiled path for memory errors, and
    # the C++ test TUs still sanitize the eval kernels through the headers.
    if(TREEWEAVE_ENABLE_SANITIZERS)
        target_compile_options(${tgt} PRIVATE -fno-sanitize=address,undefined)
    endif()
endfunction()

# Add an OBJECT library, configure it, and publish its objects for the libs.
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

# ---------------------------------------------------------------------------
# Family detection + per-family variant fan-out plan.
# ---------------------------------------------------------------------------
# Each multi-arch family defines a set of `-march` levels (one per runtime
# variant TU) plus the per-level flags. arch_dispatch.cpp, compiled at the
# family *baseline*, selects the matching dispatch arch_list (dispatch_arch.hpp)
# and the variant TUs emit `make_eval_for<best_arch,…>` for each level. The
# fan-out below is generic over the level list, so adding a family is just a
# new branch that sets `_treeweave_arch_levels`, `_treeweave_<lvl>_flags`, and
# the baseline flags.
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

# The fan-out drives per-variant `-march`/`-mtune` flags, which are a GCC/Clang
# spelling; MSVC has no `-march` (cl.exe only *warns* on it, so all "variants"
# would silently collapse to the baseline) and no equivalent runtime-dispatch
# story here, so MSVC builds single-arch regardless of ISA. Apple aarch64 also
# stays single-arch: Apple clang rejects `-march=armv8-a`, has no SVE, and a
# one-entry neon64 ladder adds nothing over the existing `-mcpu=apple-m1` path.
# So the multi-arch fan-out is gated on a non-MSVC toolchain targeting x86,
# non-Apple aarch64, or riscv64; everything else (MSVC, Apple, unknown) falls
# through to the single-arch branch.
set(_treeweave_multiarch_family FALSE)
if(
    TREEWEAVE_C_MULTIARCH
    AND NOT MSVC
    AND (
        _treeweave_is_x86
        OR (_treeweave_is_aarch64 AND NOT APPLE)
        OR _treeweave_is_riscv64
    )
)
    set(_treeweave_multiarch_family TRUE)
endif()

if(_treeweave_multiarch_family)
    # -----------------------------------------------------------------------
    # ON: one per-`-march` variant object lib per level + a baseline dispatcher.
    # -----------------------------------------------------------------------
    if(_treeweave_is_x86)
        # The fixed four-type x86 dispatch ladder; each level's best_arch maps to
        # one dispatch_arch_list entry.
        set(_treeweave_arch_levels x86-64 x86-64-v2 x86-64-v3 x86-64-v4)
        set(_treeweave_flags_x86-64 -march=x86-64 -mtune=generic)
        set(_treeweave_flags_x86-64-v2 -march=x86-64-v2 -mtune=generic)
        set(_treeweave_flags_x86-64-v3 -march=x86-64-v3 -mtune=generic)
        set(_treeweave_flags_x86-64-v4 -march=x86-64-v4 -mtune=generic)
        set(_treeweave_baseline_flags -march=x86-64 -mtune=generic)
    elseif(_treeweave_is_aarch64)
        # NEON64 is mandatory on ARMv8-A, so a single mandatory variant is the
        # whole runtime ladder (SVE deliberately excluded — see dispatch_arch.hpp).
        set(_treeweave_arch_levels neon64)
        set(_treeweave_flags_neon64 -march=armv8-a -mtune=generic)
        set(_treeweave_baseline_flags -march=armv8-a -mtune=generic)
    else() # riscv64
        # Best-effort / untested: no RISC-V CI runner. Single fixed-128-bit RVV
        # variant matching dispatch_arch.hpp's rvv128.
        set(_treeweave_arch_levels rvv)
        set(_treeweave_flags_rvv -march=rv64gcv -mrvv-vector-bits=zvl)
        set(_treeweave_baseline_flags -march=rv64gcv -mrvv-vector-bits=zvl)
    endif()

    # Every variant level's flags must compile; error early (not at link) if the
    # toolchain or assembler can't target one.
    foreach(_lvl IN LISTS _treeweave_arch_levels)
        # check_cxx_compiler_flag wants one space-separated string; our per-level
        # flags are a CMake list (`-march=…;-mtune=…`), so join before probing.
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

    foreach(_lvl IN LISTS _treeweave_arch_levels)
        string(REPLACE "-" "_" _tag "${_lvl}")
        _treeweave_add_c_object_lib(treeweave_c_variants_${_tag} ${_treeweave_variant_srcs})
        # Appended after the global arch flags; on GCC/Clang the last `-march`
        # wins, pinning this variant to its level regardless of TREEWEAVE_ARCH.
        target_compile_options(
            treeweave_c_variants_${_tag}
            PRIVATE ${_treeweave_flags_${_lvl}}
        )
        target_precompile_headers(
            treeweave_c_variants_${_tag}
            PRIVATE <treeweave/detail/c_binding.hpp>
        )
    endforeach()

    # Baseline dispatcher + extern "C" shim: no SIMD of their own, built at the
    # portable family baseline so the library loads on any CPU of the family.
    _treeweave_add_c_object_lib(treeweave_c_baseline
      "${PROJECT_SOURCE_DIR}/src/capi/arch_dispatch.cpp"
      "${PROJECT_SOURCE_DIR}/src/capi/treeweave.cpp"
    )
    target_compile_options(
        treeweave_c_baseline
        PRIVATE ${_treeweave_baseline_flags}
    )
    message(
        STATUS
        "treeweave: C ABI multi-arch dispatch ON on ${CMAKE_SYSTEM_PROCESSOR} "
        "(levels: ${_treeweave_arch_levels})"
    )
else()
    # -----------------------------------------------------------------------
    # OFF (default), or ON on a target without a runtime fan-out (Apple aarch64,
    # MSVC, unknown): one variant set at TREEWEAVE_ARCH plus the single-arch entry
    # points and shim. ~zero extra build time vs the pre-dispatch layout (same
    # total instantiations as the old 6 TUs).
    # -----------------------------------------------------------------------
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
    target_precompile_headers(
        treeweave_c_objects
        PRIVATE <treeweave/detail/c_binding.hpp>
    )
endif()
