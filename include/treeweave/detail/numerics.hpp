#ifndef TREEWEAVE_DETAIL_NUMERICS_HPP
#define TREEWEAVE_DETAIL_NUMERICS_HPP

#include <algorithm>
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

/// True when the leading-coefficient tail estimate of `polyfit` exceeds
/// `tol` for the given relative/absolute kind. 1D-only — the tail estimate
/// is read off the first/last Chebyshev coefficients, which generalise
/// poorly to ND. Sample-based kinds dispatch to `sample_error_exceeds_tol`.
template <class Polyfit>
auto tail_error_exceeds_tol(double tol, const Polyfit &polyfit) -> bool {
    constexpr std::size_t input_dim  = value_dim_v<typename Polyfit::InputType>;
    constexpr std::size_t output_dim = value_dim_v<typename Polyfit::OutputType>;
    using T                          = poly_eval::detail::value_type_or_t<typename Polyfit::InputType>;
    // ND / array-output never reaches here: the sole caller (node.hpp) gates
    // this behind `if constexpr (kTailErrorSupported)`, which is true only for
    // scalar→scalar fits. The static_asserts document that contract and turn
    // any stray ND instantiation into a compile error rather than dead code.
    static_assert(input_dim == 1, "tail_error check is only implemented for 1D scalar input; "
                                  "use a sample-based tol_type for array-valued or ND fits");
    static_assert(output_dim == 1, "tail_error only implemented for single output in 1D");

    // Tail tolerance is absolute today; RelativeTail scaling was never implemented.
    T maxcoeff{0.0};

    // A one-coefficient fit is a constant, so its single coefficient is the
    // whole tail and `coeffs[1]` would read past the buffer. `.data()` keeps the
    // array extent out of the type: GCC folds the identical instantiations of
    // this body across degrees and then charges one degree's extent to another.
    constexpr std::size_t n_tail = std::min<std::size_t>(2, Polyfit::NCOEFFS);

    const auto *coeffs = polyfit.coeffs().data();
    for (std::size_t i = 0; i < n_tail; ++i)
        maxcoeff = std::max(std::abs(coeffs[i]), maxcoeff);

    return static_cast<double>(maxcoeff) > tol;
}

/// True when the maximum/L2 error of `polyfit` measured on a uniform
/// `n_sample_1d`-per-axis grid exceeds `tol`. The chosen `tol_type`
/// selects between max-abs vs. L2 and relative vs. absolute.
template <class Func, class Polyfit>
inline auto sample_error_exceeds_tol(int n_sample_1d, TolKind tol_type, double tol,
                                     const typename Polyfit::InputType &center_in,
                                     const typename Polyfit::InputType &half_length_in, const Func &func,
                                     const Polyfit &polyfit) -> bool {
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

    double max_abs_err{0.0};
    double max_rel_err{0.0};
    double abs_err_l2{0.0};
    double direct_sum{0.0};
    for (std::size_t linear_index = 0; linear_index < n_samples; ++linear_index) {
        Value<T, input_dim> sample_point;
        std::size_t         curr_index = linear_index;

        for (std::size_t dim = 0; dim < input_dim; ++dim) {
            const T dx = T{2} * half_len[dim] / static_cast<T>(n_sample_1d_sz);
            sample_point[dim] =
                center[dim] - half_len[dim] + dx / T{2} + dx * static_cast<T>(curr_index % n_sample_1d_sz);
            curr_index /= n_sample_1d_sz;
        }

        Value<T, output_dim> actual = func(sample_point);
        Value<T, output_dim> approx = polyfit(sample_point);

        for (std::size_t i = 0; i < output_dim; ++i) {
            const double abs_err = std::abs(static_cast<double>(approx[i]) - static_cast<double>(actual[i]));
            max_abs_err          = std::max(max_abs_err, abs_err);
            if (actual[i] != T{0})
                max_rel_err = std::max(max_rel_err, std::abs(abs_err / static_cast<double>(actual[i])));
            abs_err_l2 += powi<2>(abs_err);
            direct_sum += powi<2>(static_cast<double>(actual[i]));
        }
    }

    switch (tol_type) {
    case TolKind::RelativeL2:
        return std::sqrt(abs_err_l2 / direct_sum) > tol;
    case TolKind::AbsoluteL2:
        return std::sqrt(abs_err_l2) / static_cast<double>(n_samples * output_dim) > tol;
    case TolKind::RelativeMax:
        return max_rel_err > tol;
    case TolKind::AbsoluteMax:
        return max_abs_err > tol;
    default:
        throw std::runtime_error("Treeweave fit error: unknown tolerance type for sampling");
    }
}

} // namespace treeweave::detail

#endif // TREEWEAVE_DETAIL_NUMERICS_HPP
