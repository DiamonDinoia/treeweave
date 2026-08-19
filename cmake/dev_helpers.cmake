include_guard(GLOBAL)

# Development helpers: warnings, sanitizers, static analysis, docs, coverage.
# Not needed when using treeweave as a library.

include(FetchContent)

include(CheckCXXCompilerFlag)
set(_tw_additional_warnings_candidates
    -Wduplicated-cond
    -Wlogical-op
    -Wuseless-cast
    -Winit-self
    -Wmissing-include-dirs
    -Wredundant-decls
)
set(_tw_additional_warnings_supported)
foreach(_f IN LISTS _tw_additional_warnings_candidates)
    string(MAKE_C_IDENTIFIER "${_f}" _flagvar)
    check_cxx_compiler_flag("${_f}" CXX_HAS_${_flagvar})
    if(CXX_HAS_${_flagvar})
        list(APPEND _tw_additional_warnings_supported ${_f})
    endif()
endforeach()
unset(_f)
unset(_flagvar)

function(_treeweave_ensure_cpm)
    get_property(_done GLOBAL PROPERTY _treeweave_cpm_ready SET)
    if(_done)
        return()
    endif()

    FetchContent_Declare(
        CPM
        URL
            https://github.com/cpm-cmake/CPM.cmake/releases/download/v0.42.0/CPM.cmake
        URL_HASH
            SHA256=2020b4fc42dba44817983e06342e682ecfc3d2f484a581f11cc5731fbe4dce8a
        DOWNLOAD_NO_EXTRACT TRUE
    )
    FetchContent_MakeAvailable(CPM)
    # Use GetProperties, not ${CPM_SOURCE_DIR}: lowercased variant isn't set on
    # reconfigure when FetchContent early-returns without repopulating it.
    FetchContent_GetProperties(CPM SOURCE_DIR _treeweave_cpm_src)
    include(${_treeweave_cpm_src}/CPM.cmake)

    set_property(GLOBAL PROPERTY _treeweave_cpm_ready TRUE)
endfunction()

function(treeweave_enable_warnings target)
    if(NOT TARGET "${target}")
        message(
            FATAL_ERROR
            "treeweave_enable_warnings called with non-existent target '${target}'"
        )
    endif()

    get_target_property(_target_type "${target}" TYPE)
    if(_target_type STREQUAL "INTERFACE_LIBRARY")
        set(_scope INTERFACE)
    else()
        set(_scope PRIVATE)
    endif()

    set(_clang_like
        $<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>
    )
    set(_gnu $<CXX_COMPILER_ID:GNU>)
    set(_gnu_or_clang $<OR:${_gnu},${_clang_like}>)
    set(_msvc $<CXX_COMPILER_ID:MSVC>)
    set(_lang_is_cxx $<COMPILE_LANGUAGE:CXX>)

    set(_warnings_clang_like
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wconversion
        -Wsign-conversion
        -Wdouble-promotion
        -Wold-style-cast
        -Wnon-virtual-dtor
        -Wnull-dereference
        -Woverloaded-virtual
        -Wcast-align
        -Wunused
        -Wimplicit-fallthrough
        -Wformat=2
    )

    list(APPEND _warnings_clang_like ${_tw_additional_warnings_supported})

    set(_warnings_gnu_only -Wmisleading-indentation -Wsuggest-override)

    set(_warnings_msvc
        /W4
        /permissive-
        /bigobj
        /w14242
        /w14254
        /w14263
        /w14265
        /w14287
        /we4289
        /w14296
        /w14311
        /w14545
        /w14546
        /w14547
        /w14549
        /w14555
        /w14619
        /w14640
        /w14826
        /w14905
        /w14906
        /w14928
        # MSVC's C4702 ("unreachable code") fires inside heavily-templated
        # `if constexpr` ladders in polyfit / poet / our numerics — it's a
        # known false-positive class with no clean source-level fix. Disable
        # it explicitly so /WX doesn't reject those harmless paths.
        /wd4702
    )

    set(_compile_options)
    foreach(flag IN LISTS _warnings_clang_like)
        list(
            APPEND _compile_options
            $<$<AND:${_lang_is_cxx},${_gnu_or_clang}>:${flag}>
        )
    endforeach()
    foreach(flag IN LISTS _warnings_gnu_only)
        list(APPEND _compile_options $<$<AND:${_lang_is_cxx},${_gnu}>:${flag}>)
    endforeach()
    foreach(flag IN LISTS _warnings_msvc)
        list(APPEND _compile_options $<$<AND:${_lang_is_cxx},${_msvc}>:${flag}>)
    endforeach()

    if(TREEWEAVE_WARNINGS_AS_ERRORS)
        list(
            APPEND _compile_options
            $<$<AND:${_lang_is_cxx},${_gnu_or_clang}>:-Werror>
        )
        list(APPEND _compile_options $<$<AND:${_lang_is_cxx},${_msvc}>:/WX>)
    endif()

    target_compile_options(${target} ${_scope} ${_compile_options})
