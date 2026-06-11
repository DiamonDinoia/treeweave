#ifndef TREEWEAVE_DETAIL_COMPILER_MACROS_HPP
#define TREEWEAVE_DETAIL_COMPILER_MACROS_HPP

/// Detail-only compiler-attribute helpers. Modeled on polyfit's
/// internal/compiler_macros.h. C++20 is the floor here, so the bare
/// attribute forms are always available; the macros exist to keep call
/// sites readable and to centralise GCC/Clang-only attribute fallbacks.

#if defined(__GNUC__) || defined(__clang__)
#define TREEWEAVE_ALWAYS_INLINE [[gnu::always_inline]] inline
// [[gnu::flatten]] recursively force-inlines the entire callee tree into the
// annotated eval methods. That is a runtime-throughput win in optimized builds,
// but it produces enormous functions that are very slow and memory-hungry to
// compile — enough to make Debug builds take tens of minutes and OOM CI runners.
// Debug builds do not care about runtime speed, so enable flatten only in
// optimized (NDEBUG) builds; it expands to nothing in Debug. This changes
// inlining only, never behavior, so tests are unaffected.
#ifdef NDEBUG
#define TREEWEAVE_FLATTEN [[gnu::flatten]]
#else
#define TREEWEAVE_FLATTEN
#endif
#elif defined(_MSC_VER)
#define TREEWEAVE_ALWAYS_INLINE __forceinline
#define TREEWEAVE_FLATTEN
#else
#define TREEWEAVE_ALWAYS_INLINE inline
#define TREEWEAVE_FLATTEN
#endif

#endif // TREEWEAVE_DETAIL_COMPILER_MACROS_HPP
