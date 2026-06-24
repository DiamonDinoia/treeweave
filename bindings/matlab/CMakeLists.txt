# bindings/matlab — MATLAB/Octave (MEX) binding, built entirely from CMake.
# Added by cmake/treeweave_bindings.cmake when TREEWEAVE_BUILD_MATLAB=ON.
#
# treeweave.mw is the mwrap source of truth. mwrap's output is platform-
# independent, so it is generated once and committed to generated/
# (treeweave_mex_gen.cpp + tw_*.m). By default the build just compiles those
# checked-in files (TREEWEAVE_MATLAB_USE_PREGENERATED=ON): no CPM, no mwrap, no
# bison/flex/m4. Set it OFF to fetch+run mwrap (zgimbutas/mwrap, via CPM) and
# refresh generated/ with the treeweave_mw_regen target after a treeweave.mw edit.
#
# mwrap's helper generates and (optionally) compiles, but it does not link an
# external static library into the MEX, so we own the final compile/link step:
#   * MATLAB  → matlab_add_mex() + link treeweave_c_static (license required);
#   * Octave  → mkoctfile --mex … treeweave_c_static (license-free, the CI path).
# A missing toolchain degrades to a STATUS message and a skipped test.
#
# Backend selection (MEX M2 note): there is no separate CMake option for Octave.
# Both CMake presets (bindings-matlab and bindings-octave) set the same flag
# TREEWEAVE_BUILD_MATLAB=ON. Backend selection is driven purely by toolchain
# discovery below: if mkoctfile is on PATH the Octave MEX is built; if MATLAB
# is found the MATLAB MEX is built; if both are present both are built. The
# bindings-octave preset is the recommended entry point when no MATLAB license
# is available (CI path) — ensure mkoctfile is on PATH before configuring.

# ---------------------------------------------------------------------------
# Toolchain discovery — at least one MEX backend must be present.
# ---------------------------------------------------------------------------
find_program(TREEWEAVE_MKOCTFILE_EXECUTABLE mkoctfile)
find_program(TREEWEAVE_OCTAVE_EXECUTABLE NAMES octave-cli octave)
find_package(Matlab QUIET COMPONENTS MAIN_PROGRAM MEX_COMPILER MX_LIBRARY)

if(NOT TREEWEAVE_MKOCTFILE_EXECUTABLE AND NOT Matlab_FOUND)
    message(
        STATUS
        "treeweave[matlab]: neither mkoctfile (Octave) nor MATLAB found — skipping."
    )
    return()
endif()

# ---------------------------------------------------------------------------
# mwrap output: checked-in by default, regenerated only on request.
# ---------------------------------------------------------------------------
# mwrap's output (treeweave_mex_gen.cpp + tw_*.m) is platform-independent — a
# textual codegen from treeweave.mw with no arch/OS flags — so we generate it
# once and commit it to generated/. The default build just compiles those
# checked-in files: no CPM, no mwrap, no bison/flex/m4, and (crucially) no MSVC
# link of mwrap's flex/bison globals, which never resolved (the Windows blocker).
# Set TREEWEAVE_MATLAB_USE_PREGENERATED=OFF to fetch+run mwrap and refresh
# generated/ via the treeweave_mw_regen target after editing treeweave.mw.
option(
    TREEWEAVE_MATLAB_USE_PREGENERATED
    "Compile the checked-in mwrap gateway (generated/) instead of running mwrap"
    ON
)
set(_treeweave_inc "${PROJECT_SOURCE_DIR}/include")