endfunction()

# Must run before library/test targets so sanitizer flags apply globally.
if(TREEWEAVE_ENABLE_SANITIZERS)
    _treeweave_ensure_cpm()
    cpmaddpackage(
        NAME cmake_scripts
        GITHUB_REPOSITORY StableCoder/cmake-scripts
        GIT_TAG 003c241725972de76e87afc0986209829b9a7d4f
        DOWNLOAD_ONLY YES
    )
    include("${cmake_scripts_SOURCE_DIR}/sanitizers.cmake")
    add_sanitizer_support(address undefined)
endif()

# Must run before library/test targets (ordering constraint).
if(TREEWEAVE_ENABLE_COVERAGE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        # Counter updates default to non-atomic (-fprofile-update=single): fast,
        # and correct for the single-threaded majority of the suite (a lone
        # thread never races its own .gcda counters). Atomic updates are much
        # slower per basic block under -O0 and are only needed where instrumented
        # code runs concurrently — applied per-target via
        # treeweave_coverage_atomic_counters() to the threaded TUs only.
        add_compile_options(--coverage)
        add_link_options(--coverage)
        message(
            STATUS
            "TREEWEAVE: coverage instrumentation enabled (--coverage)"
        )
    else()
        message(
            WARNING
            "TREEWEAVE_ENABLE_COVERAGE is ON but the compiler "
            "(${CMAKE_CXX_COMPILER_ID}) is not GCC/Clang; no coverage flags added."
        )
    endif()
endif()

# Opt a target into atomic coverage counters. Needed only for TUs whose
# instrumented code is exercised by multiple threads concurrently (else races
# silently drop .gcda increments -> undercounted coverage). No-op when coverage
# is off or the compiler lacks the flag.
function(treeweave_coverage_atomic_counters tgt)
    if(TREEWEAVE_ENABLE_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${tgt} PRIVATE -fprofile-update=atomic)
    endif()
endfunction()

option(
    TREEWEAVE_ENABLE_CLANG_TIDY
    "Run clang-tidy on C++ targets"
    ${TREEWEAVE_ENABLE_STATIC_ANALYSIS}
)
option(
    TREEWEAVE_ENABLE_CPPCHECK
    "Run cppcheck on C++ targets"
    ${TREEWEAVE_ENABLE_STATIC_ANALYSIS}
)

if(TREEWEAVE_ENABLE_CLANG_TIDY)
    find_program(TREEWEAVE_CLANG_TIDY_EXE NAMES clang-tidy)
    if(TREEWEAVE_CLANG_TIDY_EXE)
        set(CMAKE_CXX_CLANG_TIDY
            "${TREEWEAVE_CLANG_TIDY_EXE}"
            # Trailing slash scopes filter to include/treeweave/ only, excluding
            # the C ABI header include/treeweave.h (must stay valid C).
            "-header-filter=^${PROJECT_SOURCE_DIR}/(include/treeweave/|src)"
            "--extra-arg=-fsyntax-only"
        )
    else()
        message(
            WARNING
            "TREEWEAVE_ENABLE_CLANG_TIDY is ON but clang-tidy was not found on PATH"
        )
    endif()
endif()

if(TREEWEAVE_ENABLE_CPPCHECK)
    find_program(TREEWEAVE_CPPCHECK_EXE NAMES cppcheck)
    if(TREEWEAVE_CPPCHECK_EXE)
        set(CMAKE_CXX_CPPCHECK
            "${TREEWEAVE_CPPCHECK_EXE}"
            "--inline-suppr"
            "--enable=warning,style,performance,portability"
            # noExplicitConstructor: detail::Value's single-arg ctors are
            # intentionally implicit; making them explicit is an API change.
            "--suppress=noExplicitConstructor"
            # useStlAlgorithm: hot-path raw loops; rewriting would obscure intent.
            "--suppress=useStlAlgorithm"
            "--error-exitcode=1"
        )
    else()
        message(
            WARNING
            "TREEWEAVE_ENABLE_CPPCHECK is ON but cppcheck was not found on PATH"
        )
    endif()
endif()

if(TREEWEAVE_GENERATE_DOCS)
    find_package(Doxygen REQUIRED)
    find_program(SPHINX_BUILD_EXECUTABLE NAMES sphinx-build REQUIRED)
    find_package(Python COMPONENTS Interpreter REQUIRED)

    execute_process(
        COMMAND ${Python_EXECUTABLE} -c "import breathe, exhale"
        RESULT_VARIABLE DOCS_DEPS_CHECK_RESULT
        OUTPUT_QUIET
        ERROR_QUIET
    )

    if(NOT DOCS_DEPS_CHECK_RESULT EQUAL 0)
        message(
            WARNING
            "Python packages 'breathe' and 'exhale' not found. Docs generation may fail. Please run 'pip install -r docs/requirements.txt'."
        )
    endif()

    configure_file(
        ${PROJECT_SOURCE_DIR}/docs/Doxyfile.in
        ${PROJECT_BINARY_DIR}/docs/Doxyfile
        @ONLY
    )

    add_custom_target(
        doxygen
        COMMAND ${DOXYGEN_EXECUTABLE} ${PROJECT_BINARY_DIR}/docs/Doxyfile
        WORKING_DIRECTORY ${PROJECT_BINARY_DIR}/docs
        COMMENT "Generating API documentation with Doxygen"
    )

    add_custom_target(
        sphinx
        DEPENDS doxygen
        COMMAND
            ${CMAKE_COMMAND} -E env
            DOXYGEN_XML_OUTPUT=${PROJECT_BINARY_DIR}/docs/xml
            ${SPHINX_BUILD_EXECUTABLE} -b html ${PROJECT_SOURCE_DIR}/docs
            ${PROJECT_BINARY_DIR}/docs/_build/html
        COMMENT "Generating HTML documentation with Sphinx"
    )

    add_custom_target(docs DEPENDS sphinx)
    message(
        STATUS
        "TREEWEAVE: Documentation targets enabled (doxygen, sphinx, docs)"
    )
endif()

find_program(GCOVR_EXECUTABLE gcovr)
find_program(LCOV_EXECUTABLE lcov)
find_program(GENHTML_EXECUTABLE genhtml)

if(LCOV_EXECUTABLE AND GENHTML_EXECUTABLE)
    set(LCOV_INFO ${CMAKE_BINARY_DIR}/coverage.info)
    set(LCOV_FILTERED ${CMAKE_BINARY_DIR}/coverage.filtered.info)
    set(COVERAGE_DIR ${CMAKE_BINARY_DIR}/coverage)

    add_custom_target(
        coverage
        COMMAND
            ${CMAKE_CTEST_COMMAND} --test-dir ${CMAKE_BINARY_DIR}
            --output-on-failure --parallel
        COMMAND
            ${LCOV_EXECUTABLE} --capture --directory ${CMAKE_BINARY_DIR}
            --output-file ${LCOV_INFO} --ignore-errors
            inconsistent,unused,source,gcov,mismatch
        # Allowlist treeweave sources only; drops deps/system/test sources.
        COMMAND
            ${LCOV_EXECUTABLE} --extract ${LCOV_INFO} "*/include/treeweave*"
            "*/src/capi/*" --output-file ${LCOV_FILTERED} --ignore-errors
            inconsistent,unused
        # --ignore-errors source: genhtml may not find generated/relocated headers.
        COMMAND
            ${GENHTML_EXECUTABLE} -o ${COVERAGE_DIR} ${LCOV_FILTERED}
            --ignore-errors source
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT
            "Running tests and generating coverage report (lcov+genhtml) -> ${COVERAGE_DIR}/index.html"
        VERBATIM
    )
elseif(GCOVR_EXECUTABLE)
    add_custom_target(
        coverage
        COMMAND
            ${CMAKE_CTEST_COMMAND} --test-dir ${CMAKE_BINARY_DIR}
            --output-on-failure --parallel
        COMMAND
            ${GCOVR_EXECUTABLE} -r ${CMAKE_SOURCE_DIR} --filter
            "include/treeweave/|tests/" --exclude ".*/_deps/.*" --exclude
            "/usr/.*" --gcov-ignore-errors=no_working_dir_found --html
            --html-details -o ${CMAKE_BINARY_DIR}/coverage-report.html
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT
            "Running tests and generating coverage report (gcovr) -> ${CMAKE_BINARY_DIR}/coverage-report.html"
        VERBATIM
    )
else()
    message(
        STATUS
        "Coverage tools not found: install 'lcov'+'genhtml' or 'gcovr' to enable the 'coverage' target."
    )
    add_custom_target(
        coverage
        COMMAND
            ${CMAKE_COMMAND} -E echo
            "Coverage tools missing. Install 'lcov'+'genhtml' or 'gcovr' and re-run CMake to enable coverage generation."
    )
endif()

# Test-target dependencies wired in tests/CMakeLists.txt via TREEWEAVE_TEST_TARGETS.
