// kernels_arch.cpp: the per-ISA hot-loop kernel tables of the multi-arch C ABI.
//
// The multi-arch build compiles this TU once per ISA rung (its own -march);
// the rest of the C ABI — fit, tree build, pipeline glue — compiles once at
// baseline and calls in here through `KernelSet` function pointers. The
// `make_kernels_for<xsimd::best_arch, …>` instantiations at the bottom are the
// intended exports: `best_arch` differs per rung, so their mangled names never
// collide. The wrappers sit in the anonymous namespace, and `ArchTag` pushes
// internal linkage down into the polyfit instantiations (see kernels.hpp /
// horner_nd_batch.hpp).
//
// That leaves nothing else external on ELF only, where hidden visibility
// applies. COFF has none, so a Windows rung also exports every inline
// function, template instantiation and RTTI record it touches; the build
// renames those per rung afterwards (cmake/treeweave_c_dispatch.cmake).

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include <xsimd/xsimd.hpp>

#include <treeweave_shapes.hpp> // generated: TREEWEAVE_SHAPES

#include <treeweave/detail/kernels.hpp>
#include <treeweave/detail/quantize.hpp>
#include <treeweave/detail/tol_kind.hpp> // kDefaultDegree

// The rung's own ISA flags must select the arch the RUNG_TABLE claims for it,
// or two rungs compile the same code under different names and the ladder has
// a silent hole. CMake passes the table's arch as TREEWEAVE_RUNG_ARCH.
#ifdef TREEWEAVE_RUNG_ARCH
static_assert(std::is_same_v<xsimd::best_arch, TREEWEAVE_RUNG_ARCH>,
              "this rung's ISA flags do not select the arch its RUNG_TABLE row names");
#endif

namespace treeweave::detail {
namespace {

/// TU-local linkage anchor for the polyfit ND-batch instantiations.
struct ArchTag {};

constexpr auto kDeg = treeweave::detail::kDefaultDegree;

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
    static_assert(NC == kDeg, "kernel TUs instantiate the baked degree only (detail::kDefaultDegree)");
    // Arch::name() comes from this TU, compiled at this rung's ISA level, so the
    // name travels with the table and identifies which rung the caller reached.
    if constexpr (IN == 1 && OUT == 1) {
        return {&k_partition<T>, &k_run<T>, &k_eval_one<T>, Arch::name()};
    } else if constexpr (IN == 1) {
        return {&k_partition<T>, &k_run_aos<T, 1, OUT>, &k_run_soa<T, 1, OUT>, &k_eval_one_nd<T, 1, OUT>, Arch::name()};
    } else {
        return {&k_run_aos<T, IN, OUT>, &k_run_soa<T, IN, OUT>, &k_eval_one_nd<T, IN, OUT>, Arch::name()};
    }
}

// The supported shape set, from the generated shape table.
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage) — explicit-instantiation list.
#define TREEWEAVE_KERNELS_FOR(T, IN, OUT)                                                                          \
    template auto make_kernels_for<xsimd::best_arch, T, IN, kDeg, OUT>() noexcept -> KernelSet<T, IN, kDeg, OUT>;

TREEWEAVE_SHAPES(TREEWEAVE_KERNELS_FOR)

#undef TREEWEAVE_KERNELS_FOR

} // namespace treeweave::detail
