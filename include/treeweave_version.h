/* treeweave_version.h: GENERATED from include/treeweave_version.h.in by
 * cmake/treeweave_generate_version.cmake (single source of truth: the VERSION
 * file at the repo root). The generated copy is committed to the include/ tree
 * so every consumer that already adds include/ to its path finds it; it is
 * regenerated on configure and by the pre-commit hook. Do NOT edit by hand,
 * edit VERSION (or this template) and re-run CMake configure.
 *
 * It is plain C (usable from both treeweave.h and the C++ treeweave.hpp) and
 * carries only the version macros, so it has no dependencies and can be
 * included anywhere.
 */
#ifndef TREEWEAVE_VERSION_H
#define TREEWEAVE_VERSION_H

#define TREEWEAVE_VERSION_MAJOR 0
#define TREEWEAVE_VERSION_MINOR 0
#define TREEWEAVE_VERSION_PATCH 5

/* Combined integer, MAJOR*10000 + MINOR*100 + PATCH, for easy `>=` comparisons.
 * (e.g. 1.2.3 -> 10203; 0.0.0 -> 0.) */
#define TREEWEAVE_VERSION (TREEWEAVE_VERSION_MAJOR * 10000 + TREEWEAVE_VERSION_MINOR * 100 + TREEWEAVE_VERSION_PATCH)

/* Dotted release string, e.g. "1.2.3". */
#define TREEWEAVE_VERSION_STRING "0.0.5"

/* Full version including a dev suffix off a release tag, e.g. "1.2.3-dev.42";
 * equals TREEWEAVE_VERSION_STRING on an exact release commit. */
#define TREEWEAVE_VERSION_FULL "0.0.5-dev.95"

/* Compile-time guard: nonzero iff the treeweave headers are at least
 * `maj.min.pat`. Consumers gate a hard requirement on it, FINUFFT/zlib style:
 *
 *     #if !TREEWEAVE_VERSION_AT_LEAST(1, 0, 0)
 *     #  error "treeweave >= 1.0.0 is required"
 *     #endif
 *
 * (treeweave_version() / treeweave_version_string() in <treeweave.h> report the
 * version of the actually-linked library, which can differ from these macros
 * when a shared libtreeweave_c is swapped under a consumer compiled earlier.) */
#define TREEWEAVE_VERSION_AT_LEAST(maj, min, pat) (TREEWEAVE_VERSION >= ((maj) * 10000 + (min) * 100 + (pat)))

#endif /* TREEWEAVE_VERSION_H */