if(TREEWEAVE_MATLAB_USE_PREGENERATED)
    # Stage the checked-in tw_*.m into a build dir alongside where the MEX will
    # land, so a single addpath imports the binding (same layout as the regen
    # path), and the source tree never gets a compiled MEX written into it.
    set(
        _treeweave_mex_src
        "${CMAKE_CURRENT_SOURCE_DIR}/generated/treeweave_mex_gen.cpp"
    )
    set(_treeweave_gen_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
    file(
        COPY "${CMAKE_CURRENT_SOURCE_DIR}/generated/"
        DESTINATION "${_treeweave_gen_dir}"
        FILES_MATCHING
        PATTERN "tw_*.m"
    )
    set(_treeweave_mw_dep "") # no generator target
else()
    # Regen path: fetch+build the mwrap generator via CPM (MWRAP_COMPILE_MEX OFF
    # — mwrap would otherwise find_package(Matlab/Octave) REQUIRED itself; we
    # drive the compile/link below).
    _treeweave_ensure_cpm()
    cpmaddpackage(
        NAME mwrap
        GITHUB_REPOSITORY zgimbutas/mwrap
        GIT_TAG a53713c34b09b74221ff7220c77ee1e12583804d
        OPTIONS "MWRAP_BUILD_EXAMPLES OFF" "MWRAP_COMPILE_MEX OFF"
    )
    if(NOT TARGET mwrap)
        message(
            WARNING
            "treeweave[matlab]: mwrap generator unavailable after CPM — skipping."
        )
        return()
    endif()

    # mwrap's flex-generated scanner #includes <unistd.h>, which MSVC lacks.
    # Define YY_NO_UNISTD_H so the generated lexer skips it.
    if(MSVC)
        target_compile_definitions(mwrap PRIVATE YY_NO_UNISTD_H)
    endif()

    include("${mwrap_SOURCE_DIR}/cmake/MwrapAddMex.cmake")

    mwrap_add_mex(
        treeweave_mw
        MEX_NAME treeweave_mex
        CC_FILENAME treeweave_mex_gen.cpp
        MW_FILES "${CMAKE_CURRENT_SOURCE_DIR}/treeweave.mw"
        MWRAP_FLAGS -mb
        OUTPUT_VAR _treeweave_mex_src
    )
    get_target_property(_treeweave_gen_dir treeweave_mw MWRAP_OUTPUT_DIRECTORY)
    set(_treeweave_mw_dep treeweave_mw)

    # Refresh the checked-in copy from mwrap's fresh output:
    #   cmake --build <dir> --target treeweave_mw_regen
    # then `git diff bindings/matlab/generated/` shows the .mw edit's effect.
    set(_treeweave_regen_script "${CMAKE_CURRENT_BINARY_DIR}/regen_copy.cmake")
    file(
        WRITE
        "${_treeweave_regen_script}"
        "file(COPY \"${_treeweave_gen_dir}/\" DESTINATION \"${CMAKE_CURRENT_SOURCE_DIR}/generated\" FILES_MATCHING PATTERN \"tw_*.m\")\nfile(COPY \"${_treeweave_mex_src}\" DESTINATION \"${CMAKE_CURRENT_SOURCE_DIR}/generated\")\n"
    )
    add_custom_target(
        treeweave_mw_regen
        COMMAND ${CMAKE_COMMAND} -P "${_treeweave_regen_script}"
        DEPENDS treeweave_mw
        COMMENT
            "treeweave[matlab]: refreshing checked-in mwrap output in generated/"
        VERBATIM
    )
endif()

# ---------------------------------------------------------------------------
# MATLAB backend — matlab_add_mex() + our include/define/link. -DR2008OO selects
# mwrap's classdef/mwptr object-pointer path. Static libstdc++/libgcc so the
# .mexa64 doesn't depend on a newer GLIBCXX than MATLAB's bundled libstdc++.
# ---------------------------------------------------------------------------
if(Matlab_FOUND)
    matlab_add_mex(
        NAME treeweave_mex_matlab
        SRC "${_treeweave_mex_src}"
        OUTPUT_NAME treeweave_mex
    )
    if(_treeweave_mw_dep)
        add_dependencies(treeweave_mex_matlab ${_treeweave_mw_dep})
    endif()
    target_include_directories(treeweave_mex_matlab PRIVATE "${_treeweave_inc}")
    target_compile_definitions(treeweave_mex_matlab PRIVATE R2008OO)
    # matlab_add_mex() already linked the MATLAB libs with the *plain*
    # target_link_libraries signature, so this one must be plain too (CMake
    # forbids mixing the keyword and plain signatures on one target).
    target_link_libraries(treeweave_mex_matlab treeweave_c_static)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_link_options(
            treeweave_mex_matlab
            PRIVATE -static-libstdc++ -static-libgcc
        )
    endif()
    # Drop the .mexa64 next to the generated tw_*.m stubs so a single addpath
    # makes the whole binding importable.
    set_target_properties(
        treeweave_mex_matlab
        PROPERTIES
            LIBRARY_OUTPUT_DIRECTORY "${_treeweave_gen_dir}"
            RUNTIME_OUTPUT_DIRECTORY "${_treeweave_gen_dir}"
    )
endif()

# ---------------------------------------------------------------------------
# Octave backend — mkoctfile compiles + links against Octave's own mex API. The
# octave_compat/ shim satisfies mwrap's `#include <matrix.h>` (MATLAB-only;
# Octave's mex.h is self-contained and ships no matrix.h). The static C archive
# is passed as a positional input so mkoctfile links it.
# ---------------------------------------------------------------------------
if(TREEWEAVE_MKOCTFILE_EXECUTABLE)
    set(_treeweave_oct_mex "${_treeweave_gen_dir}/treeweave_mex.mex")
    add_custom_command(
        OUTPUT "${_treeweave_oct_mex}"
        COMMAND
            "${TREEWEAVE_MKOCTFILE_EXECUTABLE}" --mex -DR2008OO
            "-I${_treeweave_inc}" "-I${CMAKE_CURRENT_SOURCE_DIR}/octave_compat"
            "${_treeweave_mex_src}" "$<TARGET_FILE:treeweave_c_static>" -o
            "${_treeweave_oct_mex}"
        DEPENDS ${_treeweave_mw_dep} treeweave_c_static "${_treeweave_mex_src}"
        WORKING_DIRECTORY "${_treeweave_gen_dir}"
        COMMENT "treeweave[matlab]: building Octave MEX treeweave_mex.mex"
        VERBATIM
    )
    add_custom_target(treeweave_mex_octave ALL DEPENDS "${_treeweave_oct_mex}")
endif()

# ---------------------------------------------------------------------------
# Test — run test_treeweave.m headless. Prefer Octave (license-free, the CI
# path); fall back to MATLAB. Both need the source dir (treeweave.m class +
# test_treeweave.m) and the generated dir (tw_*.m + the compiled MEX) on path.
# ---------------------------------------------------------------------------
if(TREEWEAVE_MKOCTFILE_EXECUTABLE AND TREEWEAVE_OCTAVE_EXECUTABLE)
    add_test(
        NAME matlab_treeweave
        COMMAND
            "${TREEWEAVE_OCTAVE_EXECUTABLE}" --no-gui --quiet --eval
            "addpath('${CMAKE_CURRENT_SOURCE_DIR}'); addpath('${_treeweave_gen_dir}'); test_treeweave"
    )
    message(
        STATUS
        "treeweave[matlab]: test 'matlab_treeweave' registered (Octave: ${TREEWEAVE_OCTAVE_EXECUTABLE})."
    )
elseif(Matlab_FOUND AND Matlab_MAIN_PROGRAM)
    add_test(
        NAME matlab_treeweave
        COMMAND
            "${Matlab_MAIN_PROGRAM}" -batch
            "addpath('${CMAKE_CURRENT_SOURCE_DIR}'); addpath('${_treeweave_gen_dir}'); test_treeweave"
    )
    message(
        STATUS
        "treeweave[matlab]: test 'matlab_treeweave' registered (MATLAB: ${Matlab_MAIN_PROGRAM})."
    )
else()
    message(
        STATUS
        "treeweave[matlab]: MEX built, but no interpreter found to run the test."
    )
endif()
