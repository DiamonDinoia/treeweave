# treeweave_python.cmake: single source of truth for the `_treeweave` nanobind
# extension, included by cmake/treeweave_bindings.cmake. The wheel build and the
# in-tree CTest glue share that one include path: scikit-build-core configures
# the root CMakeLists (pyproject sets cmake.source-dir = "../../") with
# TREEWEAVE_BUILD_PYTHON=ON, so both routes define `_treeweave` identically.
#
# Callers must have already run find_package(Python ... Development.Module).

include_guard(GLOBAL)

# Make nanobind's `nanobind_add_module` available. Prefer the nanobind shipped
# with the active interpreter (the pip/scikit-build path, and what a developer
# who `pip install nanobind`-ed gets); otherwise FetchContent it so an in-tree
# `cmake .. -DTREEWEAVE_BUILD_PYTHON=ON` works without any pip install.
function(treeweave_setup_nanobind)
    if(COMMAND nanobind_add_module)
        return()
    endif()

    execute_process(
        COMMAND "${Python_EXECUTABLE}" -m nanobind --cmake_dir
        OUTPUT_STRIP_TRAILING_WHITESPACE
        OUTPUT_VARIABLE _nb_dir
        RESULT_VARIABLE _nb_rc
        ERROR_QUIET
    )

    if(_nb_rc EQUAL 0 AND IS_DIRECTORY "${_nb_dir}")
        message(
            STATUS
            "treeweave[python]: using nanobind from ${Python_EXECUTABLE}"
        )
        find_package(
            nanobind
            CONFIG
            REQUIRED
            PATHS "${_nb_dir}"
            NO_DEFAULT_PATH
        )
    else()
        message(
            STATUS
            "treeweave[python]: nanobind not importable in "
            "${Python_EXECUTABLE}, fetching it via FetchContent."
        )
        include(FetchContent)
        FetchContent_Declare(
            nanobind
            GIT_REPOSITORY https://github.com/wjakob/nanobind.git
            GIT_TAG v2.9.2
            SYSTEM
        )
        FetchContent_MakeAvailable(nanobind)
    endif()
endfunction()

# Define the `_treeweave` extension: nanobind_add_module + include dir + link the
# given static C-ABI target (e.g. treeweave::treeweave_c_static). Placement (output
# dir / install) is left to the caller, which differs between the two paths.
function(treeweave_add_python_module static_target)
    treeweave_setup_nanobind()
    nanobind_add_module(_treeweave STABLE_ABI
      "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/src/_treeweave.cpp"
    )
    target_include_directories(
        _treeweave
        PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../include"
    )
    target_link_libraries(_treeweave PRIVATE ${static_target})
endfunction()
