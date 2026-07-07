# bindings/matlab — MATLAB/Octave MEX binding. Pregenerated gateway by default
# (TREEWEAVE_MATLAB_USE_PREGENERATED=ON); set OFF to fetch+run mwrap via CPM.
# Backend auto-detected: mkoctfile (license-free) or MATLAB; both if present.

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

option(
    TREEWEAVE_MATLAB_USE_PREGENERATED
    "Compile the checked-in mwrap gateway (generated/) instead of running mwrap"
    ON
)
# Static-link libstdc++/libgcc into the GNU MATLAB MEX (portable shipped binary);
# OFF links them dynamically (smaller, but needs a compatible system libstdc++).
option(
    TREEWEAVE_MATLAB_STATIC_LIBSTDCXX
    "Static-link the C++ runtime into the GNU MATLAB MEX (portable .mexa64)"
    ON
)
set(_treeweave_inc "${PROJECT_SOURCE_DIR}/include")

if(TREEWEAVE_MATLAB_USE_PREGENERATED)
    # Stage tw_*.m into build dir so a single addpath imports the binding;
    # source tree never gets a compiled MEX written into it.
    set(_treeweave_mex_src
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
    # Regen path: MWRAP_COMPILE_MEX OFF so we drive compile/link ourselves.
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
        WRITE "${_treeweave_regen_script}"
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
    # matlab_add_mex() uses the plain TLL signature; must match to avoid CMake error.
    target_link_libraries(treeweave_mex_matlab treeweave_c_static)
    # -static-libstdc++/-static-libgcc: .mexa64 must not need a newer GLIBCXX than
    # MATLAB's bundled libstdc++. Standard portable-MEX fix.
    if(
        CMAKE_CXX_COMPILER_ID STREQUAL "GNU"
        AND TREEWEAVE_MATLAB_STATIC_LIBSTDCXX
    )
        target_link_options(
            treeweave_mex_matlab
            PRIVATE -static-libstdc++ -static-libgcc
        )
    endif()
    set_target_properties(
        treeweave_mex_matlab
        PROPERTIES
            LIBRARY_OUTPUT_DIRECTORY "${_treeweave_gen_dir}"
            RUNTIME_OUTPUT_DIRECTORY "${_treeweave_gen_dir}"
    )
endif()

# octave_compat/ shim satisfies mwrap's #include <matrix.h> (MATLAB-only header).
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

if(TREEWEAVE_MKOCTFILE_EXECUTABLE AND TREEWEAVE_OCTAVE_EXECUTABLE)
    add_test(
        NAME matlab_treeweave
        COMMAND
            "${TREEWEAVE_OCTAVE_EXECUTABLE}" --no-gui --quiet --eval
            "addpath('${CMAKE_CURRENT_SOURCE_DIR}'); addpath('${_treeweave_gen_dir}'); test_treeweave"
    )
    foreach(_example IN ITEMS example_1d example_2d example_vector)
        add_test(
            NAME "octave_example_${_example}"
            COMMAND
                "${TREEWEAVE_OCTAVE_EXECUTABLE}" --no-gui --quiet --eval
                "addpath('${CMAKE_CURRENT_SOURCE_DIR}'); addpath('${_treeweave_gen_dir}'); run('${CMAKE_CURRENT_SOURCE_DIR}/examples/${_example}.m')"
        )
    endforeach()
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
