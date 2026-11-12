// Zeta bench (C++): treeweave vs adaptive-stop Riemann-zeta baseline (<=160 terms); single/batch/sorted.
// Cross-language family member. TREEWEAVE_BENCH_YAML=path emits YAML.

// std::getenv trips MSVC's C4996 "may be unsafe" (fatal under /WX); it is the
// portable, correct call here. Must precede any (transitive) <cstdlib> include.
#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <treeweave/treeweave.hpp>

#ifdef TREEWEAVE_BENCH_BASELINES
#include <boost/math/special_functions/chebyshev_transform.hpp>
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>

namespace {

using clock_type = std::chrono::steady_clock;

auto elapsed_s(clock_type::time_point t0) -> double {
    return std::chrono::duration<double>(clock_type::now() - t0).count();
}

// Fair baseline: adaptive-stop sum (rel eps=1e-10, cap 160 terms), not a fixed-iteration strawman.
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

// Fit target: all kMaxTerms terms. The early stop above makes zeta_partial jump
// by up to kEps relative where the term count changes, and a global Chebyshev
// series then needs degree 177 instead of 26 on [2, 10]. Every interpolator,
// treeweave included, fits this smooth sum; the native rate uses zeta_partial.
auto zeta_smooth(double s) -> double {
    double acc = 0.0;
    for (std::size_t k = 1; k <= kMaxTerms; ++k)
        acc += std::pow(static_cast<double>(k), -s);
    return acc;
}

// A native interpolator grown until it meets the tolerance on the check sample,
// then timed with the same protocol as treeweave. Every rate is Mevals/s.
struct Baseline {
    std::string name;
    std::string size;
    double      relmax;
    double      max_rel;
    double      single;
    double      multi;
    double      sorted;
};

// {max|p - f| / max|f|, max |p - f| / |f|} over the check sample. The first is
// treeweave's default RelativeMax criterion, so every baseline is grown against
// it; the second is the pointwise error. Both are reported for every method,
// treeweave included.
auto rel_errors(const auto &f, const std::vector<double> &xs, const std::vector<double> &ref) -> std::array<double, 2> {
    double max_abs = 0.0, max_f = 0.0, max_rel = 0.0;
    for (std::size_t i = 0; i < xs.size(); ++i) {
        const double err = std::abs(f(xs[i]) - ref[i]);
        max_abs          = std::max(max_abs, err);
        max_f            = std::max(max_f, std::abs(ref[i]));
        max_rel          = std::max(max_rel, err / std::abs(ref[i]));
    }
    return {max_abs / max_f, max_rel};
}

// A library with only a scalar call: batch and sorted batch loop over it.
auto time_scalar_api(const auto &f, const std::vector<double> &xs, const std::vector<double> &xs_sorted,
                     std::size_t n_scalar) -> std::array<double, 3> {
    volatile double sink = 0.0;
    const auto      rate = [&](const std::vector<double> &pts, std::size_t count) {
        for (std::size_t i = 0; i < count; ++i)
            sink = sink + f(pts[i]); // warm-up (untimed)
        const auto t0 = clock_type::now();
        for (std::size_t i = 0; i < count; ++i)
            sink = sink + f(pts[i]);
        return static_cast<double>(count) / (elapsed_s(t0) * 1e6);
    };
    return {rate(xs, n_scalar), rate(xs, xs.size()), rate(xs_sorted, xs_sorted.size())};
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
    auto fn       = treeweave::fit(zeta_smooth, a, b, tol, opts);

    constexpr std::size_t n        = 1'000'000; // batch / sorted points
    constexpr std::size_t n_scalar = 100'000;   // scalar-API points
    constexpr std::size_t n_native = 256;       // brute-force sample (N pows each)

    std::mt19937                           gen(7);
    std::uniform_real_distribution<double> ds(a, b);
    std::vector<double>                    xs(n);
    for (auto &x : xs)
        x = ds(gen);

    // Accuracy of every method, treeweave included, on the same sample.
    constexpr std::size_t     n_check = 4096;
    const std::vector<double> xs_check(xs.begin(), xs.begin() + n_check);
    std::vector<double>       ref_check(n_check);
    for (std::size_t i = 0; i < n_check; ++i)
        ref_check[i] = zeta_smooth(xs_check[i]);
    const auto   tw_err  = rel_errors([&](double x) { return fn(x); }, xs_check, ref_check);
    const double max_rel = tw_err[1];

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

    std::vector<Baseline> baselines;
#ifdef TREEWEAVE_BENCH_BASELINES
    {
        // Boost.Math chebyshev_transform: FFT-based Chebyshev series, refined
        // until the coefficient tail falls below its tol. Tighten tol by 10x
        // until the series meets the requested relative tolerance.
        for (double cheb_tol = tol;; cheb_tol /= 10) {
            const boost::math::chebyshev_transform<double> cheb(zeta_smooth, a, b, cheb_tol);
            const auto                                     err = rel_errors(cheb, xs_check, ref_check);
            if (err[0] <= tol) {
                const auto r = time_scalar_api(cheb, xs, xs_sorted, n_scalar);
                baselines.push_back({"Boost.Math chebyshev_transform",
                                     "degree " + std::to_string(cheb.coefficients().size() - 1), err[0], err[1], r[0],
                                     r[1], r[2]});
                break;
            }
            if (cheb_tol < 1e-15) {
                std::cerr << "Boost chebyshev_transform never met tol " << tol << "\n";
                return 1;
            }
        }
    }
#endif

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
    for (const auto &bl : baselines)
        std::cout << "  " << bl.name << " (" << bl.size << ", max rel err " << bl.max_rel << ")  single " << bl.single
                  << "  multi " << bl.multi << "  sorted " << bl.sorted << " Mevals/s\n";

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
          << "relmax_err: " << tw_err[0] << "\n"
          << "max_rel_err: " << max_rel << "\n";
        block("single_eval", tw_single, nat_rate);
        block("multi_eval", tw_multi, nat_rate);
        block("sorted_eval", tw_sorted, nat_rate);
        if (!baselines.empty())
            y << "interp:\n";
        for (const auto &bl : baselines)
            y << "  - name: \"" << bl.name << "\"\n"
              << "    size: \"" << bl.size << "\"\n"
              << "    relmax_err: " << bl.relmax << "\n"
              << "    max_rel_err: " << bl.max_rel << "\n"
              << "    single_eval: " << bl.single << "\n"
              << "    multi_eval: " << bl.multi << "\n"
              << "    sorted_eval: " << bl.sorted << "\n";
    }

    return (max_rel < 1e-7) ? 0 : 1;
}
