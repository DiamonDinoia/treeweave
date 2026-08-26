#ifndef TREEWEAVE_DETAIL_KERNELS_HPP
#define TREEWEAVE_DETAIL_KERNELS_HPP

// Per-leaf polynomial eval kernels over POD views, plus the header-only
// kernel policy `InlineKernels`. Header-only and arch-generic: a TU compiles
// these at its own -march, so the same code serves the header-only inline
// path and the per-ISA kernel TUs of the multi-arch C ABI. Each kernel body
// is the exact call the matching `FuncEval`/`FuncEvalND` member forwards to,
// so a view-based call and a member call produce identical code.
//
// ---------------------------------------------------------------------------
// Maintainer guide — adding a function
//
// A NON-vectorized function (fit logic, tree build, glue, error paths):
//   write it in the ordinary headers (function.hpp etc.) and call it
//   directly. It compiles once, at baseline, inside whatever TU calls it.
//   Register nothing — not here, not in kernels_arch.cpp, not in CMake.
//
// A VECTORIZED hot-loop kernel (compiled once per ISA rung):
//   1. Add the free kernel below: `static`, over POD views only, with a
//      `class Tag` linkage-anchor parameter if it instantiates polyfit
//      batch templates (see `leaf_run_aos`).
//   2. Add a `static` TREEWEAVE_ALWAYS_INLINE forward with the same name to
//      `InlineKernels` (the single-arch / header-only path).
//   3. Add a matching function-pointer member to each `KernelSet`
//      (specialization) whose shape uses the kernel.
//   4. In src/capi/kernels_arch.cpp: add an anonymous-namespace `k_<name>`
//      wrapper and wire its address into `make_kernels_for` (marked there).
//   5. Call it from the pipeline as `k.<name>(...)` through the duck-typed
//      kernel policy `const K &` — never call the free kernel directly from
//      code that compiles at baseline.
// The check_rung_symbols ctest (TREEWEAVE_VERIFY_RUNGS in release builds)
// fails the build when a missed step leaks one rung's code into a symbol
// the other rungs share.
// ---------------------------------------------------------------------------

#include <array>
#include <cstddef>
#include <cstdint>

#include <polyfit/polyeval.hpp>

#include <treeweave/detail/compiler_macros.hpp>
#include <treeweave/detail/quantize.hpp>

