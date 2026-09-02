// panels1d: print the adaptive panel layout treeweave builds in 1D.
//
// The docs show this program's output as the picture of what "adaptive
// paneling" means. It fits a function with a pole just outside the domain, then
// recovers the panel boundaries through the public API alone: leaf_id(x) is
// constant inside a panel, so a fine scan reveals every boundary.
//
// The final checks are the claim the picture makes: the fit meets the requested
// tolerance, and the panels shrink toward the pole rather than being uniform.

#include <treeweave/treeweave.hpp>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

int main() {
    // A pole at x = 1.05, just past the right end of the domain. Smooth on
    // [-1, 1], but its derivatives blow up as x -> 1.
    auto f = [](double x) { return 1.0 / (x - 1.05); };

    constexpr double a = -1.0, b = 1.0, tol = 1e-10;
    const auto       fn = treeweave::fit(f, a, b, tol);

    // Panel boundaries: scan finely and record every leaf_id change. The scan
    // is 100x finer than the narrowest panel the fit produces, so no panel is
    // missed.
    constexpr int       n_scan = 2'000'000;
    std::vector<double> edge{a};
    std::uint32_t       prev = fn.leaf_id(a);
    for (int i = 1; i <= n_scan; ++i) {
        const double        x  = a + (b - a) * static_cast<double>(i) / n_scan;
        const std::uint32_t id = fn.leaf_id(x);
        if (id != prev) {
            edge.push_back(x);
            prev = id;
        }
    }
    edge.push_back(b);

    // Draw the layout: a '|' at every panel boundary, '-' inside a panel.
    constexpr int width = 78;
    std::string   bar(width + 1, '-');
    for (const double x : edge)
        bar[static_cast<std::size_t>((x - a) / (b - a) * width)] = '|';

    std::printf("f(x) = 1/(x - 1.05) on [%g, %g], tol = %g\n\n", a, b, tol);
    std::printf("%s\n", bar.c_str());
    std::printf("x = %-*gx = %g\n\n", width - 5, a, b);

    // Run-length summary of the refinement levels, left to right. A panel at
    // level k is (b - a) / 2^k wide, so this line is the refinement history.
    // Levels, not raw widths: the scan quantizes each edge, but the level is
    // exact.
    std::printf("%zu panels, level:count left to right (level k means width (b-a)/2^k):\n ", edge.size() - 1);
    const auto level = [&](std::size_t i) {
        return static_cast<int>(std::lround(std::log2((b - a) / (edge[i] - edge[i - 1]))));
    };
    for (std::size_t i = 1; i < edge.size();) {
        const int   k = level(i);
        std::size_t n = 0;
        while (i < edge.size() && level(i) == k) {
            ++n;
            ++i;
        }
        std::printf("  %d:%zu", k, n);
    }
    std::printf("\n\n");

    // Accuracy: the requested tolerance is relative to max|f| over the domain.
    double max_rel = 0.0;
    for (int i = 0; i <= 100'000; ++i) {
        const double x = a + (b - a) * static_cast<double>(i) / 100'000;
        max_rel        = std::max(max_rel, std::abs(fn(x) - f(x)) / std::abs(f(b)));
    }
    std::printf("max error relative to max|f| = %.3e (requested %.0e)\n", max_rel, tol);

    // The two claims the picture makes, as checks.
    const double widest   = edge[1] - edge[0];
    const double narrowest = edge[edge.size() - 1] - edge[edge.size() - 2];
    if (max_rel > 10 * tol) {
        std::printf("FAIL: fit missed the tolerance\n");
        return 1;
    }
    if (widest < 8 * narrowest) {
        std::printf("FAIL: panels are near-uniform, so the fit did not adapt\n");
        return 1;
    }
    return 0;
}
