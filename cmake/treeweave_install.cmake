# treeweave_install.cmake — install rules + a relocatable find_package(treeweave)
# package for the C ABI.
#
# The installed package ships the C ABI (`treeweave::treeweave_c` /
# `treeweave::treeweave_c_static`) — self-contained, links polyfit/POET privately.
# It also ships the header-only C++ API: the consolidated bundle
# (treeweave_bundle.cmake) installs treeweave's headers alongside the polyfit /
# POET / xsimd / mdspan trees it instantiates against, so `-I<prefix>/include`
# is enough. find_package still exposes only the C-ABI CMake targets; the C++
# headers are consumed by include path (see treeweave_c_api.cmake and the README).

include_guard(GLOBAL)
include(CMakePackageConfigHelpers)

# Relocatable RPATH so the installed shared lib finds its sibling libraries
# regardless of install prefix. $ORIGIN (Linux) / @loader_path (macOS) expand
# to the directory containing the binary at runtime.
if(NOT DEFINED CMAKE_INSTALL_RPATH)
    if(APPLE)
        set(CMAKE_INSTALL_RPATH
            "@loader_path;@loader_path/../${CMAKE_INSTALL_LIBDIR}"
        )
    else()
        set(CMAKE_INSTALL_RPATH "$ORIGIN;$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
    endif()
endif()
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

set(_treeweave_cmakedir "${CMAKE_INSTALL_LIBDIR}/cmake/treeweave")

get_property(_install_targets GLOBAL PROPERTY TREEWEAVE_INSTALL_TARGETS)
if(_install_targets)
    install(
        TARGETS ${_install_targets}
        EXPORT treeweaveTargets
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

    install(
        EXPORT treeweaveTargets
        NAMESPACE treeweave::
        DESTINATION ${_treeweave_cmakedir}
    )

    configure_package_config_file(
        "${PROJECT_SOURCE_DIR}/cmake/treeweaveConfig.cmake.in"
        "${PROJECT_BINARY_DIR}/treeweaveConfig.cmake"
        INSTALL_DESTINATION ${_treeweave_cmakedir}
    )

    write_basic_package_version_file(
        "${PROJECT_BINARY_DIR}/treeweaveConfigVersion.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
    )

    install(
        FILES
            "${PROJECT_BINARY_DIR}/treeweaveConfig.cmake"
            "${PROJECT_BINARY_DIR}/treeweaveConfigVersion.cmake"
        DESTINATION ${_treeweave_cmakedir}
    )
endif()

# Ship the consolidated header bundle (treeweave_bundle.cmake): treeweave's own
# headers plus the vendored polyfit/POET/xsimd/mdspan trees. This makes the
# header-only C++ API consumable from the install prefix with a single
# `-I<prefix>/include` (or none, for a standard prefix), the same as the build
# tree. Falls back to the source include/ if the bundle wasn't built (e.g. a
# non-top-level configure). The generated treeweave_version.h ships; its .in
# template does not.
if(
    TREEWEAVE_BUNDLE_INCLUDE_DIR
    AND IS_DIRECTORY "${TREEWEAVE_BUNDLE_INCLUDE_DIR}"
)
    install(
        DIRECTORY ${TREEWEAVE_BUNDLE_INCLUDE_DIR}/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        PATTERN "*.in" EXCLUDE
    )
else()
    install(
        DIRECTORY ${PROJECT_SOURCE_DIR}/include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        PATTERN "*.in" EXCLUDE
    )
endif()

# Relative DESTINATION so `cmake --install --prefix <dir>` is honored (an
# absolute ${CMAKE_INSTALL_PREFIX} here would bake in the configure-time
# prefix and ignore a later --prefix override).
install(
    FILES ${PROJECT_SOURCE_DIR}/LICENSE ${PROJECT_SOURCE_DIR}/NOTICE
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/licenses/Treeweave
)
