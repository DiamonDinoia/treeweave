include_guard(GLOBAL)

# ==============================================================================
# TREEWEAVE Development Helpers
# ==============================================================================
# This file provides CMake helper functions and targets for TREEWEAVE development:
# - Compiler warnings configuration (treeweave_enable_warnings)
# - Sanitizers (ASan/UBSan via StableCoder/cmake-scripts, applied globally)
# - Static analysis (clang-tidy/cppcheck via CMAKE_CXX_CLANG_TIDY/CPPCHECK)
# - Documentation generation (doxygen, sphinx, docs targets)
# - Code coverage reporting (coverage target)
#
# These helpers are only used for development/testing builds and are not
# required when using TREEWEAVE as a header-only library.
# ==============================================================================

# Prepare CPM (CMake Package Manager) for fetching dependencies. Only fetched
# when a feature that needs it (sanitizers) is actually enabled, so the default
# treeweave build does not touch the network.
include(FetchContent)

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
    FetchContent_GetProperties(CPM)
    if(NOT CPM_POPULATED)
        FetchContent_MakeAvailable(CPM)
    endif()
    include(${cpm_SOURCE_DIR}/CPM.cmake)

    set_property(GLOBAL PROPERTY _treeweave_cpm_ready TRUE)
endfunction()

# -------------------------
# Warnings helper (from PoetWarnings.cmake)
# -------------------------
# Enable comprehensive compiler warnings for a target
# Supports GCC, Clang, AppleClang, and MSVC compilers
# Applies warnings with PRIVATE scope for regular targets, INTERFACE scope for interface libraries
function(treeweave_enable_warnings target)
    if(NOT TARGET "${target}")
        message(
            FATAL_ERROR
            "treeweave_enable_warnings called with non-existent target '${target}'"
        )
    endif()

    # Determine the appropriate scope for applying warnings
    # INTERFACE scope for interface libraries (warnings propagate to consumers)
    # PRIVATE scope for other targets (warnings only apply to this target's sources)
    get_target_property(_target_type "${target}" TYPE)
    if(_target_type STREQUAL "INTERFACE_LIBRARY")
        set(_scope INTERFACE)
    else()
        set(_scope PRIVATE)
    endif()

    # Generator expressions for compiler detection (evaluated at build time)
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

    # Additional curated warnings that are checked for compiler support before enabling
    # These are only added if the compiler supports them (GCC-specific flags)
    set(_additional_warnings
        -Wduplicated-cond
        -Wlogical-op
        -Wuseless-cast
        -Winit-self
        -Wmissing-include-dirs
        -Wredundant-decls
    )

    # Check which additional warnings are supported by the current compiler and add them
    include(CheckCXXCompilerFlag)
    foreach(_f IN LISTS _additional_warnings)
        check_cxx_compiler_flag("${_f}" _flag_supported)
        if(_flag_supported)
            list(APPEND _warnings_clang_like ${_f})
        endif()
    endforeach()

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

    # Build compiler-specific warning flags using generator expressions
    # Flags are only applied when compiling C++ code with the matching compiler
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

    # Add -Werror / /WX if treating warnings as errors
    if(TREEWEAVE_WARNINGS_AS_ERRORS)
        list(
            APPEND _compile_options
            $<$<AND:${_lang_is_cxx},${_gnu_or_clang}>:-Werror>
        )
        list(APPEND _compile_options $<$<AND:${_lang_is_cxx},${_msvc}>:/WX>)
    endif()

    target_compile_options(${target} ${_scope} ${_compile_options})
endfunction()

# -------------------------
# Sanitizers: ASan + UBSan via StableCoder/cmake-scripts, fetched with CPM.
# Driven by TREEWEAVE_ENABLE_SANITIZERS (declared in the top-level CMakeLists).
# add_sanitizer_support() applies the flags to every target defined afterwards,
# which is why dev_helpers is included before the library/test/example targets.
# -------------------------
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

