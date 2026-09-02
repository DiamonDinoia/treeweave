#!/usr/bin/env bash
# Build and run every quick-start consumer project under examples/quickstart/.
#
# Those projects are the recipes the docs embed, so this script is what makes a
# docs page a tested artifact rather than a stale snippet. It substitutes this
# checkout for the GitHub fetch through the mechanisms CMake and CPM already
# provide, so the committed files stay exactly what a user copies.
#
#   tools/ci/install-test.sh              # every route
#   tools/ci/install-test.sh cpp-cpm      # one route
#
# TREEWEAVE_INSTALL_TEST_DIR   where to build (default <root>/_install-test)
# TREEWEAVE_CMAKE_ARGS         extra CMake arguments for the treeweave builds,
#                              word-split; the Windows leg passes the cl.exe
#                              selection through it.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
work="${TREEWEAVE_INSTALL_TEST_DIR:-$root/_install-test}"
prefix="$work/prefix"
# Seeded with the build type so the array is never empty: bash 3.2 (macOS) errors
# on "${empty[@]}" under `set -u`.
# shellcheck disable=SC2206  # deliberate word splitting: this is a CMake argv
extra_args=(-DCMAKE_BUILD_TYPE=Release ${TREEWEAVE_CMAKE_ARGS:-})
routes=("$@")
if [ ${#routes[@]} -eq 0 ]; then
    routes=(cpp-fetchcontent cpp-cpm cpp-find_package c-find_package cxx-headers)
fi

rm -rf "$work"
mkdir -p "$work"

# find_package routes need an install tree. Build one once, shared by both.
needs_prefix=0
for route in "${routes[@]}"; do
    case "$route" in *find_package) needs_prefix=1 ;; esac
done
if [ "$needs_prefix" = 1 ]; then
    echo "=== building an install prefix at $prefix"
    cmake -S "$root" -B "$work/build" \
        -DTREEWEAVE_BUILD_TESTS=OFF -DTREEWEAVE_BUILD_EXAMPLES=OFF \
        "${extra_args[@]}"
    cmake --build "$work/build" --config Release -j
    cmake --install "$work/build" --prefix "$prefix" --config Release
fi

# Run the app a route built, whatever the generator named it.
run_app() {
    local dir=$1 app
    for app in "$dir/app" "$dir/app.exe" "$dir/Release/app.exe"; do
        if [ -x "$app" ]; then
            "$app"
            return
        fi
    done
    echo "ERROR: $dir built no executable" >&2
    exit 1
}

for route in "${routes[@]}"; do
    echo
    echo "=== route: $route"
    src="$root/examples/quickstart/$route"
    build="$work/$route"
    args=(-S "$src" -B "$build" "${extra_args[@]}")

    case "$route" in
    cpp-fetchcontent)
        # CMake's own override: fetch nothing, use this checkout.
        args+=(-DFETCHCONTENT_SOURCE_DIR_TREEWEAVE="$root")
        ;;
    cpp-cpm)
        # CPM's own override, same idea.
        args+=(-DCPM_treeweave_SOURCE="$root")
        ;;
    *find_package)
        args+=(-DCMAKE_PREFIX_PATH="$prefix")
        # Windows has no RPATH; the DLL lives in bin/.
        [ -d "$prefix/bin" ] && PATH="$prefix/bin:$PATH" && export PATH
        ;;
    cxx-headers)
        # No CMake at all: pack the consolidated header tree exactly as the
        # release asset is packed, extract it elsewhere, and compile with one
        # -I, which is the "download the headers" recipe in the docs.
        cmake -S "$root" -B "$work/hdrs" -DTREEWEAVE_BUILD_TESTS=OFF \
            -DTREEWEAVE_BUILD_EXAMPLES=OFF "${extra_args[@]}"
        cmake --build "$work/hdrs" -j
        tar -C "$work/hdrs" -czf "$work/treeweave-cxx-headers.tar.gz" include
        mkdir -p "$build" && tar -C "$build" -xzf "$work/treeweave-cxx-headers.tar.gz"
        "${CXX:-c++}" -std=c++20 -O3 "$root/examples/quickstart/main.cpp" \
            -I"$build/include" -o "$build/app"
        run_app "$build"
        continue
        ;;
    *)
        echo "ERROR: unknown route '$route'" >&2
        exit 1
        ;;
    esac

    cmake "${args[@]}"
    cmake --build "$build" --config Release -j
    run_app "$build"
done

echo
echo "all routes built and ran"
