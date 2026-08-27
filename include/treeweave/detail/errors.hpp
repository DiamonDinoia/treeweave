#ifndef TREEWEAVE_DETAIL_ERRORS_HPP
#define TREEWEAVE_DETAIL_ERRORS_HPP

#include <cstddef>
#include <exception>
#include <string>
#include <vector>

namespace treeweave {

/// A panel where adaptive paneling failed to meet `tol` at the configured
/// `max_depth`. Reported either through `MaxDepthExceeded::panels()` (the
/// default throwing path) or through `Function::non_converged_panels()`
/// when `options.allow_max_depth_leaves == true`.
struct NonConvergedPanel {
    std::vector<double> a;     ///< Lower bound of the half-open panel.
    std::vector<double> b;     ///< Upper bound of the half-open panel.
    std::size_t         depth{}; ///< Tree depth at which convergence failed.
};

/// Thrown when adaptive paneling hits `options.max_depth` without converging.
/// Carries the offending panel as a half-open domain [a, b) and the depth,
/// so callers can identify the singular region — typically a latent
/// singularity in the function that sampling alone cannot resolve.
class MaxDepthExceeded : public std::exception {
  public:
    /// Construct from a list of unconverged panels (the aggregate path).
    /// `panels` must be non-empty; the first entry's bounds are mirrored into
    /// the legacy single-panel accessors `a()/b()` for backwards compatibility.
    explicit MaxDepthExceeded(std::vector<NonConvergedPanel> panels);

    [[nodiscard]] auto depth() const noexcept -> std::size_t { return depth_; }
    /// Lower bound(s) of the first unconverged panel [a, b). Legacy accessor;
    /// for the full list use `panels()`.
    [[nodiscard]] auto a() const noexcept -> const std::vector<double> & { return a_; }
    /// Upper bound(s) of the first unconverged panel [a, b).
    [[nodiscard]] auto b() const noexcept -> const std::vector<double> & { return b_; }
    /// Full list of unconverged panels gathered before the throw.
    [[nodiscard]] auto panels() const noexcept -> const std::vector<NonConvergedPanel> & { return panels_; }
    [[nodiscard]] auto what() const noexcept -> const char * override;

  private:
    std::size_t                    depth_ = 0;
    std::vector<double>            a_;
    std::vector<double>            b_;
    std::vector<NonConvergedPanel> panels_;
    std::string                    msg_ = "Treeweave fit error: tree depth exceeded max allowed input depth";
};

/// Thrown when adaptive paneling pushes accumulated leaf storage past
/// `options.max_memory_mib`. Carries the amount used, the budget, and the
/// offending half-open panel [a, b) — the same shape as `MaxDepthExceeded`
/// — so callers can either raise the budget or excise the singular region.
class MemoryBudgetExceeded : public std::exception {
  public:
    template <class VecC, class VecH>
    MemoryBudgetExceeded(std::size_t used_bytes, std::size_t budget_bytes, const VecC &center_in,
                         const VecH &half_length_in);

    [[nodiscard]] auto used_bytes() const noexcept -> std::size_t { return used_bytes_; }
    [[nodiscard]] auto budget_bytes() const noexcept -> std::size_t { return budget_bytes_; }
    [[nodiscard]] auto a() const noexcept -> const std::vector<double> & { return a_; }
    [[nodiscard]] auto b() const noexcept -> const std::vector<double> & { return b_; }
    [[nodiscard]] auto what() const noexcept -> const char * override;

  private:
    std::size_t         used_bytes_   = 0;
    std::size_t         budget_bytes_ = 0;
    std::vector<double> a_;
    std::vector<double> b_;
    std::string         msg_ = "Treeweave fit error: leaf-storage exceeded budget";
};

} // namespace treeweave

#include <sstream>
#include <utility>

namespace treeweave {

namespace detail {
/// Pretty-print a half-open panel `[a, b)` (1D) or its Cartesian-product
/// form `[[a0,b0) x [a1,b1) x ...]` (ND) into `os`. Shared between the
/// two exception types so the message format stays uniform.
inline auto format_domain(std::ostringstream &os, const std::vector<double> &a, const std::vector<double> &b) -> void {
    if (a.size() == 1) {
        os << "[" << a[0] << ", " << b[0] << ")";
        return;
    }
    os << "[";
    for (std::size_t i = 0; i < a.size(); ++i)
        os << (i ? " x " : "") << "[" << a[i] << ", " << b[i] << ")";
    os << "]";
}
} // namespace detail

inline MaxDepthExceeded::MaxDepthExceeded(std::vector<NonConvergedPanel> panels)
    : panels_(std::move(panels)) {
    if (!panels_.empty()) {
        depth_ = panels_.front().depth;
        a_     = panels_.front().a;
        b_     = panels_.front().b;
    }
    std::ostringstream os;
    os << "Treeweave fit error: tree depth exceeded max allowed input depth (" << depth_ << ") on " << panels_.size()
       << " panel" << (panels_.size() == 1 ? "" : "s") << "; first ";
    detail::format_domain(os, a_, b_);
    os << " — likely a singularity; subdivide manually, raise "
          "options.max_depth, or set options.allow_max_depth_leaves=true "
          "to accept best-effort leaves.";
    msg_ = os.str();
}

inline auto MaxDepthExceeded::what() const noexcept -> const char * { return msg_.c_str(); }

template <class VecC, class VecH>
MemoryBudgetExceeded::MemoryBudgetExceeded(std::size_t used_bytes, std::size_t budget_bytes,
                                           const VecC &center_in, const VecH &half_length_in)
    : used_bytes_(used_bytes), budget_bytes_(budget_bytes) {
    auto cit = center_in.begin();
    auto hit = half_length_in.begin();
    for (; cit != center_in.end() && hit != half_length_in.end(); ++cit, ++hit) {
        // Panel bounds are reported in double regardless of the fit's
        // value_type; cast explicitly so a float fit does not trip
        // -Wdouble-promotion under -Werror.
        a_.push_back(static_cast<double>(*cit - *hit));
        b_.push_back(static_cast<double>(*cit + *hit));
    }
    std::ostringstream os;
    os << "Treeweave fit error: leaf-storage exceeded budget ("
       << (static_cast<double>(used_bytes_) / (1024.0 * 1024.0)) << " MiB used vs "
       << (static_cast<double>(budget_bytes_) / (1024.0 * 1024.0)) << " MiB budget) on panel ";
    detail::format_domain(os, a_, b_);
    os << " — raise options.max_memory_mib (or set to 0 to disable), "
          "or restrict the fit domain to skip this region.";
    msg_ = os.str();
}

inline auto MemoryBudgetExceeded::what() const noexcept -> const char * { return msg_.c_str(); }

} // namespace treeweave

#endif // TREEWEAVE_DETAIL_ERRORS_HPP
