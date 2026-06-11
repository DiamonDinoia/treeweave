// hankel.cpp — a real-world treeweave use case: a recurring (oscillatory) special
// function.
//
// The Hankel function of the first kind H0^(1)(x) = J0(x) + i*Y0(x) shows up all
// over wave scattering, acoustics, and electromagnetics. Evaluating it from a
// library (here the standard-library Bessel functions) is accurate but slow, and
// in a solver you evaluate it millions of times. treeweave fits it once into a real
// 2-output function ([Re, Im] = [J0, Y0]) and then evaluates a cheap polynomial
// per point.
//
// Bessel J0/Y0 portability: the C++17 special math functions
// (std::cyl_bessel_j / std::cyl_neumann) are implemented by libstdc++ and the
// MSVC STL but NOT by libc++ (Apple/Clang). Where they are unavailable we fall
// back to the POSIX j0()/y0() (MSVC spells them _j0/_y0); both compute the same
// order-0 Bessel functions of the first and second kind.
//
// We fit on [1, 30): Y0 diverges at the origin, so — per treeweave's "shift the
// domain off the singularity" guidance — we start at x = 1 rather than 0.
// A 1-D vector-valued fit spells its input as std::array<double, 1>.

#include <treeweave/treeweave.hpp>

#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <random>
#include <vector>

namespace {
// Order-0 Bessel functions J0/Y0, routed to whatever the platform provides.
#if defined(__cpp_lib_math_special_functions)
inline double bessel_j0(double x) { return std::cyl_bessel_j(0.0, x); }
inline double bessel_y0(double x) { return std::cyl_neumann(0.0, x); }
#elif defined(_MSC_VER)
inline double bessel_j0(double x) { return ::_j0(x); }
inline double bessel_y0(double x) { return ::_y0(x); }
#else
inline double bessel_j0(double x) { return ::j0(x); }
inline double bessel_y0(double x) { return ::y0(x); }
#endif
} // namespace

int main() {
    // Hankel H0^(1) packed as a real 2-vector: {J0(x), Y0(x)}.
    auto hankel0 = [](std::array<double, 1> x) -> std::array<double, 2> { return {bessel_j0(x[0]), bessel_y0(x[0])}; };

    // J0/Y0 oscillate through zero, so relative error is meaningless near their
    // roots — fit to an *absolute* tolerance instead (the natural choice for
    // oscillatory functions).
    constexpr double   tol = 1e-8;
    treeweave::options opts;
    opts.tol_kind = treeweave::TolKind::AbsoluteMax;
    std::array<double, 1> a{1.0};
    std::array<double, 1> b{30.0};
    auto                  fn = treeweave::fit(hankel0, a, b, tol, opts);

    // --- accuracy vs the library, on random points -------------------------
    std::mt19937                           gen(7);
    std::uniform_real_distribution<double> dx(a[0], b[0]);
    constexpr int                          n_pts = 1'000'000;
    std::vector<double>                    xs(n_pts);
    for (auto &x : xs)
        x = dx(gen);

    std::array<double, 2> max_abs{0.0, 0.0};
    for (double x : xs) {
        const auto approx = fn(std::array<double, 1>{x});
        const auto exact  = hankel0(std::array<double, 1>{x});
        for (std::size_t k = 0; k < 2; ++k)
            max_abs[k] = std::max(max_abs[k], std::abs(approx[k] - exact[k]));
    }

    // --- throughput: treeweave vs calling the library directly ----------------
    // A single volatile accumulator is used as an anti-DCE sink for both the
    // treeweave and library timing loops.  Declaring `sink` volatile means
    // every read and write is a sequentially-consistent observable side effect
    // — the compiler cannot fold, hoist, or eliminate the loop bodies even
    // when it can see that the final value of `sink` is never read again.
    volatile double sink = 0.0;

    // Warm up: one cold-start batch eval before the timed run so instruction
    // caches and branch predictors are in a steady state.
    std::vector<double> out(2 * n_pts);
    fn(xs.data(), out.data(), n_pts); // warm-up pass (untimed)
    sink = out[0];                    // prevent DCE of the warm-up

    const auto t0 = std::chrono::steady_clock::now();
    fn(xs.data(), out.data(), n_pts); // timed pass
    const auto t1 = std::chrono::steady_clock::now();
    sink          = out[0]; // volatile write — prevents DCE of the timed fn(...)

    const auto t2 = std::chrono::steady_clock::now();
    for (double x : xs)
        sink = sink + bessel_j0(x) + bessel_y0(x);
    const auto t3 = std::chrono::steady_clock::now();

    const double treeweave_s = std::chrono::duration<double>(t1 - t0).count();
    const double lib_s       = std::chrono::duration<double>(t3 - t2).count();

    std::cout << "Hankel H0^(1) fit on [" << a[0] << ", " << b[0] << "), abs tol " << tol << "\n"
              << "  max abs err  Re=J0: " << max_abs[0] << "   Im=Y0: " << max_abs[1] << "\n"
              << "  treeweave:  " << n_pts / (treeweave_s * 1e6) << " Mevals/s\n"
              << "  library: " << n_pts / (lib_s * 1e6) << " Mevals/s\n"
              << "  speedup: " << lib_s / treeweave_s << "x\n";

    return (max_abs[0] < 100 * tol && max_abs[1] < 100 * tol) ? 0 : 1;
}
