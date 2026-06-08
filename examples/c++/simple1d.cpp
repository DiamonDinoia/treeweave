#include <treeweave/treeweave.hpp>

#include <chrono>
#include <cmath>
#include <iostream>
#include <random>
#include <vector>

int main() {
    auto runge = [](double x) { return 1.0 / (1.0 + 25.0 * x * x); };
    auto fn    = treeweave::fit(runge, -1.0, 1.0, /*tol=*/1e-10);

    std::mt19937                           gen(1);
    std::uniform_real_distribution<double> d(-0.99, 0.99);
    constexpr int                          N = 1'000'000;
    std::vector<double>                    xs(N);
    for (auto &x : xs)
        x = d(gen);

    const auto t0  = std::chrono::steady_clock::now();
    double     acc = 0.0;
    for (double x : xs)
        acc += fn(x);
    const auto   t1 = std::chrono::steady_clock::now();
    const double dt = std::chrono::duration<double>(t1 - t0).count();

    double mx = 0.0;
    for (double x : xs)
        mx = std::max(mx, std::abs(fn(x) - runge(x)) / std::abs(runge(x)));

    std::cout << "MEvals/s: " << N / (dt * 1e6) << "\n";
    std::cout << "max relative error: " << mx << "\n";
    std::cout << "ignore: " << acc << "\n";
    return 0;
}