# -------------------------
# Coverage instrumentation, driven by TREEWEAVE_ENABLE_COVERAGE (declared in the
# top-level CMakeLists). Like the sanitizer block above, this must run before the
# library/test targets are defined: add_compile_options/add_link_options apply to
# every target created afterwards, so the headers (instantiated in the test TUs)
# and the C-ABI library all emit .gcno/.gcda for the `coverage` target to collect.
# Without this the option was inert: the build stayed uninstrumented, ctest passed,
# and lcov aborted with "no .gcda files found".
# -------------------------
if(TREEWEAVE_ENABLE_COVERAGE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        # --coverage == -fprofile-arcs -ftest-coverage (+ links libgcov). The
        # batch evaluator runs leaf kernels on multiple threads, so atomic
        # profile updates avoid counter races corrupting the .gcda.
        add_compile_options(--coverage -fprofile-update=atomic)
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

# -------------------------
# Static analysis: clang-tidy / cppcheck via CMake's built-in driver. Report-
# only (no -warnings-as-errors yet); clang-tidy reads .clang-tidy from the tree
# and only reports on our own headers. Tools are looked up on PATH and skipped
# with a warning if absent. Defaults follow TREEWEAVE_ENABLE_STATIC_ANALYSIS.
# -------------------------
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
            # Trailing slash on include/treeweave/ is deliberate: it scopes the
            # filter to the C++ headers in that directory and excludes the
            # sibling C ABI header include/treeweave.h (which must stay valid C —
            # clang-tidy's C++ fixes would corrupt it).
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
            # Suppress two opinionated `style` checks that conflict with this
            # library's deliberate design (newer cppcheck on CI enables them; the
            # version used for local verification did not):
            #   - noExplicitConstructor: detail::Value's single-arg ctors are
            #     intentionally implicit (scalar/array/pointer construction is
            #     ergonomic and used pervasively in the eval pipeline); making
            #     them explicit would be an API/behaviour change.
            #   - useStlAlgorithm: the flagged sites are hot-path raw loops in a
            #     SIMD library; rewriting them as std::accumulate/all_of/copy
            #     would obscure intent for no measured gain (and risk codegen
            #     regressions). We keep the obvious loop, matching the existing
            #     NOLINT pattern for clang-tidy checks the project rejects.
            "--suppress=noExplicitConstructor"
            "--suppress=useStlAlgorithm"
            # Fail the build on any other cppcheck finding so regressions are
            # caught in CI rather than scrolling past. The library is
            # cppcheck-clean today; tests and fetched deps are excluded.
            "--error-exitcode=1"
        )
    else()
        message(
            WARNING
            "TREEWEAVE_ENABLE_CPPCHECK is ON but cppcheck was not found on PATH"
        )
    endif()
endif()

# -------------------------
# Docs helper (from PoetDocs.cmake)
# -------------------------
option(
    TREEWEAVE_GENERATE_DOCS
    "Generate documentation using Doxygen + Sphinx pipeline"
    OFF
)

if(TREEWEAVE_GENERATE_DOCS)
    # Require Doxygen for API documentation extraction
    find_package(Doxygen REQUIRED)
    # Require Sphinx for generating HTML documentation
    find_program(SPHINX_BUILD_EXECUTABLE NAMES sphinx-build REQUIRED)
    # Require Python for Sphinx and its extensions
    find_package(Python COMPONENTS Interpreter REQUIRED)

    # Check if required Python packages (breathe, exhale) are installed
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

    # Generate Doxyfile from template
    configure_file(
        ${CMAKE_SOURCE_DIR}/docs/Doxyfile.in
        ${CMAKE_BINARY_DIR}/docs/Doxyfile
        @ONLY
    )

    # Target: Generate Doxygen XML output from source code
    add_custom_target(
        doxygen
        COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_BINARY_DIR}/docs/Doxyfile
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/docs
        COMMENT "Generating API documentation with Doxygen"
    )

    # Target: Generate HTML documentation from Doxygen XML using Sphinx
    add_custom_target(
        sphinx
        DEPENDS doxygen
        COMMAND
            ${CMAKE_COMMAND} -E env
            DOXYGEN_XML_OUTPUT=${CMAKE_BINARY_DIR}/docs/xml
            ${SPHINX_BUILD_EXECUTABLE} -b html ${CMAKE_SOURCE_DIR}/docs
            ${CMAKE_BINARY_DIR}/docs/_build/html
        COMMENT "Generating HTML documentation with Sphinx"
    )

    # Target: Complete documentation build (alias for sphinx target)
    add_custom_target(docs DEPENDS sphinx)
    message(
        STATUS
        "TREEWEAVE: Documentation targets enabled (doxygen, sphinx, docs)"
    )
