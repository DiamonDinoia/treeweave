// kernels_arch.cpp: the per-ISA hot-loop kernel tables of the multi-arch C ABI.
//
// The multi-arch build compiles this TU once per ISA rung (its own -march);
// the rest of the C ABI — fit, tree build, pipeline glue — compiles once at
// baseline and calls in here through `KernelSet` function pointers. The only
// external symbols are the `make_kernels_for<xsimd::best_arch, …>`
// instantiations at the bottom: `best_arch` differs per rung, so their
// mangled names never collide. Everything else is internal: the wrappers sit
// in the anonymous namespace, and `ArchTag` pushes internal linkage down
// into the polyfit instantiations (see kernels.hpp / horner_nd_batch.hpp).

#include <array>
#include <cstddef>
#include <cstdint>

#include <xsimd/xsimd.hpp>

#include <treeweave/detail/arch_degree_table.hpp>
#include <treeweave/detail/kernels.hpp>
#include <treeweave/detail/quantize.hpp>

namespace treeweave::detail {
namespace {

/// TU-local linkage anchor for the polyfit ND-batch instantiations.
struct ArchTag {};

constexpr auto kDeg = static_cast<std::size_t>(treeweave::capi::chosen_degree);

// New vectorized kernel (step 4 of the guide in kernels.hpp): add its
// `k_<name>` wrapper below — anonymous namespace, POD-view parameters,
// forwarding to the free kernel with `ArchTag` where the kernel takes a
// linkage anchor — then add `&k_<name>` to the matching `make_kernels_for`
// branch after the wrappers.

template <class T>
auto k_partition(const QuantizeView<T, 1> &v, const T *xp, std::size_t n, std::uint32_t *counts,
                 std::uint32_t *perm_inv, T *xp_packed, std::uint32_t ood_id) noexcept -> void {
    partition_1d_table(v, xp, n, counts, perm_inv, xp_packed, ood_id);
}

template <class T>
auto k_run(const Leaf1D<T, kDeg> &lv, const T *xp, T *out, std::size_t n) noexcept -> void {
    leaf_run(lv, xp, out, n);
}

template <class T>
auto k_eval_one(const Leaf1D<T, kDeg> &lv, const T x) noexcept -> T {
    return leaf_eval_one(lv, x);
}

template <class T, std::size_t IN, std::size_t OUT>
auto k_run_aos(const LeafND<T, IN, kDeg> &lv, const std::array<T, IN> *xp, std::array<T, OUT> *out,
               std::size_t n) noexcept -> void {
    leaf_run_aos<T, IN, kDeg, OUT, ArchTag>(lv, xp, out, n);
}

template <class T, std::size_t IN, std::size_t OUT>
auto k_run_soa(const LeafND<T, IN, kDeg> &lv, const std::array<T, IN> *xp, const std::array<T *, OUT> &out,
               std::size_t n) noexcept -> void {
    leaf_run_soa<T, IN, kDeg, OUT, ArchTag>(lv, xp, out, n);
}

template <class T, std::size_t IN, std::size_t OUT>
auto k_eval_one_nd(const LeafND<T, IN, kDeg> &lv, const std::array<T, IN> &x, std::array<T, OUT> &out) noexcept
    -> void {
    leaf_eval_one(lv, x, out);
}

} // namespace

// One branch per KernelSet specialization; the braced list wires one wrapper
// address per member, in member order (see kernels.hpp).
template <class Arch, class T, std::size_t IN, std::size_t NC, std::size_t OUT>
auto make_kernels_for() noexcept -> KernelSet<T, IN, NC, OUT> {
    static_assert(NC == kDeg, "kernel TUs instantiate the baked degree only (arch_degree_table.hpp)");
    if constexpr (IN == 1 && OUT == 1) {
        return {&k_partition<T>, &k_run<T>, &k_eval_one<T>};
    } else if constexpr (IN == 1) {
        return {&k_partition<T>, &k_run_aos<T, 1, OUT>, &k_run_soa<T, 1, OUT>, &k_eval_one_nd<T, 1, OUT>};
    } else {
        return {&k_run_aos<T, IN, OUT>, &k_run_soa<T, IN, OUT>, &k_eval_one_nd<T, IN, OUT>};
    }
}

// The supported shape set: 2 dtypes x {1,2,3}x{1,2,3}. Single source of
// truth: the foreach lists in cmake/treeweave_c_dispatch.cmake — keep this
// list and arch_dispatch.cpp's TREEWEAVE_SELECT_KERNELS list in sync with it.
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage) — explicit-instantiation list.
#define TREEWEAVE_KERNELS_FOR(T, IN, OUT)                                                                          \
    template auto make_kernels_for<xsimd::best_arch, T, IN, kDeg, OUT>() noexcept -> KernelSet<T, IN, kDeg, OUT>;

TREEWEAVE_KERNELS_FOR(double, 1, 1)
TREEWEAVE_KERNELS_FOR(double, 1, 2)
TREEWEAVE_KERNELS_FOR(double, 1, 3)
TREEWEAVE_KERNELS_FOR(double, 2, 1)
TREEWEAVE_KERNELS_FOR(double, 2, 2)
TREEWEAVE_KERNELS_FOR(double, 2, 3)
TREEWEAVE_KERNELS_FOR(double, 3, 1)
TREEWEAVE_KERNELS_FOR(double, 3, 2)
TREEWEAVE_KERNELS_FOR(double, 3, 3)
TREEWEAVE_KERNELS_FOR(float, 1, 1)
TREEWEAVE_KERNELS_FOR(float, 1, 2)
TREEWEAVE_KERNELS_FOR(float, 1, 3)
TREEWEAVE_KERNELS_FOR(float, 2, 1)
TREEWEAVE_KERNELS_FOR(float, 2, 2)
TREEWEAVE_KERNELS_FOR(float, 2, 3)
TREEWEAVE_KERNELS_FOR(float, 3, 1)
TREEWEAVE_KERNELS_FOR(float, 3, 2)
TREEWEAVE_KERNELS_FOR(float, 3, 3)

#undef TREEWEAVE_KERNELS_FOR

} // namespace treeweave::detail
