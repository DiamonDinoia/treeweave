#ifndef TREEWEAVE_DETAIL_TOL_KIND_HPP
#define TREEWEAVE_DETAIL_TOL_KIND_HPP

#include <cstddef>
#include <cstdint>

namespace treeweave {

/// Tolerance interpretation for the tree's adaptive refinement.
// BEGIN DOCS_TOL_KIND
enum class TolKind : std::uint8_t {
    RelativeTail = 0, ///< relative tail-coefficient estimate (1D only)
    AbsoluteTail = 1, ///< absolute tail-coefficient estimate (1D only)
    RelativeMax  = 2, ///< sample-based, max-abs relative error
    AbsoluteMax  = 3, ///< sample-based, max-abs absolute error
    RelativeL2   = 4, ///< sample-based, L2 relative error
    AbsoluteL2   = 5, ///< sample-based, L2 absolute error
};
// END DOCS_TOL_KIND

namespace detail {

/// Default leaf degree: 7 wins or ties every (arch, dtype, input_dim) cell,
/// and is spill-free in the wide SIMD cells. The C ABI bakes it into every
/// generated shape, so it is fixed there, not CPU-selected.
inline constexpr std::size_t kDefaultDegree = 7;

/// Internal fit-time configuration; mirror of `treeweave::options` plus the
/// shape parameters resolved at the public API boundary.
struct TreeInput {
    int    input_dim  = 0;
    int    output_dim = 1;
    int    degree     = static_cast<int>(kDefaultDegree);
    double tol        = 0.0;
    int    max_depth  = 50;
    // Already-resolved concrete budget: >0 caps leaf storage at that many
    // MiB, 0 disables. The public `options` auto sentinel (<0) is resolved to
    // a dimension-scaled value at the API boundary (see make_input), so a
    // negative value never reaches the paneler.
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
};

/// Sample-grid resolution per axis used by sample-based tolerance kinds
/// (`RelativeMax`, `AbsoluteMax`, `RelativeL2`, `AbsoluteL2`). 8 samples
/// per axis is dense enough to expose ringing from a degree-8 fit while
/// keeping ND fit cost bounded (8^Dim per panel).
inline constexpr int kFitSamplesPerDim = 8;
} // namespace detail

} // namespace treeweave

#endif // TREEWEAVE_DETAIL_TOL_KIND_HPP
