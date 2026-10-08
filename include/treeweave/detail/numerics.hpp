#ifndef TREEWEAVE_DETAIL_NUMERICS_HPP
#define TREEWEAVE_DETAIL_NUMERICS_HPP

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <tuple>

#include <polyfit/polyfit.hpp>

#include <treeweave/detail/tol_kind.hpp>
#include <treeweave/detail/value.hpp>

namespace treeweave::detail {

using index_t = std::size_t; ///< Type specifying indexing into flattened tree.

/// Compile-time integer power, `base ** EXP`, via repeated squaring. Used in
/// place of `std::pow` for ND fan-out counts (e.g. `Degree ** input_dim`)
/// where the exponent is a compile-time constant.
template <int EXP, typename T>
constexpr auto powi(T base) -> T {
    if constexpr (EXP == 0) {
        return T{1};
    } else if constexpr (EXP % 2 == 0) {
        const auto half = powi<EXP / 2>(base);
        return half * half;
    } else {
        return base * powi<EXP - 1>(base);
    }
}

/// Number of scalar components in a fit input/output type: 1 for arithmetic
/// scalars, `std::tuple_size_v<T>` for `std::array`-like tuples. Implemented
/// as a function template to keep `std::tuple_size_v<T>` out of the
/// instantiation when `T` is a scalar.
template <typename T>
constexpr auto value_dim() -> std::size_t {
    if constexpr (poly_eval::detail::hasTupleSize_v<T>)
        return std::tuple_size_v<T>;
    else
        return 1;
}

template <typename T>
inline constexpr std::size_t value_dim_v = value_dim<T>();

/// Geometric portion of Treeweave nodes.
template <typename T, std::size_t Dim>
struct Box {
    Value<T, Dim> center;
    Value<T, Dim> half_length;

