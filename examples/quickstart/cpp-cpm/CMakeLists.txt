# Consume treeweave with CPM.cmake. CPM caches checkouts across projects, which
# FetchContent does not; the cost is bootstrapping CPM itself, below.
cmake_minimum_required(VERSION 3.20)
project(treeweave_quickstart LANGUAGES CXX)

# The four-line CPM bootstrap, from https://github.com/cpm-cmake/CPM.cmake.
file(
    DOWNLOAD
    https://github.com/cpm-cmake/CPM.cmake/releases/latest/download/get_cpm.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake
)
include(${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake)

# "#stable" fetches the stable branch. Use "@0.0.6" to pin a release instead.
CPMAddPackage("gh:DiamonDinoia/treeweave#stable")

add_executable(app "${CMAKE_CURRENT_LIST_DIR}/../main.cpp")
target_link_libraries(app PRIVATE treeweave::treeweave)
