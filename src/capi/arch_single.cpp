// arch_single.cpp: single-architecture ISA introspection.
//
// Selected by CMake when multi-arch dispatch is OFF (the default) or the
// target has no ISA ladder. The variant TUs compile at the same `-march` as
// this TU and use the header-only `InlineKernels` policy, so every kernel is
// fully inlined into the pipeline — no function-pointer indirection.
//
// One rung means nothing to select, so this TU carries only the introspection
// pair. The fit path calls `make_eval` (c_binding.hpp) directly.

#include <string_view>

#include <xsimd/xsimd.hpp>

#include <treeweave/detail/c_binding.hpp>

namespace treeweave::capi {

// Introspection. The ladder is one rung here: the arch this TU and every
// variant TU compile at.
auto active_arch() -> const char * { return xsimd::best_arch::name(); }

auto arch_available(const char *name) -> int {
    if (name == nullptr)
        return -1;
    return std::string_view{name} == xsimd::best_arch::name() ? 1 : -1;
}

} // namespace treeweave::capi
