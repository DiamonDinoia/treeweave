#include <treeweave/treeweave.hpp>

#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <random>
#include <vector>

int main() {
    // BEGIN DOCS_MULTIDIM
    auto bump = [](std::array<double, 2> x) -> std::array<double, 1> {
        return {std::exp(-100.0 * (x[0] - 0.5) * (x[0] - 0.5) - (x[1] - 0.5) * (x[1] - 0.5))};
    };
    // Fit bump(x, y) on [0, 1]^2 syntax is fit(callback, lower_bound, upper_bound, tolerance).
    auto fn = treeweave::fit(bump, std::array{0.0, 0.0}, std::array{1.0, 1.0},
                             /*tol=*/1e-8);
    // Evaluate fn on one point; the result is a std::array<double, 1>.
    const std::array<double, 1> y = fn(std::array{0.4, 0.6});
    // END DOCS_MULTIDIM
    std::cout << "fn(0.4, 0.6) = " << y[0] << "\n";

    std::mt19937                           gen(1);
    std::uniform_real_distribution<double> d(0.001, 0.999);
    constexpr int                          N = 500'000;
    std::vector<std::array<double, 2>>     xs(N);
    for (auto &x : xs)
        x = {d(gen), d(gen)};

    const auto t0  = std::chrono::steady_clock::now();
    double     acc = 0.0;
    // Evaluate fn on random points and print throughput plus max error.
    for (auto &x : xs)
        acc += fn(x)[0];
    const auto   t1 = std::chrono::steady_clock::now();
    const double dt = std::chrono::duration<double>(t1 - t0).count();

    double mx = 0.0;
    for (auto &x : xs) {
        const double exact = bump(x)[0];
        if (std::abs(exact) > 1e-12)
            mx = std::max(mx, std::abs(fn(x)[0] - exact) / std::abs(exact));
    }
    std::cout << "MEvals/s: " << N / (dt * 1e6) << "\n";
    std::cout << "max relative error: " << mx << "\n";
    std::cout << "ignore: " << acc << "\n";
    return 0;
}
