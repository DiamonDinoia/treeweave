// The smallest useful treeweave program: fit an expensive function once, then
// evaluate the polynomial approximation instead of the function.
//
// Every C++ install route under examples/quickstart/ compiles this same file,
// and tools/ci/install-test.sh runs all of them, so the quick-start snippet in
// the docs is code that CI proved works.

// BEGIN DOCS_PROGRAM
#include <treeweave/treeweave.hpp>

#include <cmath>
#include <cstdio>

int main() {
    // Stand-in for an expensive function: a 1000-term series, ~1 us per call.
    auto zeta = [](double s) {
        double y = 0.0;
        for (int k = 1; k <= 1000; ++k)
            y += std::pow(k, -s);
        return y;
    };

    // fit(callback, lower_bound, upper_bound, tolerance)
    const auto f = treeweave::fit(zeta, 2.0, 10.0, 1e-10);

    const double x   = 3.5;
    const double err = std::abs(f(x) - zeta(x)) / std::abs(zeta(x));
    std::printf("f(%g) = %.15g, relative error %.2e\n", x, f(x), err);
    return err < 1e-8 ? 0 : 1;
}