namespace treeweave::detail {

/// POD view of one leaf's 1D polynomial: `NC` Horner-order coefficients plus
/// the affine map to the fit domain. Valid while the owning tree is alive.
template <class T, std::size_t NC>
struct Leaf1D {
    const T *coeffs        = nullptr;
    T        inv_span      = T(0);
    T        sum_endpoints = T(0);
    bool     identity      = true;
};

/// POD view of one leaf's ND polynomial: flat `layout_right` coefficients in
/// `FuncEvalND` order (`NC^IN x OUT`) plus the per-axis domain map.
template <class T, std::size_t IN, std::size_t NC>
struct LeafND {
    const T                          *coeffs = nullptr;
    poly_eval::domain_nd_view<T, IN>  dom{};
};

// --- 1D ---------------------------------------------------------------------
// Every kernel is `static`: internal linkage per TU, so a per-ISA kernel TU
// exports no weak symbol another rung could collide with. The internal-lambda
// / `Tag` template arguments push the same internal linkage down into the
// polyfit instantiations (see horner_nd_batch.hpp on `Tag`).

/// Evaluate the leaf polynomial at `n` points, `out[i] = p(xp[i])`.
template <class T, std::size_t NC>
static auto leaf_run(const Leaf1D<T, NC> &lv, const T *xp, T *out, std::size_t n) noexcept -> void {
    poly_eval::horner<NC, false, false, 0>(xp, out, n, lv.coeffs, NC, [&](const auto v) -> auto {
        if (lv.identity) return v;
        return polyfit::internal::helpers::mapFromDomainScalar(v, lv.inv_span, lv.sum_endpoints);
    });
}

/// Evaluate the leaf polynomial at one point.
template <class T, std::size_t NC>
[[nodiscard]] TREEWEAVE_ALWAYS_INLINE static auto leaf_eval_one(const Leaf1D<T, NC> &lv, const T x) noexcept -> T {
    const T xm =
        lv.identity ? x : polyfit::internal::helpers::mapFromDomainScalar(x, lv.inv_span, lv.sum_endpoints);
    return poly_eval::horner<NC>(xm, lv.coeffs, NC);
}

// --- ND ---------------------------------------------------------------------

/// AoS batch: `out[i] = p(xp[i])`. `Tag` is the linkage anchor forwarded to
/// `horner_nd_batch` (per-ISA TUs pass an internal-linkage tag).
template <class T, std::size_t IN, std::size_t NC, std::size_t OUT, class Tag = void>
static auto leaf_run_aos(const LeafND<T, IN, NC> &lv, const std::array<T, IN> *xp, std::array<T, OUT> *out,
                         std::size_t n) noexcept -> void {
    const auto md = poly_eval::make_coeffs_mdspan<NC, IN, OUT>(lv.coeffs, static_cast<int>(NC));
    poly_eval::horner_nd_batch<NC, OUT, poly_eval::ScalarKernel::Horner, 0, Tag>(md, lv.dom, xp, out, n);
}

/// SoA batch: component `d` of point `i` lands in `out[d][i]`.
template <class T, std::size_t IN, std::size_t NC, std::size_t OUT, class Tag = void>
static auto leaf_run_soa(const LeafND<T, IN, NC> &lv, const std::array<T, IN> *xp, const std::array<T *, OUT> &out,
                         std::size_t n) noexcept -> void {
    const auto md = poly_eval::make_coeffs_mdspan<NC, IN, OUT>(lv.coeffs, static_cast<int>(NC));
    poly_eval::horner_nd_batch_soa<NC, OUT, poly_eval::ScalarKernel::Horner, 0, Tag>(md, lv.dom, xp, out, n);
}

/// Single point; `OUT` is deduced from the out-parameter.
template <class T, std::size_t IN, std::size_t NC, std::size_t OUT>
TREEWEAVE_ALWAYS_INLINE static auto leaf_eval_one(const LeafND<T, IN, NC> &lv, const std::array<T, IN> &x,
                                                  std::array<T, OUT> &out) noexcept -> void {
    const auto md = poly_eval::make_coeffs_mdspan<NC, IN, OUT>(lv.coeffs, static_cast<int>(NC));
    const auto xm =
        lv.dom.identity ? x
                        : polyfit::internal::helpers::mapFromDomainArray(x, lv.dom.inv_span, lv.dom.sum_endpoints);
    out = poly_eval::horner<NC, true, std::array<T, OUT>>(xm, md, static_cast<int>(NC));
}

// --- Kernel policy ------------------------------------------------------------

/// Header-only kernel policy: static forwards to the free kernels above,
/// compiled at the including TU's -march. The multi-arch C ABI substitutes a
/// function-pointer table with the same member names, selected once per
/// handle; every `Function` pipeline call site is written against this
/// duck-typed interface and never names an ISA.
struct InlineKernels {
    template <class T>
    TREEWEAVE_ALWAYS_INLINE static auto partition(const QuantizeView<T, 1> &v, const T *xp, std::size_t n,
                                                  std::uint32_t *counts, std::uint32_t *perm_inv, T *xp_packed,
                                                  std::uint32_t ood_id) noexcept -> void {
        partition_1d_table(v, xp, n, counts, perm_inv, xp_packed, ood_id);
    }

    template <class T, std::size_t NC>
    TREEWEAVE_ALWAYS_INLINE static auto run(const Leaf1D<T, NC> &lv, const T *xp, T *out, std::size_t n) noexcept
        -> void {
        leaf_run(lv, xp, out, n);
    }

    template <class T, std::size_t NC>
    [[nodiscard]] TREEWEAVE_ALWAYS_INLINE static auto eval_one(const Leaf1D<T, NC> &lv, const T x) noexcept -> T {
        return leaf_eval_one(lv, x);
    }

