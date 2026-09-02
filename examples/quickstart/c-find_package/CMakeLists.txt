# Consume the installed C library. The release tarball for every platform ships
# this CMake package, so extract it and point CMake at the prefix:
#   cmake -S . -B build -DCMAKE_PREFIX_PATH="$PREFIX"
# where PREFIX is the directory the tarball was extracted into.
# BEGIN DOCS_PROJECT
cmake_minimum_required(VERSION 3.25)
project(treeweave_quickstart C)

find_package(treeweave REQUIRED)

add_executable(app "${CMAKE_CURRENT_LIST_DIR}/../main.c")
# The example's own kernel calls pow(), so link libm explicitly: modern linkers
# run --as-needed and will not pull it in transitively.
find_library(MATH_LIBRARY m)
target_link_libraries(
    app
    PRIVATE treeweave::treeweave_c $<$<BOOL:${MATH_LIBRARY}>:${MATH_LIBRARY}>
)

# Windows has no RPATH, so the loader finds treeweave_c.dll only through PATH or
# next to the executable. Copy it next to the executable.
if(WIN32)
    add_custom_command(
        TARGET app
        POST_BUILD
        COMMAND
            ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:treeweave::treeweave_c> $<TARGET_FILE_DIR:app>
    )
endif()
