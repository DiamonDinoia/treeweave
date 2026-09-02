# Consume treeweave with FetchContent, which ships with CMake. This is the
# recommended C++ route: no bootstrap, no install step, nothing to download by
# hand. tools/ci/install-test.sh configures and runs this project on every PR.
cmake_minimum_required(VERSION 3.20)
project(treeweave_quickstart LANGUAGES CXX)

include(FetchContent)
FetchContent_Declare(
    treeweave
    GIT_REPOSITORY https://github.com/DiamonDinoia/treeweave.git
    GIT_TAG stable # a branch that always points at the newest release
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(treeweave)

add_executable(app "${CMAKE_CURRENT_LIST_DIR}/../main.cpp")
target_link_libraries(app PRIVATE treeweave::treeweave)
