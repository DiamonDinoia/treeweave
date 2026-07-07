// hankel.cpp — treeweave fit of Hankel H0^(1)(x) = J0(x) + iY0(x).
// Fits a real 2-output function on [1, 30) to absolute tolerance 1e-8,
// then checks accuracy against the library on 1 M random points.
// Domain starts at x = 1 because Y0 diverges at the origin.

#include <treeweave/treeweave.hpp>

#include <array>
#include <cmath>
#include <iostream>
#include <random>
#include <vector>

namespace {
// Order-0 Bessel functions J0/Y0, routed to whatever the platform provides.
#if defined(__cpp_lib_math_special_functions)
inline double bessel_j0(double x) { return std::cyl_bessel_j(0.0, x); }
inline double bessel_y0(double x) { return std::cyl_neumann(0.0, x); }
#elif defined(_MSC_VER)
inline double bessel_j0(double x) { return ::_j0(x); }
inline double bessel_y0(double x) { return ::_y0(x); }
#else
inline double bessel_j0(double x) { return ::j0(x); }
inline double bessel_y0(double x) { return ::y0(x); }
#endif
} // namespace

int main() {
    // Hankel H0^(1) packed as a real 2-vector: {J0(x), Y0(x)}.
    auto hankel0 = [](std::array<double, 1> x) -> std::array<double, 2> { return {bessel_j0(x[0]), bessel_y0(x[0])}; };

    // J0/Y0 oscillate through zero — fit to absolute tolerance.
    constexpr double   tol = 1e-8;
    treeweave::options opts;
    opts.tol_kind = treeweave::TolKind::AbsoluteMax;
    std::array<double, 1> a{1.0};
    std::array<double, 1> b{30.0};
    // Fit Hankel H0^(1)(x) on [1, 30] syntax is fit(callback, lower_bound, upper_bound, tolerance, options).
    auto fn = treeweave::fit(hankel0, a, b, tol, opts);

    // Check accuracy on random points in [1, 30).
    std::mt19937                           gen(7);
    std::uniform_real_distribution<double> dx(a[0], b[0]);
    constexpr int                          n_pts = 1'000'000;
    std::array<double, 2>                  max_abs{0.0, 0.0};
    // Evaluate fn on random points and print the maximum absolute error.
    for (int i = 0; i < n_pts; ++i) {
        const double x      = dx(gen);
        const auto   approx = fn(std::array<double, 1>{x});
        const auto   exact  = hankel0(std::array<double, 1>{x});
        for (std::size_t k = 0; k < 2; ++k)
            max_abs[k] = std::max(max_abs[k], std::abs(approx[k] - exact[k]));
    }

    std::cout << "Hankel H0^(1) fit on [" << a[0] << ", " << b[0] << "), abs tol " << tol << "\n"
              << "  max abs err  Re=J0: " << max_abs[0] << "   Im=Y0: " << max_abs[1] << "\n";

    return (max_abs[0] < 100 * tol && max_abs[1] < 100 * tol) ? 0 : 1;
}
