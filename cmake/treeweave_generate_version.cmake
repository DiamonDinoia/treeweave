# treeweave_generate_version.cmake — composes TREEWEAVE_VERSION_FULL from
# VERSION + git state; writes include/treeweave_version.h (committed, in-tree).
# Script mode: cmake -P ...; -DCHECK=ON exits 1 if header would change.
# (see devel/agents/build-notes.md — "Version composition logic")

cmake_minimum_required(VERSION 3.20)

if(CMAKE_SOURCE_DIR AND EXISTS "${CMAKE_SOURCE_DIR}/VERSION")
    set(_tw_src "${CMAKE_SOURCE_DIR}")
else()
    get_filename_component(_tw_src "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()

file(READ "${_tw_src}/VERSION" TREEWEAVE_VERSION_STRING)
string(STRIP "${TREEWEAVE_VERSION_STRING}" TREEWEAVE_VERSION_STRING)

# Release workflow passes -DTREEWEAVE_RELEASE_VERSION=<ver> to pin headers at
# clean X.Y.Z before the tag is pushed. (see devel/agents/build-notes.md)
if(
    DEFINED TREEWEAVE_RELEASE_VERSION
    AND NOT TREEWEAVE_RELEASE_VERSION STREQUAL ""
)
    set(TREEWEAVE_VERSION_STRING "${TREEWEAVE_RELEASE_VERSION}")
    if(NOT TREEWEAVE_VERSION_STRING MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)$")
        message(
            FATAL_ERROR
            "VERSION must be MAJOR.MINOR.PATCH, got: ${TREEWEAVE_VERSION_STRING}"
        )
    endif()
    set(TREEWEAVE_VERSION_MAJOR "${CMAKE_MATCH_1}")
    set(TREEWEAVE_VERSION_MINOR "${CMAKE_MATCH_2}")
    set(TREEWEAVE_VERSION_PATCH "${CMAKE_MATCH_3}")
    set(TREEWEAVE_VERSION_FULL "${TREEWEAVE_VERSION_STRING}")
    set(_out "${_tw_src}/include/treeweave_version.h")
    set(_in "${_tw_src}/include/treeweave_version.h.in")
    configure_file("${_in}" "${_out}" @ONLY)
    message(
        STATUS
        "treeweave version (release override): ${TREEWEAVE_VERSION_FULL}"
    )
    return()
endif()

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

# Shallow clone (CI fetch-depth:1): trust committed header verbatim.
# (see devel/agents/build-notes.md — "Shallow clone behavior")
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
