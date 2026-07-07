#include <treeweave/treeweave.hpp>

#include <cmath>
#include <iostream>

int main() {
    // sin(50 x) / x on [0.01, 5) — oscillatory and nearly singular near 0.
    // A low max_depth forces the fit to hit the ceiling: `MaxDepthExceeded`
    // then carries the offending panel [a, b) so the caller can identify
    // the region responsible.
    auto f = [](double x) { return std::sin(50.0 * x) / x; };

    try {
        // Fit f(x) on [0.01, 5] syntax is fit(callback, lower_bound, upper_bound, tolerance, options).
        auto fn = treeweave::fit(f, 0.01, 5.0, /*tol=*/1e-8, treeweave::options{.max_depth = 4});
        (void)fn;
        std::cerr << "expected MaxDepthExceeded but fit succeeded\n";
        return 1;
    } catch (const treeweave::MaxDepthExceeded &e) {
        std::cout << "caught MaxDepthExceeded:\n"
                  << "  depth = " << e.depth() << "\n"
                  << "  offending panel = [" << e.a()[0] << ", " << e.b()[0] << ")\n"
                  << "  what()  = " << e.what() << "\n";
    }

    // Re-fit with the default max_depth — converges now.
    // Fit f(x) on [0.01, 5] syntax is fit(callback, lower_bound, upper_bound, tolerance).
    auto fn = treeweave::fit(f, 0.01, 5.0, /*tol=*/1e-8);
    // Evaluate fn on (1.0) and print the result.
    std::cout << "converged fit at x=1.0: fn=" << fn(1.0) << "  exact=" << f(1.0) << "\n";
    return 0;
}
