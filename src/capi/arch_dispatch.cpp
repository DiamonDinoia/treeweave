// arch_dispatch.cpp: runtime multi-architecture kernel selection.
//
// Selected by CMake when TREEWEAVE_C_MULTIARCH is ON. Compiled at the family
// baseline -march; emits no SIMD itself, only a CPU-feature probe. Fit, tree
// build and pipeline glue compile once at that baseline; the per-ISA choice
// happens in `select_kernels`, which returns the `KernelSet` of the widest
// host-supported rung, or the TREEWEAVE_FORCE_ARCH override. Uses
// available_architectures().has (not Arch::available(), which is
// constexpr-true and would SIGILL non-AVX512 hosts). The ladder itself is
// generated from the RUNG_TABLE in cmake/treeweave_c_dispatch.cmake.

#include <cstdlib>
#include <string_view>

#include <xsimd/xsimd.hpp>

#include <treeweave_dispatch_ladder.hpp> // generated: dispatch_arch_list
#include <treeweave_shapes.hpp>          // generated: TREEWEAVE_SHAPES

#include <treeweave/detail/c_binding.hpp>

namespace treeweave::capi {
namespace {

/// `xsimd::dispatch` functor: builds the matched rung's kernel table.
/// `make_kernels_for` is noexcept and only fills a struct with function
/// pointers, so running it inside the dispatch is safe.
template <class T, std::size_t IN, std::size_t NC, std::size_t OUT>
struct SelectKernelsFn {
    template <class Arch>
    auto operator()(Arch /*tag*/) const noexcept -> detail::KernelSet<T, IN, NC, OUT> {
        return detail::make_kernels_for<Arch, T, IN, NC, OUT>();
    }
};

/// Call `f.template operator()<Arch>()` once per rung of `dispatch_arch_list`,
/// widest first. The fold over the arch list is the only way to iterate it.
template <class F>
void for_each_ladder_arch(F &&f) {
    [&]<class... Arch>(xsimd::arch_list<Arch...> /*tag*/) -> void {
        (f.template operator()<Arch>(), ...);
    }(dispatch_arch_list{});
}

/// Testing-only: if TREEWEAVE_FORCE_ARCH names a supported ladder arch, fill
/// `out` with its kernel table so one capable host can exercise every
/// fallback. Unset / unknown / unsupported → false (caller falls back to
/// xsimd::dispatch).
template <class T, std::size_t IN, std::size_t NC, std::size_t OUT>
auto force_select(std::string_view want, detail::KernelSet<T, IN, NC, OUT> &out) -> bool {
    const auto archs = xsimd::available_architectures();
    bool       found = false;
    for_each_ladder_arch([&]<class Arch>() -> void {
        if (!found && want == Arch::name() && archs.has(Arch{})) {
            out   = detail::make_kernels_for<Arch, T, IN, NC, OUT>();
            found = true;
        }
    });
    return found;
}

} // namespace

template <class T, std::size_t IN, std::size_t NC, std::size_t OUT>
auto select_kernels() -> detail::KernelSet<T, IN, NC, OUT> {
    // NOLINTNEXTLINE(concurrency-mt-unsafe) — read-only env probe, set before any call.
    if (const char *want = std::getenv("TREEWEAVE_FORCE_ARCH")) {
        detail::KernelSet<T, IN, NC, OUT> forced{};
        if (force_select<T, IN, NC, OUT>(want, forced)) {
            return forced;
        }
    }
    return xsimd::dispatch<dispatch_arch_list>(SelectKernelsFn<T, IN, NC, OUT>{})();
}

// The shapes make_eval_impl can request, from the generated shape table.
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage) — explicit-instantiation list.
#define TREEWEAVE_SELECT_KERNELS(T, IN, OUT)                                                                           \
    template auto select_kernels<T, IN, detail::kDefaultDegree, OUT>()                                                 \
        -> detail::KernelSet<T, IN, detail::kDefaultDegree, OUT>;

TREEWEAVE_SHAPES(TREEWEAVE_SELECT_KERNELS)

#undef TREEWEAVE_SELECT_KERNELS

// Introspection. The name comes from the rung's own object, so a name the
// caller did not select means the linker resolved a rung's kernels to another
// rung's code — the COFF duplicate-COMDAT failure, caught at runtime.
auto active_arch() -> const char * { return select_kernels<double, 1, detail::kDefaultDegree, 1>().arch; }

auto arch_available(const char *name) -> int {
    if (name == nullptr)
        return -1;
    const auto             archs = xsimd::available_architectures();
    const std::string_view want{name};
    int                    state = -1;
    for_each_ladder_arch([&]<class Arch>() -> void {
        if (state == -1 && want == Arch::name())
            state = archs.has(Arch{}) ? 1 : 0;
    });
    return state;
}

} // namespace treeweave::capi
