#ifndef TREEWEAVE_DETAIL_TOL_KIND_HPP
#define TREEWEAVE_DETAIL_TOL_KIND_HPP

#include <cstdint>
#include <optional>
#include <string>

namespace treeweave {

/// Tolerance interpretation for the tree's adaptive refinement.
/// The default, RelativeMax, is 0, so a zeroed options struct selects it.
enum class TolKind : std::uint8_t {
    RelativeMax  = 0, ///< sample-based, max|p - f| <= tol * max|f| sampled over the domain; all-zero f needs p == f
    AbsoluteMax  = 1, ///< sample-based, max-abs absolute error
    RelativeL2   = 2, ///< sample-based, L2 relative error
    AbsoluteL2   = 3, ///< sample-based, L2 absolute error
    RelativePointwise = 4, ///< sample-based, |p - f| <= tol * |f| at every sample; a zero sample needs p == f
    RelativeTail = 5, ///< tail-coefficient estimate over max_k |c_k| of the panel; all-zero panel converges (1D only).
                      ///< Exception: with a fit of 1 or 2 coefficients the tail is the whole polynomial, so this
                      ///< falls back to sampled RelativeMax scaled by the running domain-wide max|f|.
    AbsoluteTail = 6, ///< absolute tail-coefficient estimate (1D only)
};

namespace detail {
/// Internal fit-time configuration; mirror of `treeweave::options` plus the
/// shape parameters resolved at the public API boundary.
struct TreeInput {
    int    input_dim  = 0;
    int    output_dim = 1;
    int    degree     = 7;
    double tol        = 0.0;
    int    max_depth  = 50;
    // Resolved budget: >0 caps leaf storage at that many MiB, <0 disables.
    // `init_defaults` resolves the public auto value (0) at the API boundary.
    int  max_memory_mib         = 4;
    bool allow_max_depth_leaves = false;
    /// Force BFS to refine every panel to at least this depth before
    /// the per-panel tolerance test is allowed to mark a node as a leaf.
    /// Useful for driving the leaf-table fast path: a uniformly-refined
    /// tree of depth D in input_dim K builds a 2^(K*D)-entry quantize
    /// table at construction (see PolyTree::leaf_table_), turning the
    /// eval-time leaf lookup into one SIMD quantize + one u32 load.
    /// Default 0: no forcing, tol-based refinement only.
    int     min_uniform_depth = 0;
    TolKind tol_kind          = TolKind::RelativeMax;
    /// Fit file: load the fit from it when its header matches, else fit and
    /// save to it. `std::nullopt`: no file.
    std::optional<std::string> filename = std::nullopt;
    /// Running max|f| over every sample of this fit, the `RelativeMax` normaliser. The fit is serial: no atomic.
    mutable double max_abs_f = 0.0;
};

/// Sample-grid resolution per axis used by sample-based tolerance kinds
/// (`RelativeMax`, `AbsoluteMax`, `RelativeL2`, `AbsoluteL2`). 8 samples
/// per axis is dense enough to expose ringing from a degree-8 fit while
/// keeping ND fit cost bounded (8^Dim per panel).
inline constexpr int kFitSamplesPerDim = 8;
} // namespace detail

} // namespace treeweave

#endif // TREEWEAVE_DETAIL_TOL_KIND_HPP
