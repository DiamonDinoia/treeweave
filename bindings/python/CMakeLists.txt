# bindings/python — nanobind extension linking treeweave_c_static + a pytest
# CTest. Added by cmake/treeweave_bindings.cmake when TREEWEAVE_BUILD_PYTHON=ON.
# The pip/scikit-build wheel build reaches this same file: pyproject sets
# cmake.source-dir = "../../" and configures the root with TREEWEAVE_BUILD_PYTHON=ON.

# Development.SABIModule (optional) lets nanobind emit a CPython stable-ABI
# (abi3) extension on 3.12+, which is what the wheel build ships.
find_package(
    Python
    3.9
    QUIET
    COMPONENTS Interpreter Development.Module
    OPTIONAL_COMPONENTS Development.SABIModule
)
if(NOT Python_FOUND)
    message(
        STATUS
        "treeweave[python]: Python 3.9+ (Development.Module) not found — skipping."
    )
    return()
endif()

# Shared module definition (also used by the pip wheel build) — locates nanobind
# (interpreter or FetchContent) and defines the `_treeweave` target.
include("${CMAKE_CURRENT_SOURCE_DIR}/treeweave_python.cmake")
treeweave_add_python_module(treeweave::treeweave_c_static)

# Install the extension into the wheel's `treeweave/` package. Scoped to its own
# component so the scikit-build-core wheel build installs ONLY this
# (install.components = ["python_ext"] in pyproject.toml) and never drags in the
# C++ dev tree (headers / libtreeweave_c / CMake config).
install(TARGETS _treeweave LIBRARY DESTINATION treeweave COMPONENT python_ext)

# Assemble an importable package *in the build tree* — never write the .so (or
# copy .py) into the source tree. Layout: <build>/python-pkg/treeweave/.
set(_treeweave_pypkg "${CMAKE_BINARY_DIR}/python-pkg")
set_target_properties(
    _treeweave
    PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${_treeweave_pypkg}/treeweave"
)
# Stage the pure-Python sources next to the extension (re-copied on reconfigure
# if they change, via the configure_file input dependency).
file(GLOB _treeweave_py "${CMAKE_CURRENT_SOURCE_DIR}/treeweave/*.py")
foreach(_f IN LISTS _treeweave_py)
    get_filename_component(_n "${_f}" NAME)
    configure_file("${_f}" "${_treeweave_pypkg}/treeweave/${_n}" COPYONLY)
endforeach()

add_test(
    NAME python_treeweave
    COMMAND
        "${Python_EXECUTABLE}" -m pytest -q "${CMAKE_CURRENT_SOURCE_DIR}/tests"
)
set_tests_properties(
    python_treeweave
    PROPERTIES ENVIRONMENT "PYTHONPATH=${_treeweave_pypkg}"
)
message(
    STATUS
    "treeweave[python]: extension + pytest 'python_treeweave' registered "
    "(build-tree package at ${_treeweave_pypkg})."
)
