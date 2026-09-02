# examples/CMakeLists.txt: C++ examples. Added via add_subdirectory() from the
# top-level CMakeLists, guarded by TREEWEAVE_BUILD_EXAMPLES there.

# Keep example executables in the build root (where they lived before examples/
# became its own subdirectory) rather than under build/examples/, CI and docs
# invoke them as build/<example>.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")

function(_treeweave_add_cpp_example name source)
    add_executable(${name} "${CMAKE_CURRENT_SOURCE_DIR}/${source}")
    # treeweave::treeweave carries include/ (which holds the generated
    # <treeweave_version.h>), polyfit/poet, and cxx_std_20.
    target_link_libraries(${name} PRIVATE treeweave::treeweave)
    treeweave_enable_warnings(${name})
    add_test(NAME cpp_example_${name} COMMAND ${name})
    set_property(GLOBAL APPEND PROPERTY TREEWEAVE_TEST_TARGETS ${name})
endfunction()

# Every c++/*.cpp is an example and a test, so adding a file is enough.
file(GLOB _treeweave_cpp_examples CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/c++/*.cpp")
foreach(_src IN LISTS _treeweave_cpp_examples)
    cmake_path(GET _src STEM _name)
    _treeweave_add_cpp_example(${_name} "c++/${_name}.cpp")
endforeach()

# The examples only gate anything if a wrong answer reaches ctest as a failure.
# selfcheck_control.cpp takes that branch on purpose, so WILL_FAIL is what keeps
# the whole example suite honest. It sits outside c++/ to stay out of the glob.
add_executable(selfcheck_control "${CMAKE_CURRENT_SOURCE_DIR}/selfcheck_control.cpp")
target_link_libraries(selfcheck_control PRIVATE treeweave::treeweave)
treeweave_enable_warnings(selfcheck_control)
add_test(NAME selfcheck_control COMMAND selfcheck_control)
set_tests_properties(selfcheck_control PROPERTIES WILL_FAIL TRUE)
set_property(GLOBAL APPEND PROPERTY TREEWEAVE_TEST_TARGETS selfcheck_control)

# Doc figures generated from the examples themselves, so a picture in the docs
# is literally what the shipped program prints. Sphinx reads docs/ from the
# source tree, so the output lands there (git-ignored) and the sphinx target
# waits for it.
if(TREEWEAVE_GENERATE_DOCS AND TARGET sphinx)
    set(_tw_docs_gen "${PROJECT_SOURCE_DIR}/docs/_generated")
    set(_tw_docs_figures "")
    # Each entry is an example target whose stdout the docs literalinclude as
    # <target>.txt. Add a target here to publish its output.
    foreach(_fig IN ITEMS panels1d singularity)
        add_custom_command(
            OUTPUT ${_tw_docs_gen}/${_fig}.txt
            COMMAND ${CMAKE_COMMAND} -E make_directory ${_tw_docs_gen}
            COMMAND
                ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:${_fig}>
                -DOUT=${_tw_docs_gen}/${_fig}.txt -P
                ${CMAKE_CURRENT_SOURCE_DIR}/capture_output.cmake
            DEPENDS ${_fig}
            COMMENT "Capturing ${_fig} output for the docs"
            VERBATIM
        )
        list(APPEND _tw_docs_figures ${_tw_docs_gen}/${_fig}.txt)
    endforeach()
    add_custom_target(docs_example_output DEPENDS ${_tw_docs_figures})
    add_dependencies(sphinx docs_example_output)
endif()
