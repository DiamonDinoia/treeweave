// singularity: what treeweave does when f is not smooth, and what to do about
// it. Runs the three cases that matter, in order of increasing trouble, and
// prints the outcome of each. The docs quote this program's output.
//
// The point: adaptivity handles a singularity NEAR the domain for the price of a
// logarithm, and refuses, loudly, to fake one ON or INSIDE it.
//
// Every line also carries its expected outcome, so this example fails if the
// refusal ever turns into a quietly wrong answer, or vice versa.

#include <treeweave/treeweave.hpp>

#include <cmath>
#include <cstdio>
#include <exception>

namespace {

enum class Expect { Fits, Throws };

int failures = 0;

// Fit f on [a, b], print leaves/memory/error or the exception raised, and check
// the outcome against what the docs claim it is.
template <typename F>
auto report(const char *label, F f, double a, double b, double tol, Expect expect) -> void {
    std::printf("  %-32s ", label);
    try {
        const auto fn = treeweave::fit(f, a, b, tol);
        // tol is relative to max|f| over the domain, so compare on that scale.
        double scale = 0.0, max_rel = 0.0;
        for (int i = 0; i <= 20'000; ++i) {
            const double x = a + (b - a) * static_cast<double>(i) / 20'000;
            scale          = std::max(scale, std::abs(f(x)));
        }
        for (int i = 0; i <= 20'000; ++i) {
            const double x = a + (b - a) * static_cast<double>(i) / 20'000;
            max_rel        = std::max(max_rel, std::abs(fn(x) - f(x)) / scale);
        }
        std::printf("fitted: %4zu panels, %6.1f KiB, err %.1e\n", fn.num_leaves(),
                    static_cast<double>(fn.memory_usage()) / 1024.0, max_rel);
        if (expect != Expect::Fits || max_rel > 10 * tol) {
            std::printf("    FAIL: expected this fit to be refused, or it missed the tolerance\n");
            ++failures;
        }
    } catch (const treeweave::MaxDepthExceeded &) {
        std::printf("refused: MaxDepthExceeded\n");
        if (expect != Expect::Throws) {
            std::printf("    FAIL: expected this fit to converge\n");
            ++failures;
        }
    } catch (const treeweave::MemoryBudgetExceeded &) {
        std::printf("refused: MemoryBudgetExceeded\n");
        if (expect != Expect::Throws) {
            std::printf("    FAIL: expected this fit to converge\n");
            ++failures;
        }
    }
}

} // namespace

int main() {
    constexpr double tol = 1e-10;
    std::printf("f(x) = 1/(x - p) fitted to relative tolerance %.0e\n\n", tol);

    // Case 1: the pole sits outside the domain. Refinement pays a logarithm:
    // each decade closer costs a bounded number of extra panels, until the
    // default memory budget (4 MiB in 1-D) runs out.
    std::printf("Case 1, pole p outside the domain [-1, 1]:\n");
    const double gaps[] = {1.0, 1e-1, 1e-2, 1e-4};
    for (const double gap : gaps) {
        char label[64];
        std::snprintf(label, sizeof label, "p = 1 + %g", gap);
        report(label, [p = 1.0 + gap](double x) { return 1.0 / (x - p); }, -1.0, 1.0, tol, Expect::Fits);
    }
    report("p = 1 + 1e-08", [](double x) { return 1.0 / (x - (1.0 + 1e-8)); }, -1.0, 1.0, tol, Expect::Throws);

    // Case 2: the pole is ON the boundary. The panel touching it never
    // converges, so the fit stops and throws. The fix is to cut the domain
    // short, which is what examples/c++/hankel.cpp does for Y0 at the origin.
    std::printf("\nCase 2, pole exactly on the boundary, and the fix:\n");
    const auto pole_at_one = [](double x) { return 1.0 / (x - 1.0); };
    report("domain [-1, 1]", pole_at_one, -1.0, 1.0, tol, Expect::Throws);
    report("domain [-1, 0.9]", pole_at_one, -1.0, 0.9, tol, Expect::Fits);

    // Case 3: the pole is strictly inside. No panel layout fixes that, and
    // treeweave says so instead of returning a plausible-looking wrong answer.
    // The fix is to split the domain at the singularity: each side is Case 2.
    std::printf("\nCase 3, pole strictly inside the domain, and the fix:\n");
    const auto pole_at_zero = [](double x) { return 1.0 / x; };
    report("domain [-1, 1]", pole_at_zero, -1.0, 1.0, tol, Expect::Throws);
    report("domain [-1, -1e-3]", pole_at_zero, -1.0, -1e-3, tol, Expect::Fits);
    report("domain [1e-3, 1]", pole_at_zero, 1e-3, 1.0, tol, Expect::Fits);

    std::printf("\n%s\n", failures == 0 ? "all outcomes as documented" : "SOME OUTCOMES CHANGED");
    return failures == 0 ? 0 : 1;
}
