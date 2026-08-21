// simple1d: the smallest treeweave demo: fit an expensive 1-D function once,
// then evaluate its fast polynomial approximant. Runs in a fraction of a second.
//
// Set the project up with CMake once; it fetches the deps (polyfit, POET, xsimd,
// mdspan) and consolidates every header into a single <build>/include tree:
//
//   git clone https://github.com/DiamonDinoia/treeweave.git
//   cmake -S treeweave -B build -DTREEWEAVE_BUILD_EXAMPLES=ON
//   cmake --build build
//
// Then compile this file without CMake, one include flag, like xsimd. The right
// -I depends only on whether the library is installed:
//
//   * built, NOT installed:      -Ibuild/include      (the tree CMake just merged)
//   * after `cmake --install`:   -I<prefix>/include   (or nothing, standard prefix)
//
//   g++ -std=c++20 -O3 -march=native examples/c++/simple1d.cpp -Ibuild/include -o simple1d
//
// Simpler still: `cd examples/c++ && make` (uses the generated build/make.inc;
// `make -n simple1d` prints the exact command). Inside a CMake project, just
// `target_link_libraries(app PRIVATE treeweave::treeweave)`, no -I at all.
// For a throughput benchmark (single / batched / sorted eval), see zeta_bench.cpp.

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

    const double x = 3.5;
    // Evaluate f on (3.5) and print the result.
    std::cout << std::setprecision(15)           //
              << "zeta(x) = " << zeta(x) << "\n" //
              << "f(x)    = " << f(x) << "\n"    //
              << "rel err = " << std::abs(f(x) - zeta(x)) / std::abs(zeta(x)) << "\n";
    return 0;
}