    template <class T, std::size_t IN, std::size_t NC, std::size_t OUT>
    TREEWEAVE_ALWAYS_INLINE static auto run_aos(const LeafND<T, IN, NC> &lv, const std::array<T, IN> *xp,
                                                std::array<T, OUT> *out, std::size_t n) noexcept -> void {
        leaf_run_aos(lv, xp, out, n);
    }

    template <class T, std::size_t IN, std::size_t NC, std::size_t OUT>
    TREEWEAVE_ALWAYS_INLINE static auto run_soa(const LeafND<T, IN, NC> &lv, const std::array<T, IN> *xp,
                                                const std::array<T *, OUT> &out, std::size_t n) noexcept -> void {
        leaf_run_soa(lv, xp, out, n);
    }

    template <class T, std::size_t IN, std::size_t NC, std::size_t OUT>
    TREEWEAVE_ALWAYS_INLINE static auto eval_one(const LeafND<T, IN, NC> &lv, const std::array<T, IN> &x,
                                                 std::array<T, OUT> &out) noexcept -> void {
        leaf_eval_one(lv, x, out);
    }
};

// --- Function-pointer kernel policy ------------------------------------------

/// Function-pointer twin of `InlineKernels` for one (T, IN, NC, OUT) shape:
/// the same duck-typed member names, but each member is a pointer into a
/// per-ISA kernel TU. The multi-arch C ABI selects one set per handle via
/// `xsimd::dispatch` and threads it through the `Function` pipeline, which
/// itself compiles once at baseline.
template <class T, std::size_t IN, std::size_t NC, std::size_t OUT>
struct KernelSet {
    void (*run_aos)(const LeafND<T, IN, NC> &, const std::array<T, IN> *, std::array<T, OUT> *,
                    std::size_t) noexcept = nullptr;
    void (*run_soa)(const LeafND<T, IN, NC> &, const std::array<T, IN> *, const std::array<T *, OUT> &,
                    std::size_t) noexcept = nullptr;
    void (*eval_one)(const LeafND<T, IN, NC> &, const std::array<T, IN> &, std::array<T, OUT> &) noexcept = nullptr;
};

/// 1D input, multi-output: the ND run kernels (the input is tuple-like, so
/// the pipeline routes through `LeafND`) plus the 1D leaf-table bin sort.
template <class T, std::size_t NC, std::size_t OUT>
struct KernelSet<T, 1, NC, OUT> {
    void (*partition)(const QuantizeView<T, 1> &, const T *, std::size_t, std::uint32_t *, std::uint32_t *, T *,
                      std::uint32_t) noexcept = nullptr;
    void (*run_aos)(const LeafND<T, 1, NC> &, const std::array<T, 1> *, std::array<T, OUT> *,
                    std::size_t) noexcept = nullptr;
    void (*run_soa)(const LeafND<T, 1, NC> &, const std::array<T, 1> *, const std::array<T *, OUT> &,
                    std::size_t) noexcept = nullptr;
    void (*eval_one)(const LeafND<T, 1, NC> &, const std::array<T, 1> &, std::array<T, OUT> &) noexcept = nullptr;
};

/// Scalar 1D->1D shape: the bin sort plus the scalar-input `Leaf1D` kernels.
template <class T, std::size_t NC>
struct KernelSet<T, 1, NC, 1> {
    void (*partition)(const QuantizeView<T, 1> &, const T *, std::size_t, std::uint32_t *, std::uint32_t *, T *,
                      std::uint32_t) noexcept = nullptr;
    void (*run)(const Leaf1D<T, NC> &, const T *, T *, std::size_t) noexcept   = nullptr;
    T (*eval_one)(const Leaf1D<T, NC> &, T) noexcept                           = nullptr;
};

/// One external factory per (Arch, shape). Defined in src/capi/kernels_arch.cpp,
/// which the multi-arch build compiles once per ISA rung; `Arch` keys the
/// mangled name, so the rungs coexist and the baseline dispatcher picks one
/// at runtime.
template <class Arch, class T, std::size_t IN, std::size_t NC, std::size_t OUT>
auto make_kernels_for() noexcept -> KernelSet<T, IN, NC, OUT>;

} // namespace treeweave::detail

#endif // TREEWEAVE_DETAIL_KERNELS_HPP
