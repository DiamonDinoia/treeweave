# bindings/js — JavaScript/TypeScript binding over the C ABI, with two
# toolchain-selected backends:
#   * native (default): a Node-API `.node` addon linking treeweave_c_static,
#     exactly like the Python nanobind extension. Headers are located via
#     node-addon-api (header-only, over the ABI-stable node_api.h that ships
#     with Node) — no cmake-js, so the project's preset/CTest flow is unchanged.
#   * WASM (under emcc): the same C ABI compiled to a single WASM module; the C
#     ABI symbols are exported by name and the Emscripten runtime drives them.
# Added by cmake/treeweave_bindings.cmake when TREEWEAVE_BUILD_JS=ON. A missing
# toolchain (node / node-addon-api / tsc) degrades to a STATUS message.
#
# Artifacts land in this package's dist/ (gitignored): treeweave.node (native),
# treeweave.mjs + treeweave.wasm (WASM), and the tsc output (*.js + *.d.ts).

include_guard()

find_program(TREEWEAVE_NODE_EXECUTABLE NAMES node)
if(NOT TREEWEAVE_NODE_EXECUTABLE)
    message(STATUS "treeweave[js]: node not found — skipping.")
    return()
endif()

set(_js_dir "${CMAKE_CURRENT_SOURCE_DIR}")
set(_js_dist "${_js_dir}/dist")
file(MAKE_DIRECTORY "${_js_dist}")

if(EMSCRIPTEN)
    # ---- WASM backend --------------------------------------------------------
    # The public surface is the C ABI itself, exported by name (underscore
    # prefixed). The exported-functions list doubles as the linker's set of
    # roots, pulling the needed members out of the treeweave_c_static archive.
    add_executable(treeweave_wasm "${_js_dir}/src/wasm_glue.cpp")
    target_link_libraries(treeweave_wasm PRIVATE treeweave_c_static)

    set(_tw_exported_functions
        "_treeweave_fit,_treeweavef_fit,_treeweave_eval,_treeweavef_eval,_treeweave_batch,_treeweavef_batch,_treeweave_sorted,_treeweavef_sorted,_treeweave_transposed,_treeweavef_transposed,_treeweave_memory_usage,_treeweave_print_stats,_treeweave_free,_treeweave_dtype,_treeweave_input_dim,_treeweave_output_dim,_treeweave_last_error,_treeweave_version,_treeweave_version_string,_malloc,_free"
    )
    set(_tw_runtime_methods
        "addFunction,removeFunction,ccall,cwrap,HEAPF64,HEAPF32,HEAP32,HEAPU8"
    )
    target_link_options(
        treeweave_wasm
        PRIVATE
            "-sMODULARIZE=1"
            "-sEXPORT_ES6=1"
            "-sALLOW_TABLE_GROWTH=1"
            "-sALLOW_MEMORY_GROWTH=1"
            "-sENVIRONMENT=web,node"
            "--no-entry"
            "-sEXPORTED_FUNCTIONS=[${_tw_exported_functions}]"
            "-sEXPORTED_RUNTIME_METHODS=[${_tw_runtime_methods}]"
    )
    set_target_properties(
        treeweave_wasm
        PROPERTIES
            OUTPUT_NAME treeweave
            SUFFIX
                ".mjs" # emcc emits treeweave.mjs (+ treeweave.wasm) here
            RUNTIME_OUTPUT_DIRECTORY "${_js_dist}"
    )
    message(STATUS "treeweave[js]: WASM module -> ${_js_dist}/treeweave.mjs")
