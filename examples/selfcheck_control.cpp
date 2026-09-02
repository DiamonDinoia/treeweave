// Positive control for the self-check every example ends with. Each example
// computes a relative error against the function it fitted and returns
// `err < tol ? 0 : 1`. This program does the same against a *different*
// function, so the error is large and the program must exit 1. Its ctest is
// WILL_FAIL, so the suite turns red if a wrong answer ever stops failing.

#include <treeweave/treeweave.hpp>

#include <cmath>

int main() {
    auto f = treeweave::fit([](double x) { return std::sin(x); }, 0.0, 1.0, /*tol=*/1e-10);

    const double x   = 0.5;
    const double err = std::abs(f(x) - std::cos(x)) / std::abs(std::cos(x));
    return err < 1e-8 ? 0 : 1;
}
