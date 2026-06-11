// lgamma_bench.cpp — treeweave vs the standard-library special function.
//
// log-Gamma (`std::lgamma`) is a workhorse special function: it shows up in
// statistics (log-densities of the Gamma/Beta/Dirichlet, log-binomial),
// combinatorics (log n!), and anywhere products of many terms are summed in
// log-space. The library routine is accurate but comparatively slow, and in a
// tight loop you may evaluate it millions of times. treeweave fits it once and
// then evaluates a cheap polynomial per point.
//
// Domain [3, 50): lgamma has roots at x = 1 and x = 2 (lgamma(1) = lgamma(2) =
// 0) and a minimum near x ≈ 1.4618. We fit where it is smooth, positive, and
// monotone increasing — x ≥ 3 — so relative error is well defined everywhere
// (lgamma(3) = ln 2 ≈ 0.693, lgamma(50) ≈ 144.57). That makes this the natural
// place to use treeweave's default RelativeMax tolerance, in contrast to the
// oscillatory hankel.cpp example which must use AbsoluteMax.
//
// This is the C++ member of a cross-language benchmark family (one per binding:
// C, Fortran, Python, Julia, MATLAB/Octave) that all fit lgamma on [3, 50) with
// a relative tolerance and report the same three numbers — max relative error
// vs the native routine, throughput, and speedup — so the bindings can be
// compared directly.

#include <treeweave/treeweave.hpp>

#include <chrono>
#include <cmath>
#include <iostream>
#include <random>
#include <vector>

int main() {
    // 1-D scalar fit: scalar input, scalar output (cf. examples/c++/simple1d.cpp).
    auto lgamma_fn = [](double x) -> double { return std::lgamma(x); };

    // lgamma is zero-free and monotone on [3, 50): fit to a *relative*
    // tolerance (treeweave's default), the natural error measure for a
    // function that spans two orders of magnitude with no sign change.
    constexpr double   tol = 1e-10;
    constexpr double   a   = 3.0;
    constexpr double   b   = 50.0;
    treeweave::options opts;
    opts.tol_kind = treeweave::TolKind::RelativeMax;
    auto fn       = treeweave::fit(lgamma_fn, a, b, tol, opts);

    // --- accuracy vs the library, on random points -------------------------
    std::mt19937                           gen(7);
    std::uniform_real_distribution<double> dx(a, b);
    constexpr int                          n_pts = 1'000'000;
    std::vector<double>                    xs(n_pts);
    for (auto &x : xs)
        x = dx(gen);

    double max_rel = 0.0;
    for (double x : xs) {
        const double approx = fn(x);
        const double exact  = std::lgamma(x);
        max_rel             = std::max(max_rel, std::abs(approx - exact) / std::abs(exact));
    }

    // --- throughput: treeweave vs calling the library directly ----------------
    // A single volatile accumulator is the anti-DCE sink for both timing loops:
    // every read and write of a volatile is an observable side effect, so the
    // compiler cannot fold, hoist, or delete the loop bodies.
    volatile double sink = 0.0;

    // Warm up one cold batch eval before timing so caches and the branch
    // predictor are in steady state.
    std::vector<double> out(n_pts);
    fn(xs.data(), out.data(), n_pts); // warm-up pass (untimed)
    sink = out[0];                    // prevent DCE of the warm-up

    const auto t0 = std::chrono::steady_clock::now();
    fn(xs.data(), out.data(), n_pts); // timed pass
    const auto t1 = std::chrono::steady_clock::now();
    sink          = out[0]; // volatile write — prevents DCE of the timed fn(...)

    const auto t2 = std::chrono::steady_clock::now();
    for (double x : xs)
        sink = sink + std::lgamma(x);
    const auto t3 = std::chrono::steady_clock::now();

    const double treeweave_s = std::chrono::duration<double>(t1 - t0).count();
    const double lib_s       = std::chrono::duration<double>(t3 - t2).count();

    std::cout << "lgamma fit on [" << a << ", " << b << "), relative tol " << tol << "\n"
              << "  max rel err: " << max_rel << "\n"
              << "  treeweave:  " << n_pts / (treeweave_s * 1e6) << " Mevals/s\n"
              << "  library: " << n_pts / (lib_s * 1e6) << " Mevals/s\n"
              << "  speedup: " << lib_s / treeweave_s << "x\n";

    return (max_rel < 1e-7) ? 0 : 1;
}
