# treeweave_bundle.cmake: merge all C++ API headers into one include tree so
# non-CMake users get `g++ -I<build>/include`. Top-level only.

include_guard(GLOBAL)

set(TREEWEAVE_BUNDLE_INCLUDE_DIR "${PROJECT_BINARY_DIR}/include")

if(NOT PROJECT_IS_TOP_LEVEL)
    return()
endif()

file(REMOVE_RECURSE "${TREEWEAVE_BUNDLE_INCLUDE_DIR}")
foreach(
    _t
    IN
    ITEMS treeweave_headers polyfit poet xsimd mdspan std::mdspan
)
    if(NOT TARGET ${_t})
        continue()
    endif()
    get_target_property(_dirs ${_t} INTERFACE_INCLUDE_DIRECTORIES)
    foreach(_d IN LISTS _dirs)
        if(_d MATCHES "\\$<BUILD_INTERFACE:(.+)>")
            set(_d "${CMAKE_MATCH_1}") # strip genexpr wrapper
        elseif(_d MATCHES "\\$<")
            continue() # INSTALL_INTERFACE etc., no build-tree path
        endif()
        if(_d AND IS_DIRECTORY "${_d}")
            execute_process(
                COMMAND
                    ${CMAKE_COMMAND} -E copy_directory "${_d}"
                    "${TREEWEAVE_BUNDLE_INCLUDE_DIR}"
            )
        endif()
    endforeach()
endforeach()