else()
    # ---- native N-API addon --------------------------------------------------
    # node-addon-api's include_dir is relative to cwd, so resolve it from the
    # package dir (where node_modules lives) to an absolute path.
    execute_process(
        COMMAND
            "${TREEWEAVE_NODE_EXECUTABLE}" -p
            "require('path').resolve(require('node-addon-api').include_dir)"
        WORKING_DIRECTORY "${_js_dir}"
        OUTPUT_VARIABLE _napi_inc
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _napi_res
        ERROR_QUIET
    )
    if(NOT _napi_res EQUAL 0 OR NOT EXISTS "${_napi_inc}")
        message(
            STATUS
            "treeweave[js]: node-addon-api not found (run 'npm install' in bindings/js) — skipping native addon."
        )
    else()
        # node_api.h ships inside the Node distribution's include/node directory.
        execute_process(
            COMMAND
                "${TREEWEAVE_NODE_EXECUTABLE}" -e
                "const p=require('path');process.stdout.write(p.join(p.dirname(process.execPath),'..','include','node'))"
            OUTPUT_VARIABLE _node_inc
            OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE _node_res
        )
        if(NOT _node_res EQUAL 0 OR NOT EXISTS "${_node_inc}")
            message(
                WARNING
                "treeweave[js]: could not resolve node include dir — skipping native addon."
            )
        else()
            add_library(
                treeweave_node
                MODULE
                "${_js_dir}/src/treeweave_napi.cpp"
            )
            target_include_directories(
                treeweave_node
                PRIVATE "${_napi_inc}" "${_node_inc}"
            )
            target_link_libraries(treeweave_node PRIVATE treeweave_c_static)
            target_compile_definitions(
                treeweave_node
                PRIVATE NAPI_VERSION=8 NAPI_CPP_EXCEPTIONS
            )
            set_target_properties(
                treeweave_node
                PROPERTIES
                    PREFIX ""
                    SUFFIX ".node"
                    OUTPUT_NAME treeweave
                    LIBRARY_OUTPUT_DIRECTORY "${_js_dist}"
            )
            if(APPLE)
                # node resolves the napi_* symbols at load time on macOS.
                target_link_options(
                    treeweave_node
                    PRIVATE -undefined dynamic_lookup
                )
            elseif(WIN32)
                # MSVC cannot leave the napi_* imports unresolved (unlike ELF /
                # dyld). Link node's import library and delay-load node.exe so
                # they bind to the host process at load time — the node-gyp
                # approach. TREEWEAVE_NODE_LIB is node.lib for the target node,
                # fetched by the prebuild workflow.
                if(NOT TREEWEAVE_NODE_LIB)
                    message(
                        FATAL_ERROR
                        "treeweave[js]: Windows native addon needs -DTREEWEAVE_NODE_LIB=<path to node.lib>"
                    )
                endif()
                target_sources(
                    treeweave_node
                    PRIVATE "${_js_dir}/src/win_delay_load_hook.cc"
                )
                target_link_libraries(
                    treeweave_node
                    PRIVATE "${TREEWEAVE_NODE_LIB}" delayimp
                )
                target_link_options(treeweave_node PRIVATE /DELAYLOAD:node.exe)
            endif()
            message(
                STATUS
                "treeweave[js]: native addon -> ${_js_dist}/treeweave.node"
            )
        endif()
    endif()
endif()

# ---- TypeScript -> dist/*.js (+ .d.ts), built as part of the default target --
find_program(
    TREEWEAVE_TSC
    NAMES tsc
    PATHS "${_js_dir}/node_modules/.bin"
    NO_DEFAULT_PATH
)
if(TREEWEAVE_TSC)
    add_custom_target(
        treeweave_js_ts
        ALL
        COMMAND "${TREEWEAVE_TSC}" -p "${_js_dir}/tsconfig.json"
        WORKING_DIRECTORY "${_js_dir}"
        COMMENT "treeweave[js]: compiling TypeScript -> dist/"
        VERBATIM
    )
else()
    message(
        STATUS
        "treeweave[js]: tsc not found in node_modules/.bin (run 'npm install') — dist/*.js not built here."
    )
endif()

# ---- CTest: the node:test suite, exercising whichever backends are in dist/ --
add_test(
    NAME js_treeweave
    COMMAND
        "${TREEWEAVE_NODE_EXECUTABLE}" --test
        "${_js_dir}/test/treeweave.test.mjs"
)
message(STATUS "treeweave[js]: test 'js_treeweave' registered.")