endif()

# -------------------------
# Coverage target (moved from top-level)
# -------------------------
# Creates a `coverage` custom target that:
# 1. Builds all test executables
# 2. Runs the test suite using CTest
# 3. Collects code coverage data
# 4. Generates an HTML coverage report
#
# Prefers lcov+genhtml (more robust), falls back to gcovr if unavailable
find_program(GCOVR_EXECUTABLE gcovr)
find_program(LCOV_EXECUTABLE lcov)
find_program(GENHTML_EXECUTABLE genhtml)

# Prefer lcov+genhtml when available (generally more robust and handles complex build trees better)
# Falls back to gcovr if lcov/genhtml are not found
if(LCOV_EXECUTABLE AND GENHTML_EXECUTABLE)
    set(LCOV_INFO ${CMAKE_BINARY_DIR}/coverage.info)
    set(LCOV_FILTERED ${CMAKE_BINARY_DIR}/coverage.filtered.info)
    set(COVERAGE_DIR ${CMAKE_BINARY_DIR}/coverage)

    add_custom_target(
        coverage
        COMMAND
            ${CMAKE_CTEST_COMMAND} --test-dir ${CMAKE_BINARY_DIR}
            --output-on-failure
        COMMAND
            ${LCOV_EXECUTABLE} --capture --directory ${CMAKE_BINARY_DIR}
            --output-file ${LCOV_INFO} --ignore-errors
            inconsistent,unused,source,gcov,mismatch
        # Keep ONLY treeweave's own sources (the header library + the C-ABI
        # implementation) so the coverage % is reliable: this allowlist drops
        # system headers (/usr/*), fetched dependencies (*/_deps/*), the Catch2
        # test framework, and the test/example/benchmark sources in one stroke,
        # none of which should count toward the library's coverage.
        COMMAND
            ${LCOV_EXECUTABLE} --extract ${LCOV_INFO} "*/include/treeweave*"
            "*/src/capi/*" --output-file ${LCOV_FILTERED} --ignore-errors
            inconsistent,unused
        # genhtml may still reference a source file it cannot open (e.g. a
        # generated/relocated dependency header); skip those rather than abort.
        COMMAND
            ${GENHTML_EXECUTABLE} -o ${COVERAGE_DIR} ${LCOV_FILTERED}
            --ignore-errors source
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT
            "Running tests and generating coverage report (lcov+genhtml) -> ${COVERAGE_DIR}/index.html"
        VERBATIM
    )
elseif(GCOVR_EXECUTABLE)
    # Fallback to gcovr if lcov/genhtml aren't available
    add_custom_target(
        coverage
        COMMAND
            ${CMAKE_CTEST_COMMAND} --test-dir ${CMAKE_BINARY_DIR}
            --output-on-failure
        # Filter to project sources (include/treeweave and tests), excluding external dependencies and system headers
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

# The coverage target runs CTest, so it must build the test executables first.
# Those targets are defined later in tests/CMakeLists.txt (added after this
# include), so the dependency wiring lives there: it appends each test target to
# the global TREEWEAVE_TEST_TARGETS property and add_dependencies(coverage ...)
# on them once the suite is defined.
