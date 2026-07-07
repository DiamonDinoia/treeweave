// Zeta bench (C++): treeweave vs adaptive-stop Riemann-zeta baseline (<=160 terms); single/batch/sorted.
// Cross-language family member. TREEWEAVE_BENCH_YAML=path emits YAML. (see devel/agents/build-notes.md)

// std::getenv trips MSVC's C4996 "may be unsafe" (fatal under /WX); it is the
// portable, correct call here. Must precede any (transitive) <cstdlib> include.
#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <treeweave/treeweave.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <vector>

namespace {

using clock_type = std::chrono::steady_clock;

auto elapsed_s(clock_type::time_point t0) -> double {
    return std::chrono::duration<double>(clock_type::now() - t0).count();
}

// Fair baseline: adaptive-stop sum (rel eps=1e-10, cap 160 terms), not a fixed-iteration strawman.
// (see devel/agents/build-notes.md)
constexpr double      kEps      = 1e-10;
constexpr std::size_t kMaxTerms = 160;

auto zeta_partial(double s) -> double {
    double acc = 0.0;
    for (std::size_t k = 1; k <= kMaxTerms; ++k) {
        const double term = std::pow(static_cast<double>(k), -s);
        acc += term;
        if (term < kEps * acc)
            break;
    }
    return acc;
}

} // namespace

int main() {
    // Smooth, positive, monotone on [2, 10] (clear of the s=1 pole): fit to a
    // relative tolerance (treeweave's default).
    constexpr double   tol = 1e-10;
    constexpr double   a   = 2.0;
    constexpr double   b   = 10.0;
    treeweave::options opts;
    opts.tol_kind = treeweave::TolKind::RelativeMax;
    auto fn       = treeweave::fit(zeta_partial, a, b, tol, opts);

    constexpr std::size_t n        = 1'000'000; // batch / sorted points
    constexpr std::size_t n_scalar = 100'000;   // scalar-API points
    constexpr std::size_t n_native = 256;       // brute-force sample (N pows each)

    std::mt19937                           gen(7);
    std::uniform_real_distribution<double> ds(a, b);
    std::vector<double>                    xs(n);
    for (auto &x : xs)
        x = ds(gen);

    double max_rel = 0.0;
    for (std::size_t i = 0; i < n_native; ++i) {
        const double approx = fn(xs[i]);
        const double exact  = zeta_partial(xs[i]);
        max_rel             = std::max(max_rel, std::abs(approx - exact) / std::abs(exact));
    }

    volatile double     sink = 0.0; // anti-DCE sink for every timed loop
    std::vector<double> out(n);

    for (std::size_t i = 0; i < n_native; ++i)
        sink = sink + zeta_partial(xs[i]); // warm-up (untimed)
    auto t0 = clock_type::now();
    for (std::size_t i = 0; i < n_native; ++i)
        sink = sink + zeta_partial(xs[i]);
    const double nat_s    = elapsed_s(t0);
    const double nat_rate = n_native / (nat_s * 1e6); // Mevals/s, all modes

    for (std::size_t i = 0; i < n_scalar; ++i)
        sink = sink + fn(xs[i]); // warm-up (untimed)
    t0 = clock_type::now();
    for (std::size_t i = 0; i < n_scalar; ++i)
        sink = sink + fn(xs[i]);
    const double tw_single_s = elapsed_s(t0);

    fn(xs.data(), out.data(), n); // warm-up (untimed)
    sink = out[0];
    t0   = clock_type::now();
    fn(xs.data(), out.data(), n);
    const double tw_multi_s = elapsed_s(t0);
    sink                    = out[0];

    std::vector<double> xs_sorted(xs);
    std::sort(xs_sorted.begin(), xs_sorted.end());

    fn.sorted(xs_sorted.data(), out.data(), n); // warm-up (untimed)
    sink = out[0];
    t0   = clock_type::now();
    fn.sorted(xs_sorted.data(), out.data(), n);
    const double tw_sorted_s = elapsed_s(t0);
    sink                     = out[0];

    const double tw_single = n_scalar / (tw_single_s * 1e6);
    const double tw_multi  = n / (tw_multi_s * 1e6);
    const double tw_sorted = n / (tw_sorted_s * 1e6);

    std::cout << "zeta(s) = sum_k k^-s (<=" << kMaxTerms << " terms, stop at " << kEps << " rel), fit on [" << a << ", "
              << b << "], relative tol " << tol << "\n"
              << "  max rel err: " << max_rel << "\n"
              << "  single-eval  treeweave " << tw_single << "  native " << nat_rate << " Mevals/s  speedup "
              << tw_single / nat_rate << "x\n"
              << "  multi-eval   treeweave " << tw_multi << "  native " << nat_rate << " Mevals/s  speedup "
              << tw_multi / nat_rate << "x\n"
              << "  sorted-eval  treeweave " << tw_sorted << "  native " << nat_rate << " Mevals/s  speedup "
              << tw_sorted / nat_rate << "x\n";

    if (const char *yaml_path = std::getenv("TREEWEAVE_BENCH_YAML")) {
        std::ofstream y(yaml_path);
        if (!y) {
            std::cerr << "could not open TREEWEAVE_BENCH_YAML path: " << yaml_path << "\n";
            return 1;
        }
        // std::scientific => every float carries a '.', so YAML 1.1 reads a float.
        y << std::scientific << std::setprecision(17);
        const auto block = [&](const char *name, double tw, double nat) {
            y << name << ":\n"
              << "  treeweave_mevals_s: " << tw << "\n"
              << "  native_mevals_s: " << nat << "\n"
              << "  speedup: " << tw / nat << "\n";
        };
        y << "language: \"c++\"\n"
          << "domain: [" << a << ", " << b << "]\n"
          << "tol: " << tol << "\n"
          << "n_pts: " << n << "\n"
          << "max_rel_err: " << max_rel << "\n";
        block("single_eval", tw_single, nat_rate);
        block("multi_eval", tw_multi, nat_rate);
        block("sorted_eval", tw_sorted, nat_rate);
    }

    return (max_rel < 1e-7) ? 0 : 1;
}
