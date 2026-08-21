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
// compile, enough to make Debug builds take tens of minutes and OOM CI runners.
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

#if defined(_MSC_VER) && !defined(__clang__)
#include <xmmintrin.h> // _mm_prefetch / _MM_HINT_* (MSVC has no __builtin_prefetch)
#endif

namespace treeweave::detail {

/// Software prefetch hint for a read. `Locality` mirrors __builtin_prefetch's
/// locality arg: 3 = keep in all caches (T0) … 0 = non-temporal (NTA). No-op on
/// compilers without a prefetch intrinsic.
template <int Locality = 3>
TREEWEAVE_ALWAYS_INLINE void prefetch([[maybe_unused]] const void *addr) noexcept {
    static_assert(Locality >= 0 && Locality <= 3, "prefetch locality must be 0..3");
#if defined(__GNUC__) || defined(__clang__)
    __builtin_prefetch(addr, /*rw=*/0, Locality);
#elif defined(_MSC_VER)
    constexpr int hints[] = {_MM_HINT_NTA, _MM_HINT_T2, _MM_HINT_T1, _MM_HINT_T0};
    _mm_prefetch(reinterpret_cast<const char *>(addr), hints[Locality]);
#endif
}

} // namespace treeweave::detail

#endif // TREEWEAVE_DETAIL_COMPILER_MACROS_HPP