    // Default construction: deque<Box>::resize default-inserts.
    Box() = default;
    Box(const auto &x, const auto &hl) : center{x}, half_length{hl} {}
};

/// True when the tail estimate of `polyfit` exceeds `tol` for the given
/// relative/absolute kind. 1D-only — the estimate is read off the two
/// highest-degree monomial coefficients on the canonical [-1, 1] domain
/// (`coeffs()` is Horner order, highest first), which generalise poorly to
/// ND. Sample-based kinds dispatch to `sample_error_exceeds_tol`.
template <class Polyfit>
auto tail_error_exceeds_tol(TolKind tol_type, double tol, const Polyfit &polyfit) -> bool {
    constexpr std::size_t input_dim  = value_dim_v<typename Polyfit::InputType>;
    constexpr std::size_t output_dim = value_dim_v<typename Polyfit::OutputType>;
    // ND / array-output never reaches here: the sole caller (node.hpp) gates
    // this behind `if constexpr (kTailErrorSupported)`, which is true only for
    // scalar→scalar fits. The static_asserts document that contract and turn
    // any stray ND instantiation into a compile error rather than dead code.
    static_assert(input_dim == 1, "tail_error check is only implemented for 1D scalar input; "
                                  "use a sample-based tol_type for array-valued or ND fits");
    static_assert(output_dim == 1, "tail_error only implemented for single output in 1D");

    // A one-coefficient fit is a constant, so its single coefficient is the
    // whole tail and `coeffs[1]` would read past the buffer. `.data()` keeps the
    // array extent out of the type: GCC folds the identical instantiations of
    // this body across degrees and then charges one degree's extent to another.
    constexpr std::size_t n_tail = std::min<std::size_t>(2, Polyfit::NCOEFFS);

    const auto *coeffs = polyfit.coeffs().data();
    double      tail{0.0}, scale{0.0};
    for (std::size_t i = 0; i < Polyfit::NCOEFFS; ++i) {
        const double c = std::abs(static_cast<double>(coeffs[i]));
        // std::max(x, NaN) keeps x, so test every coefficient before it can be dropped.
        if (!std::isfinite(c))
            return true;
        scale = std::max(scale, c);
        if (i < n_tail)
            tail = std::max(tail, c);
    }
    // AbsoluteTail compares with tol itself; RelativeTail scales tol by the largest coefficient.
    return tail > tol * (tol_type == TolKind::RelativeTail ? scale : 1.0);
}

/// True when the maximum/L2 error of `polyfit` measured on a uniform
/// `n_sample_1d`-per-axis open midpoint grid and on the closed grid through
/// the panel bounds exceeds `tol`. The chosen `tol_type` selects between
/// max-abs vs. L2 and relative vs. absolute. `max_abs_f` is the fit-wide
/// running max|f| that `RelativeMax` normalises by; a `RelativeMax` call
/// raises it, but only for a panel finite on both grids. Both `RelativeMax`
/// gates normalise by one shared panel scale, the max over the pre-existing
/// `max_abs_f` and both grid maxima, computed after both grids are sampled.
/// `lb_in`/`ub_in` carry the exact panel bounds: the closed grid probes them
/// verbatim, because `center ± half_len` can land a rounding step outside
/// the domain (e.g. [-0.1, 0.2]).
template <class Func, class Polyfit>
inline auto sample_error_exceeds_tol(int n_sample_1d, TolKind tol_type, double tol, double &max_abs_f,
                                     const typename Polyfit::InputType &center_in,
                                     const typename Polyfit::InputType &half_length_in,
                                     const typename Polyfit::InputType &lb_in, const typename Polyfit::InputType &ub_in,
                                     const Func &func, const Polyfit &polyfit) -> bool {
    constexpr std::size_t input_dim  = value_dim_v<typename Polyfit::InputType>;
    constexpr std::size_t output_dim = value_dim_v<typename Polyfit::OutputType>;
    // Sample in the fit's own value type so a `float` fit never silently
    // promotes its sample grid to double; the error metrics below still
    // accumulate in double via explicit casts.
    using T                          = poly_eval::detail::value_type_or_t<typename Polyfit::InputType>;
    const auto        n_sample_1d_sz = static_cast<std::size_t>(n_sample_1d);
    const std::size_t n_samples      = powi<static_cast<int>(input_dim)>(n_sample_1d_sz);
    const Value       half_len       = half_length_in;
    const Value       center         = center_in;
    const Value       lb             = lb_in;
    const Value       ub             = ub_in;

    // Per-grid accumulators, reset by `reset()` between the two gates. The
    // grids are two independent gates; the panel converges only if both
    // pass. A pooled norm would dilute the error: samples of one grid with a
    // small error would raise the count and hide a large error at the
    // samples of the other grid. The open-grid gate uses the same points,
    // count and norm as before, so its decision is bit-identical. The
    // closed-grid gate probes the vertices and the panel boundary, where
    // the open grid sees none.
    double max_abs_err{0.0};
    // Hypot-style scaled sums of squares: accumulate `ssq = Σ (x/scale)²`
    // against the running `scale = max|x|`, so no intermediate square
    // overflows at |x| > ~1e154 or underflows below ~1e-154, and the final
    // `scale * sqrt(ssq)` recovers the exact norm. Squaring raw values
    // (the previous form) let RelativeL2 produce inf/inf or 0/0 = NaN,
    // and `NaN > tol` is false, so a NaN error was accepted silently.
    double ssq_err{0.0}, scale_err{0.0};
    double ssq_f{0.0}, scale_f{0.0};
    // Scalar error samples accumulated in the current grid, times output_dim.
    std::size_t n_accum{0};
    // RelativePointwise: |err_i| <= tol * |f_i| at each sample i. A zero
    // reference sample converges iff its error is 0 (the RelativeMax
    // all-zero-reference contract, applied per sample).
    bool pointwise_ok{true};
    // Per-grid finite flag and max|f| local to each grid. The fit-wide
    // `max_abs_f` is committed only when both grids are finite, the same
    // commit main makes for its one grid there (see the reset calls below).
    std::array<bool, 2>   grid_finite{true, true};
    std::array<double, 2> grid_max_abs_f{0.0, 0.0};
    // Per-grid saved norm accumulators, so each grid runs once and both
    // gates still see their own private error/norm after the commit below.
    struct Accums {
        double      max_abs_err{0.0};
        double      ssq_err{0.0};
        double      scale_err{0.0};
        double      ssq_f{0.0};
        double      scale_f{0.0};
        std::size_t n_accum{0};
        bool        pointwise_ok{true};
    };
    std::array<Accums, 2> saved{};

    const auto accumulate = [&](const Value<T, input_dim> &sample_point, std::size_t grid) -> void {
        Value<T, output_dim> actual = func(sample_point);
        Value<T, output_dim> approx = polyfit(sample_point);
        for (std::size_t i = 0; i < output_dim; ++i) {
            const double abs_err = std::abs(static_cast<double>(approx[i]) - static_cast<double>(actual[i]));
            // std::max(x, NaN) keeps x and would drop a NaN, so record non-finite values in a flag.
            // abs_err is non-finite whenever approx or actual is.
            grid_finite[grid]    = grid_finite[grid] && std::isfinite(abs_err);
            max_abs_err          = std::max(max_abs_err, abs_err);
            grid_max_abs_f[grid] = std::max(grid_max_abs_f[grid], std::abs(static_cast<double>(actual[i])));
            // Rescale when a new max appears; the stored ssq stays finite.
            if (abs_err > scale_err) {
                const double r = scale_err / abs_err;
                ssq_err        = ssq_err * r * r + 1.0;
                scale_err      = abs_err;
            } else if (scale_err > 0.0) {
                const double r = abs_err / scale_err;
                ssq_err += r * r;
            }
            const double abs_f = std::abs(static_cast<double>(actual[i]));
            pointwise_ok       = pointwise_ok && (abs_err <= tol * abs_f);
            if (abs_f > scale_f) {
                const double r = scale_f / abs_f;
                ssq_f          = ssq_f * r * r + 1.0;
                scale_f        = abs_f;
            } else if (scale_f > 0.0) {
                const double r = abs_f / scale_f;
                ssq_f += r * r;
            }
            ++n_accum;
        }
    };

    // A panel check pays (8^d + 9^d) / 8^d f-evals: 2.13x for d=1, 2.27x for
    // d=2, 2.42x for d=3. Gate 2 must start from scratch (no dilution), so
    // `reset()` restores the norm accumulators; the per-grid finite flags
    // and maxima above stay until the final commit.
    const auto run_grid = [&](bool closed) -> void {
        const std::size_t grid     = closed ? 1u : 0u;
        const std::size_t pts_1d   = n_sample_1d_sz + (closed ? 1 : 0);
        const std::size_t n_points = closed ? powi<static_cast<int>(input_dim)>(pts_1d) : n_samples;
        for (std::size_t linear_index = 0; linear_index < n_points; ++linear_index) {
            Value<T, input_dim> sample_point;
            std::size_t         curr_index = linear_index;
            for (std::size_t dim = 0; dim < input_dim; ++dim) {
                const std::size_t i = curr_index % pts_1d;
                if (closed) {
                    // Exact bounds at the ends: index 0 is lb, index n is ub,
                    // the interior is clamped into [lb, ub]. Rounding of
                    // lb + i*(ub-lb)/n can otherwise land outside the domain
                    // (e.g. center-half_len on [-0.1, 0.2]).
                    const T span = (ub[dim] - lb[dim]) / static_cast<T>(n_sample_1d_sz);
                    if (i == 0)
                        sample_point[dim] = lb[dim];
                    else if (i == n_sample_1d_sz)
                        sample_point[dim] = ub[dim];
                    else
                        sample_point[dim] = std::clamp(lb[dim] + span * static_cast<T>(i), lb[dim], ub[dim]);
                } else {
                    const T dx        = T{2} * half_len[dim] / static_cast<T>(n_sample_1d_sz);
                    sample_point[dim] = center[dim] - half_len[dim] + dx / T{2} + dx * static_cast<T>(i);
                }
                curr_index /= pts_1d;
            }
            accumulate(sample_point, grid);
        }
    };
    const auto reset = [&]() -> void {
        max_abs_err  = 0.0;
        ssq_err      = 0.0;
        scale_err    = 0.0;
        ssq_f        = 0.0;
        scale_f      = 0.0;
        n_accum      = 0;
        pointwise_ok = true;
    };
    const auto save = [&](std::size_t grid) -> void {
        saved[grid].max_abs_err  = max_abs_err;
        saved[grid].ssq_err      = ssq_err;
        saved[grid].scale_err    = scale_err;
        saved[grid].ssq_f        = ssq_f;
        saved[grid].scale_f      = scale_f;
        saved[grid].n_accum      = n_accum;
        saved[grid].pointwise_ok = pointwise_ok;
    };
    const auto restore = [&](std::size_t grid) -> void {
        max_abs_err  = saved[grid].max_abs_err;
        ssq_err      = saved[grid].ssq_err;
        scale_err    = saved[grid].scale_err;
        ssq_f        = saved[grid].ssq_f;
        scale_f      = saved[grid].scale_f;
        n_accum      = saved[grid].n_accum;
        pointwise_ok = saved[grid].pointwise_ok;
    };
    const auto decide = [&](std::size_t grid, double shared_scale) -> bool {
        // A non-finite sample or error can never satisfy any check below
        // (`inf > tol*inf` is false): reject the panel outright.
        if (!grid_finite[grid])
            return true;

        switch (tol_type) {
        case TolKind::RelativeL2:
            // Sibling rule (RelativeMax): an all-zero reference converges iff
            // the error is 0. Either norm returns 0.0 exactly when its scale
            // is 0.
            if (scale_f == 0.0)
                return scale_err != 0.0;
            return (scale_err / scale_f) * std::sqrt(ssq_err / ssq_f) > tol;
        case TolKind::AbsoluteL2:
            return scale_err * std::sqrt(ssq_err) / static_cast<double>(n_accum) > tol;
        case TolKind::RelativeMax:
            // Only RelativeMax consumes the panel scale: the fit-wide
            // `max_abs_f` plus both grid maxima, passed in as
            // `shared_scale`. A finite rejected panel still updates
            // `max_abs_f` at the final commit; only a non-finite panel does
            // not. Running the decision before the closed grid existed mixed
            // scales: the open gate used `max_abs_f` plus its own maximum,
            // the closed gate added the closed maximum. One shared scale
            // (larger, so looser) makes the two gates comparable.
            return max_abs_err > tol * shared_scale;
        case TolKind::AbsoluteMax:
            return max_abs_err > tol;
        case TolKind::RelativePointwise:
            return !pointwise_ok;
        default:
            throw std::runtime_error("Treeweave fit error: unknown tolerance type for sampling");
        }
    };
    // Run each grid once. Save the open-grid accumulators before `reset()`
    // wipes them, then run the closed grid and save those too. Both grids
    // are then decided against one shared panel scale, max over the
    // pre-existing `max_abs_f` and both grid maxima. A larger scale makes
    // RelativeMax looser, so over-estimating the panel maximum would relax
    // the tolerance.
    run_grid(false);
    save(0);
    reset();
    run_grid(true);
    save(1);

    const double panel_scale = std::max({max_abs_f, grid_max_abs_f[0], grid_max_abs_f[1]});

    // Gate 1: the open grid probes the cell midpoints only.
    restore(0);
    const bool open_exceeds = decide(0, panel_scale);

    // Gate 2: the closed grid, both endpoints and the k-1 interior vertices
    // per axis, (k+1)^d points.
    restore(1);
    const bool closed_exceeds = decide(1, panel_scale);

    // Main raises max_abs_f unconditionally per panel with the panel's
    // finite max|f| (the non-finite early return skips the raise). Do the
    // same here across both gates, but only for RelativeMax, the sole
    // consumer of the running scale. A finite but rejected panel still
    // raises `max_abs_f`, so later panels keep the larger normaliser; a
    // non-finite panel does not.
    if (tol_type == TolKind::RelativeMax && grid_finite[0] && grid_finite[1])
        max_abs_f = std::max({max_abs_f, grid_max_abs_f[0], grid_max_abs_f[1]});

    return open_exceeds || closed_exceeds;
}

} // namespace treeweave::detail

#endif // TREEWEAVE_DETAIL_NUMERICS_HPP
