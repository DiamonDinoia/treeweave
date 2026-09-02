# Consume an already-installed treeweave: either the release tarball, or the
# result of `cmake --install`. Point CMake at the prefix when configuring:
#   cmake -S . -B build -DCMAKE_PREFIX_PATH="$PREFIX"
# where PREFIX is the install prefix, ./_prefix in the docs.
# BEGIN DOCS_PROJECT
cmake_minimum_required(VERSION 3.20)
project(treeweave_quickstart LANGUAGES CXX)

find_package(treeweave REQUIRED)

add_executable(app "${CMAKE_CURRENT_LIST_DIR}/../main.cpp")
target_link_libraries(app PRIVATE treeweave::treeweave)
