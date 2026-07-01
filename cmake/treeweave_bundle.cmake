# treeweave_bundle.cmake — consolidate the header-only C++ API and its deps
# (polyfit, POET, xsimd, mdspan) into ONE include tree so non-CMake users get an
# xsimd-style single-flag build: `g++ ... -I<build>/include`. The deps live
# wherever FetchContent / the CPM cache put them and land in disjoint subdirs
# (polyfit/, poet/, xsimd/, experimental/), so copy_directory merges them
# without collision. This same tree is what install() ships (see
# treeweave_install.cmake), so the installed prefix works the same way.
#
# Built at configure time; re-run cmake to refresh after a dep bump. Only useful
# top-level (a consumer via find_package / add_subdirectory resolves deps itself).

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
            continue() # INSTALL_INTERFACE etc. — no build-tree path
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
