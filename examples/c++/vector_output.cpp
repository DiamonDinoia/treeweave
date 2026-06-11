#include <treeweave/treeweave.hpp>

#include <array>
#include <cmath>
#include <iostream>
#include <random>

int main() {
    // Stokeslet-diagonal-style 2 -> 2 vector-valued function on a shifted
    // box off the origin. Exercises the multi-output path of the fit.
    auto f = [](std::array<double, 2> x) -> std::array<double, 2> {
        const double r2     = x[0] * x[0] + x[1] * x[1];
        const double r      = std::sqrt(r2);
        const double inv_r  = 1.0 / r;
        const double inv_r3 = inv_r / r2;
        return {inv_r + x[0] * x[0] * inv_r3, inv_r + x[1] * x[1] * inv_r3};
    };

    constexpr double      tol = 1e-8;
    std::array<double, 2> a{0.2, 0.2};
    std::array<double, 2> b{1.5, 1.5};
    auto                  fn = treeweave::fit(f, a, b, tol);

    std::mt19937                           gen(42);
    std::uniform_real_distribution<double> dx(0.21, 1.49);
    std::uniform_real_distribution<double> dy(0.21, 1.49);
    std::array<double, 2>                  mx{0.0, 0.0};
    for (int i = 0; i < 5000; ++i) {
        std::array<double, 2> x{dx(gen), dy(gen)};
        const auto            exact  = f(x);
        const auto            approx = fn(x);
        for (std::size_t k = 0; k < 2; ++k)
            if (std::abs(exact[k]) > 1e-14)
                mx[k] = std::max(mx[k], std::abs((exact[k] - approx[k]) / exact[k]));
    }
    std::cout << "max rel err channel 0: " << mx[0] << "\n"
              << "max rel err channel 1: " << mx[1] << "\n"
              << "target tol: " << tol << "\n";
    return (mx[0] < 100 * tol && mx[1] < 100 * tol) ? 0 : 1;
}
