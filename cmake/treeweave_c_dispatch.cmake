# treeweave_c_dispatch.cmake: C-ABI TU generation and per-arch fan-out.
# Generated factory TUs — fit, tree build, pipeline glue; degree baked to 7 —
# compile once at the family baseline (6 combined TUs in multi-arch, 18
# one-pipeline TUs in single-arch; see the granularity comment below).
# Multi-arch fans out only the hot loops: kernels_arch.cpp compiles once per
# ISA rung, and the factory TUs reach the selected rung through KernelSet
# function pointers (TREEWEAVE_C_KERNELSET). No precompiled header: a PCH is
# built with one target's flags, and the rung TUs compile the same headers
# with different flags.

include_guard(GLOBAL)
include(CheckCXXCompilerFlag)

# COFF has no hidden visibility, so a multi-arch Windows build privatizes each
# rung's shared symbols after compiling it (rename_rung_symbols.sh). That needs
# llvm-nm and llvm-objcopy; resolve them at configure time so a missing tool
# fails here rather than half way through the build.
if(WIN32 AND TREEWEAVE_C_MULTIARCH)
    find_program(TREEWEAVE_LLVM_NM NAMES llvm-nm)
    find_program(TREEWEAVE_LLVM_OBJCOPY NAMES llvm-objcopy)
    if(NOT TREEWEAVE_LLVM_NM OR NOT TREEWEAVE_LLVM_OBJCOPY)
        message(
            FATAL_ERROR
            "treeweave[c-abi]: the multi-arch Windows build needs llvm-nm and "
            "llvm-objcopy to give each ISA rung private symbol names; COFF has no "
            "hidden visibility, so without them the linker keeps one arbitrary copy "
            "of the evaluation core. Install LLVM and put it on PATH, or build "
            "single-arch with -DTREEWEAVE_C_MULTIARCH=OFF."
        )
    endif()
endif()

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

# Factory-TU generation. The foreach lists below (dtype × input_dim ×
# output_dim) are the single source of truth for the shape set; when the set
# changes, also update
#   * the TREEWEAVE_SELECT_KERNELS list in src/capi/arch_dispatch.cpp,
#   * the TREEWEAVE_KERNELS_FOR list in src/capi/kernels_arch.cpp,
#   * make_eval_for's output_dim dispatch range in
#     include/treeweave/detail/c_binding.hpp,
#   * the documented shape set in include/treeweave.h.
# A new NON-vectorized function needs no change anywhere in this file: code
# outside the KernelSet hot loops compiles at baseline wherever it is called.
# A new VECTORIZED kernel: follow the step guide at the top of
# include/treeweave/detail/kernels.hpp.
#
# TU granularity follows the build mode. Multi-arch factory TUs are thin —
# the kernels live in the per-rung TUs behind KernelSet pointers — so header
# parsing dominates and the three output_dim pipelines share one TU per
# (dtype, dim). Single-arch TUs inline the kernels into the pipelines
# (InlineKernels), the pipeline bodies dominate, and each output_dim gets its
# own TU so the slowest TU holds one pipeline.

# One explicit-instantiation statement for make_eval_one<vt, in, out>.
function(_treeweave_inst_line var vt in out)
    string(
        CONCAT _line
        "template auto make_eval_one<${vt}, ${in}, ${out}>(\n"
        "    c_func_t<${vt}>, void *, const ${vt} *, const ${vt} *, double,\n"
        "    const treeweave::options &) -> IEval<${vt}> *;\n"
    )
    set(${var} "${_line}" PARENT_SCOPE)
endfunction()

