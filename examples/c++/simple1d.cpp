// simple1d: fit an expensive 1-D function once, then evaluate the polynomial
// approximant instead. Build recipes live in docs/guides/cpp.rst.

#include <treeweave/treeweave.hpp>

#include <cmath>
#include <iomanip>
#include <iostream>

int main() {
    // zeta_N(s) = sum_{k=1..1000} k^-s: hundreds of pow() calls per evaluation.
    auto zeta = [](double s) {
        double a = 0.0;
        for (int k = 1; k <= 1000; ++k)
            a += std::pow(k, -s);
        return a;
    };

    // Fit zeta(s) on [2, 10] syntax is fit(callback, lower_bound, upper_bound, tolerance).
    auto f = treeweave::fit(zeta, 2.0, 10.0, /*tol=*/1e-10);

    const double x   = 3.5;
    const double err = std::abs(f(x) - zeta(x)) / std::abs(zeta(x));
    std::cout << std::setprecision(15)           //
              << "zeta(x) = " << zeta(x) << "\n" //
              << "f(x)    = " << f(x) << "\n"    //
              << "rel err = " << err << "\n";
    return err < 1e-8 ? 0 : 1;
}
