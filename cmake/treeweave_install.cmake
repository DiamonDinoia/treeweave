# treeweave_install.cmake — install rules + relocatable find_package package.
# Ships C ABI targets + consolidated header bundle. C++ consumed by include path.
# (see devel/agents/build-notes.md — "Install overview")

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

# Consolidated bundle: treeweave + dep headers in one tree (-I<prefix>/include).
# Falls back to source include/ if bundle wasn't built (non-top-level configure).
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
