# treeweave_generate_version.cmake
#
# Composes TREEWEAVE_VERSION_FULL from the tracked VERSION file plus git state
# and writes include/treeweave_version.h from include/treeweave_version.h.in.
# The header lives inside the source include/ tree (committed), so every consumer
# that already adds include/ to its path finds <treeweave_version.h> — there is
# no separate generated-include dir to propagate to each target.
#
# Usage
# -----
# - From the main project: include(cmake/treeweave_generate_version.cmake)
#   during configure (right after project()).
# - Standalone (pre-commit hook): cmake -P cmake/treeweave_generate_version.cmake
#   With -DCHECK=ON, exits 1 if the generated file would change — the hook then
#   re-stages the newly regenerated file.
#
# Version composition
# -------------------
# Let BASE = contents of ./VERSION (e.g. 0.0.0). Let TAG = `git describe
# --exact-match --tags HEAD` (stripped of a leading `v`). If TAG == BASE we are
# on an exact release commit and TREEWEAVE_VERSION_FULL = BASE. Otherwise the
# suffix is `-dev.N` where N = `git rev-list --count <v-tag>..HEAD` if a
# `v<BASE>` tag exists, else `git rev-list --count HEAD`.
#
# N is derived from committed history only — `git rev-list --count HEAD` ignores
# the staged index and the working tree, so during pre-commit the count is
# stable across retries of the same commit (HEAD hasn't moved). The count only
# advances when a new commit actually lands.

cmake_minimum_required(VERSION 3.20)

# Resolve project source dir — works both inside a CMake configure and under
# `cmake -P` (script mode).
if(CMAKE_SOURCE_DIR AND EXISTS "${CMAKE_SOURCE_DIR}/VERSION")
    set(_tw_src "${CMAKE_SOURCE_DIR}")
else()
    get_filename_component(_tw_src "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()

file(READ "${_tw_src}/VERSION" TREEWEAVE_VERSION_STRING)
string(STRIP "${TREEWEAVE_VERSION_STRING}" TREEWEAVE_VERSION_STRING)

if(NOT TREEWEAVE_VERSION_STRING MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)$")
    message(
        FATAL_ERROR
        "VERSION must be MAJOR.MINOR.PATCH, got: ${TREEWEAVE_VERSION_STRING}"
    )
endif()
set(TREEWEAVE_VERSION_MAJOR "${CMAKE_MATCH_1}")
set(TREEWEAVE_VERSION_MINOR "${CMAKE_MATCH_2}")
set(TREEWEAVE_VERSION_PATCH "${CMAKE_MATCH_3}")

find_package(Git QUIET)

set(_on_exact_tag FALSE)
set(_commit_count 0)
set(_shallow FALSE)

# A shallow clone (CI's default fetch-depth 1) only has HEAD, so the commit
# count is wrong — `rev-list --count HEAD` would yield 1 regardless of real
# history. In that case trust the committed header verbatim: don't rewrite it
# (configure) and don't fail the check (pre-commit hook). The committed value
# was produced by a full clone at commit time.
if(Git_FOUND AND EXISTS "${_tw_src}/.git")
    execute_process(
        COMMAND
            "${GIT_EXECUTABLE}" -C "${_tw_src}" rev-parse
            --is-shallow-repository
        OUTPUT_VARIABLE _is_shallow
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(_is_shallow STREQUAL "true")
        set(_shallow TRUE)
    endif()
endif()

if(Git_FOUND AND EXISTS "${_tw_src}/.git" AND NOT _shallow)
    execute_process(
        COMMAND
            "${GIT_EXECUTABLE}" -C "${_tw_src}" describe --exact-match --tags
            HEAD
        OUTPUT_VARIABLE _exact_tag
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _rc
    )
    if(_rc EQUAL 0)
        string(REGEX REPLACE "^v" "" _exact_tag_stripped "${_exact_tag}")
        if(_exact_tag_stripped STREQUAL TREEWEAVE_VERSION_STRING)
            set(_on_exact_tag TRUE)
        endif()
    endif()

    if(NOT _on_exact_tag)
        # Look for a v<BASE> tag so we can count commits since it.
        execute_process(
            COMMAND
                "${GIT_EXECUTABLE}" -C "${_tw_src}" rev-parse --verify --quiet
                "v${TREEWEAVE_VERSION_STRING}"
            OUTPUT_QUIET
            ERROR_QUIET
            RESULT_VARIABLE _rc
        )
        if(_rc EQUAL 0)
            execute_process(
                COMMAND
                    "${GIT_EXECUTABLE}" -C "${_tw_src}" rev-list --count
                    "v${TREEWEAVE_VERSION_STRING}..HEAD"
                OUTPUT_VARIABLE _commit_count
                ERROR_QUIET
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )
        else()
            execute_process(
                COMMAND
                    "${GIT_EXECUTABLE}" -C "${_tw_src}" rev-list --count HEAD
                OUTPUT_VARIABLE _commit_count
                ERROR_QUIET
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )
        endif()
    endif()
endif()

if(_on_exact_tag)
    set(TREEWEAVE_VERSION_FULL "${TREEWEAVE_VERSION_STRING}")
else()
    set(TREEWEAVE_VERSION_FULL
        "${TREEWEAVE_VERSION_STRING}-dev.${_commit_count}"
    )
endif()

if(_shallow)
    message(
        STATUS
        "treeweave version: shallow clone — trusting committed include/treeweave_version.h"
    )
    return()
endif()

set(_out "${_tw_src}/include/treeweave_version.h")
set(_in "${_tw_src}/include/treeweave_version.h.in")
set(_tmp "${_out}.new")

configure_file("${_in}" "${_tmp}" @ONLY)

set(_changed FALSE)
if(NOT EXISTS "${_out}")
    set(_changed TRUE)
else()
    file(READ "${_tmp}" _tmp_content)
    file(READ "${_out}" _out_content)
    if(NOT "${_tmp_content}" STREQUAL "${_out_content}")
        set(_changed TRUE)
    endif()
endif()

if(_changed)
    file(RENAME "${_tmp}" "${_out}")
else()
    file(REMOVE "${_tmp}")
endif()

if(DEFINED CHECK AND CHECK)
    if(_changed)
        message(
            STATUS
            "treeweave version regenerated to ${TREEWEAVE_VERSION_FULL}; please re-stage include/treeweave_version.h"
        )
        message(FATAL_ERROR "treeweave_version.h out of date")
    endif()
else()
    message(STATUS "treeweave version: ${TREEWEAVE_VERSION_FULL}")
endif()