set(_treeweave_variant_srcs "")
set(_treeweave_gen_dir "${CMAKE_CURRENT_BINARY_DIR}/capi_gen")
foreach(_dtype f64 f32)
    if(_dtype STREQUAL "f64")
        set(TREEWEAVE_VT double)
    else()
        set(TREEWEAVE_VT float)
    endif()
    foreach(_dim 1 2 3)
        if(_treeweave_multiarch_family)
            set(TREEWEAVE_VINSTS "")
            foreach(_out 1 2 3)
                _treeweave_inst_line(_line "${TREEWEAVE_VT}" ${_dim} ${_out})
                string(APPEND TREEWEAVE_VINSTS "${_line}")
            endforeach()
            set(_gen "${_treeweave_gen_dir}/dispatch_${_dtype}_dim${_dim}.cpp")
            configure_file(
                "${PROJECT_SOURCE_DIR}/src/capi/dispatch_variant.cpp.in"
                "${_gen}"
                @ONLY
            )
            list(APPEND _treeweave_variant_srcs "${_gen}")
        else()
            foreach(_out 1 2 3)
                _treeweave_inst_line(TREEWEAVE_VINSTS "${TREEWEAVE_VT}" ${_dim} ${_out})
                set(_gen "${_treeweave_gen_dir}/dispatch_${_dtype}_dim${_dim}_out${_out}.cpp")
                configure_file(
                    "${PROJECT_SOURCE_DIR}/src/capi/dispatch_variant.cpp.in"
                    "${_gen}"
                    @ONLY
                )
                list(APPEND _treeweave_variant_srcs "${_gen}")
            endforeach()
        endif()
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

    # Each rung compiles only kernels_arch.cpp with its own ISA flag. On ELF its
    # sole external symbols are the make_kernels_for<Arch, …> instantiations,
    # whose mangled names carry the rung's Arch: hidden visibility makes the
    # rest local, so the rungs cannot collide.
    #
    # COFF has no visibility control, so that does not hold on Windows. Both
    # cl.exe and clang-cl emit every inline function, template instantiation,
    # RTTI record and float-pool entry as an external COMDAT, and the linker
    # keeps one arbitrary copy of each. rename_rung_symbols.sh below gives each
    # rung its own copies. clang-cl needs it as much as cl.exe does: it defines
    # fewer of these than cl.exe, never none, and which ones diverge is not a
    # property to leave to the optimizer.
    #
    # On ELF, do not reach for objcopy --localize-symbol to get the same
    # effect. Making a rung's symbols local leaves their sections in the COMDAT
    # group, the linker still discards the group, and the local reference then
    # points into a discarded section. GNU ld rejects that ("defined in
    # discarded section"); mold accepts it, so the breakage shows up on only
    # some linkers. Renaming keeps the symbol external, so nothing is merged.
    #
    # The check_rung_symbols ctest below is the gate either way;
    # .github/scripts/check_isa_leak.sh re-checks the linked artifact.
    set(_treeweave_kernel_tgts "")
    set(_treeweave_rename_tgts "")
    foreach(_lvl IN LISTS _treeweave_arch_levels)
        string(REPLACE "-" "_" _tag "${_lvl}")
        _treeweave_add_c_object_lib(
            treeweave_c_kernels_${_tag}
            "${PROJECT_SOURCE_DIR}/src/capi/kernels_arch.cpp"
        )
        # Last ISA flag wins; pins this rung regardless of TREEWEAVE_ARCH.
        if(_treeweave_flags_${_lvl})
            target_compile_options(
                treeweave_c_kernels_${_tag}
                PRIVATE ${_treeweave_flags_${_lvl}}
            )
        endif()
        # COFF only: give this rung private names for everything it shares with
        # the other rungs, so no link can substitute another rung's code. The
        # kernel-table factory keeps its name -- the baseline TUs call it.
        #
        # A stamped custom command, not POST_BUILD: CMake rejects PRE_BUILD /
        # PRE_LINK / POST_BUILD on an OBJECT library. The rename rewrites the
        # objects in place, so an incremental build can recompile one and undo
        # it; the stamp then goes stale and the rename runs again. It is
        # idempotent, so the extra pass costs nothing.
        if(WIN32)
            set(_stamp "${CMAKE_CURRENT_BINARY_DIR}/rung-renamed-${_tag}.stamp")
            add_custom_command(
                OUTPUT "${_stamp}"
                COMMAND
                    "${CMAKE_COMMAND}" -E env "LLVM_NM=${TREEWEAVE_LLVM_NM}"
                    "LLVM_OBJCOPY=${TREEWEAVE_LLVM_OBJCOPY}" bash
                    "${PROJECT_SOURCE_DIR}/.github/scripts/rename_rung_symbols.sh"
                    "${_tag}" "make_kernels_for"
                    "$<TARGET_OBJECTS:treeweave_c_kernels_${_tag}>"
                COMMAND "${CMAKE_COMMAND}" -E touch "${_stamp}"
                DEPENDS
                    treeweave_c_kernels_${_tag}
                    "$<TARGET_OBJECTS:treeweave_c_kernels_${_tag}>"
                    "${PROJECT_SOURCE_DIR}/.github/scripts/rename_rung_symbols.sh"
                COMMENT "Privatizing shared symbols in the ${_lvl} rung (COFF)"
                COMMAND_EXPAND_LISTS
                VERBATIM
            )
            add_custom_target(treeweave_c_rename_${_tag} DEPENDS "${_stamp}")
            list(APPEND _treeweave_rename_tgts treeweave_c_rename_${_tag})
        endif()
        list(APPEND _treeweave_kernel_tgts treeweave_c_kernels_${_tag})
    endforeach()

    # Everything else — the generated factory TUs (fit, tree build, pipeline
    # glue), the runtime kernel selector and the C shim — compiles once at
    # the portable family baseline. TREEWEAVE_C_KERNELSET routes the factory
    # TUs' hot loops through the KernelSet function pointers instead of the
    # header-only InlineKernels.
    _treeweave_add_c_object_lib(treeweave_c_baseline
      ${_treeweave_variant_srcs}
      "${PROJECT_SOURCE_DIR}/src/capi/arch_dispatch.cpp"
      "${PROJECT_SOURCE_DIR}/src/capi/treeweave.cpp"
    )
    target_compile_definitions(treeweave_c_baseline PRIVATE TREEWEAVE_C_KERNELSET)
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

    # Cross-rung symbol gate: every external text symbol that more than one
    # object set defines must disassemble identically in all of them, or the
    # linker's arbitrary pick could carry a higher rung's ISA onto every host.
    # Runs on the object files, which keep full symbol tables on ELF and COFF
    # alike — a linked Windows DLL has none, so check_isa_leak.sh cannot see it.
    set(_treeweave_rung_sets
        "baseline=$<JOIN:$<TARGET_OBJECTS:treeweave_c_baseline>,$<SEMICOLON>>"
    )
    foreach(_lvl IN LISTS _treeweave_arch_levels)
        string(REPLACE "-" "_" _tag "${_lvl}")
        list(APPEND _treeweave_rung_sets
            "${_lvl}=$<JOIN:$<TARGET_OBJECTS:treeweave_c_kernels_${_tag}>,$<SEMICOLON>>"
        )
    endforeach()
    if(TREEWEAVE_BUILD_TESTS)
        add_test(
            NAME check_rung_symbols
            COMMAND
                bash "${PROJECT_SOURCE_DIR}/.github/scripts/check_rung_symbols.sh"
                ${_treeweave_rung_sets}
        )
    endif()
    # Release pipelines configure with TREEWEAVE_BUILD_TESTS=OFF, so the ctest
    # above never registers there. TREEWEAVE_VERIFY_RUNGS runs the same gate
    # as a build step instead; treeweave_c_api.cmake makes treeweave_c and
    # treeweave_c_static depend on it, so every consumer (wheel, MEX, node
    # addon) triggers it — including builds no workflow step can reach
    # (cibuildwheel's container).
    if(TREEWEAVE_VERIFY_RUNGS)
        add_custom_target(
            treeweave_verify_rungs
            COMMAND
                bash "${PROJECT_SOURCE_DIR}/.github/scripts/check_rung_symbols.sh"
                ${_treeweave_rung_sets}
            COMMENT "Checking cross-rung symbols (TREEWEAVE_VERIFY_RUNGS)"
            VERBATIM
        )
        # The rename targets too: the gate reads the objects, so it must not
        # run before they carry their private names.
        add_dependencies(
            treeweave_verify_rungs
            treeweave_c_baseline
            ${_treeweave_kernel_tgts}
            ${_treeweave_rename_tgts}
        )
    endif()
else()
    # Never a vacuous pass: a leg that asks for the gate but silently fell
    # back to single-arch must fail loudly, not skip the check.
    if(TREEWEAVE_VERIFY_RUNGS)
        message(
            FATAL_ERROR
            "TREEWEAVE_VERIFY_RUNGS=ON, but this configuration builds the C ABI "
            "single-arch (no rung fan-out to verify). Set TREEWEAVE_C_MULTIARCH=ON "
            "on a supported platform, or drop TREEWEAVE_VERIFY_RUNGS."
        )
    endif()
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
