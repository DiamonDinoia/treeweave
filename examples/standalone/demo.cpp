// Header-only drop-in demo: fit an expensive 1-D function once, then evaluate
// its fast polynomial approximant. This directory models your OWN project
// sitting next to an extracted headers bundle — nothing here references the
// treeweave repo layout, so it compiles with just `-Iinclude`:
//
//   wget https://github.com/DiamonDinoia/treeweave/releases/latest/download/treeweave-cxx-headers.tar.gz
//   tar xzf treeweave-cxx-headers.tar.gz              # -> ./include/...
//   make                                              # or the g++ line in the README
//
// ponytail: body kept byte-for-byte in sync with examples/c++/simple1d.cpp on
// purpose — a standalone example can't #include the repo's example, so the two
// are deliberate duplicates. Change one, change the other.

#include <treeweave/treeweave.hpp>

#include <cmath>
#include <iomanip>
#include <iostream>

int main() {
    // zeta_N(s) = sum_{k=1..1000} k^-s — hundreds of pow() calls per evaluation.
    auto zeta = [](double s) {
        double a = 0.0;
        for (int k = 1; k <= 1000; ++k)
            a += std::pow(k, -s);
        return a;
    };

    // Fit once (milliseconds); f is then a cheap polynomial good to ~1e-10.
    auto f = treeweave::fit(zeta, 2.0, 10.0, /*tol=*/1e-10);

    const double x = 3.5;
    std::cout << std::setprecision(15)           //
              << "zeta(x) = " << zeta(x) << "\n" //
              << "f(x)    = " << f(x) << "\n"    //
              << "rel err = " << std::abs(f(x) - zeta(x)) / std::abs(zeta(x)) << "\n";
    return 0;
}
