// nanobench harness sweeping {1D, 2D, 3D} × kernels × {deg 6,8,10} × N ∈ {1,32,1024,1e6}.
// Pin to one core for stable numbers. (see devel/agents/build-notes.md)

#define ANKERL_NANOBENCH_IMPLEMENT
#include <nanobench.h>

#include <treeweave/treeweave.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

#include <xsimd/xsimd.hpp>

namespace {

// 1D scientific kernels. Suite spans smooth, oscillatory, sigmoid-shaped,
// sharp-transition, and near-singular cases so the leaf-eval and tree-depth
// regimes both get exercised.
auto make_runge1d() {
    return [](double x) { return 1.0 / (1.0 + 25.0 * x * x); };
}
auto make_erf1d() {
    return [](double x) { return std::erf(x); };
}
// cyl_bessel_j unavailable on libc++ (Apple clang); kHasCylBesselJ gates the kernel at the call site.
// (see devel/agents/build-notes.md)
#if defined(_LIBCPP_VERSION)
inline constexpr bool kHasCylBesselJ = false;
[[maybe_unused]] auto make_j0_1d() {
    return [](double x) { return x; };
}
#else
inline constexpr bool kHasCylBesselJ = true;
auto                  make_j0_1d() {
    return [](double x) { return std::cyl_bessel_j(0, x); };
}
#endif
auto make_tanh1d() {
    return [](double x) { return std::tanh(50.0 * x); };
}
auto make_log1p1d() {
    return [](double x) { return std::log1p(x); };
}

// 2D / 3D kernels typical of scientific computing: Gaussians, oscillatory,
// RBFs, screened-Coulomb (Yukawa).
auto make_bump2d() {
    return [](std::array<double, 2> x) -> std::array<double, 1> {
        return {std::exp(-100.0 * (x[0] - 0.5) * (x[0] - 0.5) - (x[1] - 0.5) * (x[1] - 0.5))};
    };
}
auto make_osc2d() {
    return
        [](std::array<double, 2> x) -> std::array<double, 1> { return {std::cos(8.0 * x[0]) * std::cos(8.0 * x[1])}; };
}
auto make_mq2d() {
    return
        [](std::array<double, 2> x) -> std::array<double, 1> { return {std::sqrt(1.0 + x[0] * x[0] + x[1] * x[1])}; };
}

auto make_gauss3d() {
    return [](std::array<double, 3> x) -> std::array<double, 1> {
        return {std::exp(-x[0] * x[0] - x[1] * x[1] - x[2] * x[2])};
    };
}
auto make_yukawa3d() {
    return [](std::array<double, 3> x) -> std::array<double, 1> {
        const double r = std::sqrt(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]);
        return {std::exp(-r) / r};
    };
}
auto make_imq3d() {
    return [](std::array<double, 3> x) -> std::array<double, 1> {
        return {1.0 / std::sqrt(1.0 + x[0] * x[0] + x[1] * x[1] + x[2] * x[2])};
    };
}

ankerl::nanobench::Bench make_bench(std::size_t n_pts) {
    ankerl::nanobench::Bench b;
    b.title("treeweave eval pipeline")
        .unit("eval")
        .batch(static_cast<double>(n_pts))
        .relative(true)
        .warmup(3)
        .minEpochIterations(1);
    if (n_pts >= 1'000'000)
        b.minEpochTime(std::chrono::milliseconds(50));
    return b;
}

#ifndef TREEWEAVE_BENCH_POLICY
#define TREEWEAVE_BENCH_POLICY ::treeweave::EvalPolicy::Balanced
#endif

template <std::size_t Deg, class Fmaker>
void sweep_1d(ankerl::nanobench::Bench &b, const char *label, Fmaker make_f, double a, double b_) {
    auto               f = make_f();
    treeweave::options opts;
    opts.max_memory_mib = 0; // disable fit-time leaf-storage budget for bench
    auto         fn     = treeweave::fit<Deg, TREEWEAVE_BENCH_POLICY>(f, a, b_, /*tol=*/1e-10, opts);
    std::mt19937 gen(7);
    std::uniform_real_distribution<double> d(a + 1e-3, b_ - 1e-3);

    // True single-point scalar: exercises Function::operator()(x) → polyfits_[id](x),
    // i.e. the polyfit scalar kernel that EvalPolicy::Latency targets.
    {
        std::vector<double> xs(1024);
        for (auto &x : xs)
            x = d(gen);
        std::string name = std::string(label) + " deg=" + std::to_string(Deg) + " dim=1 scalar-op()";
        b.batch(1.0);
        b.run(name, [&] {
            double acc = 0.0;
            for (double x : xs)
                acc += fn(x);
            ankerl::nanobench::doNotOptimizeAway(acc);
        });
    }
    for (std::size_t n_pts : {std::size_t(1), std::size_t(32), std::size_t(1024), std::size_t(1'000'000)}) {
        std::vector<double> xs(n_pts);
        for (auto &x : xs)
            x = d(gen);
        std::vector<double> out(n_pts);

        std::string name = std::string(label) + " deg=" + std::to_string(Deg) + " dim=1 N=" + std::to_string(n_pts);
        b.batch(static_cast<double>(n_pts));
        b.run(name, [&] {
            fn(xs.data(), out.data(), n_pts);
            ankerl::nanobench::doNotOptimizeAway(out.front());
        });
    }
}

template <std::size_t Deg, std::size_t Dim, class Fmaker>
void sweep_nd(ankerl::nanobench::Bench &b, const char *label, Fmaker make_f, std::array<double, Dim> a,
              std::array<double, Dim> b_) {
    auto               f = make_f();
    treeweave::options opts;
    opts.max_memory_mib = 0; // disable fit-time leaf-storage budget for bench
    auto         fn     = treeweave::fit<Deg, TREEWEAVE_BENCH_POLICY>(f, a, b_, /*tol=*/1e-10, opts);
    std::mt19937 gen(7);
    std::uniform_real_distribution<double> ud(0.0, 1.0);
    auto pick = [&](std::size_t d) { return a[d] + (b_[d] - a[d] - 1e-3) * ud(gen) + 5e-4; };

    // True single-point scalar: ND Function::operator()(const array&) path.
    {
        std::vector<std::array<double, Dim>> xs(1024);
        for (auto &x : xs)
            for (std::size_t d = 0; d < Dim; ++d)
                x[d] = pick(d);
        std::string name =
            std::string(label) + " deg=" + std::to_string(Deg) + " dim=" + std::to_string(Dim) + " scalar-op()";
        b.batch(1.0);
        b.run(name, [&] {
            double acc = 0.0;
            for (const auto &x : xs) {
                auto y = fn(x);
                acc += y[0];
            }
            ankerl::nanobench::doNotOptimizeAway(acc);
        });
    }
    for (std::size_t n_pts : {std::size_t(1), std::size_t(32), std::size_t(1024), std::size_t(1'000'000)}) {
        std::vector<double> flat(Dim * n_pts);
        for (std::size_t i = 0; i < n_pts; ++i)
            for (std::size_t d = 0; d < Dim; ++d)
                flat[Dim * i + d] = pick(d);
        std::vector<double> out(n_pts);

        std::string name = std::string(label) + " deg=" + std::to_string(Deg) + " dim=" + std::to_string(Dim) +
                           " N=" + std::to_string(n_pts);
        b.batch(static_cast<double>(n_pts));
        b.run(name, [&] {
            fn(flat.data(), out.data(), n_pts);
            ankerl::nanobench::doNotOptimizeAway(out.front());
        });
    }
}

// Scattered multi-fit case modelling TRIQS/diagmc bench_chebfun: scalar operator() over R fits (not batched path).
// (see devel/agents/build-notes.md)
template <std::size_t Deg>
void sweep_multi_fit_1d(ankerl::nanobench::Bench &b, const char *label, std::size_t R, double beta) {
    std::mt19937 cgen(11);
    // Cap K below Deg so the underlying truth is exactly representable by
    // the leaf polynomial — keeps fit depth shallow and reproducible.
    const std::size_t                K = std::min<std::size_t>(Deg - 1, 6);
    std::vector<std::vector<double>> coefs(R, std::vector<double>(K));
    {
        std::uniform_real_distribution<double> cd(-1.0, 1.0);
        for (auto &cr : coefs)
            for (auto &c : cr)
                c = cd(cgen);
    }

    auto truth_for = [&, beta](std::size_t r) {
        const auto &cr = coefs[r];
        return [cr, beta](double tau) {
            const double y   = 2.0 * tau / beta - 1.0; // [-1, 1]
            double       sum = 0.0;
            for (std::size_t k = 0; k < cr.size(); ++k) {
                double Tk = (k == 0) ? 1.0 : (k == 1 ? y : 0.0);
                if (k >= 2) {
                    double Tk_1 = y, Tk_2 = 1.0;
                    for (std::size_t i = 2; i <= k; ++i) {
                        double Tn = 2.0 * y * Tk_1 - Tk_2;
                        Tk_2      = Tk_1;
                        Tk_1      = Tn;
                    }
                    Tk = Tk_1;
                }
                sum += cr[k] * Tk;
            }
            return sum;
        };
    };

    using fit_t = decltype(treeweave::fit<Deg>(std::function<double(double)>{}, 0.0, 1.0, 1e-10));
    std::vector<fit_t> fits;
    fits.reserve(R);
    for (std::size_t r = 0; r < R; ++r) {
        std::function<double(double)> f = truth_for(r);
        fits.emplace_back(treeweave::fit<Deg>(f, 0.0, beta, /*tol=*/1e-10));
    }

    std::mt19937                               ig(13);
    std::uniform_int_distribution<std::size_t> rd(0, R - 1);
    std::uniform_real_distribution<double>     td(1e-9, beta - 1e-9);

    for (std::size_t n_pts : {std::size_t(1), std::size_t(32), std::size_t(1024), std::size_t(1'000'000)}) {
        std::vector<std::size_t> rs(n_pts);
        std::vector<double>      taus(n_pts);
        for (std::size_t i = 0; i < n_pts; ++i) {
            rs[i]   = rd(ig);
            taus[i] = td(ig);
        }

        std::string name = std::string(label) + " deg=" + std::to_string(Deg) + " R=" + std::to_string(R) +
                           " N=" + std::to_string(n_pts);
        b.batch(static_cast<double>(n_pts));
        b.run(name, [&] {
            double acc = 0.0;
            for (std::size_t i = 0; i < n_pts; ++i)
                acc += fits[rs[i]](taus[i]);
            ankerl::nanobench::doNotOptimizeAway(acc);
        });
    }
}

} // namespace

int main() {
    std::printf("# treeweave microbench (nanobench, xsimd lane_w=%zu)\n", xsimd::batch<double>::size);

    auto b = make_bench(1);

    sweep_1d<6>(b, "1d_runge", make_runge1d, -1.0, 1.0);
    sweep_1d<8>(b, "1d_runge", make_runge1d, -1.0, 1.0);
    sweep_1d<10>(b, "1d_runge", make_runge1d, -1.0, 1.0);
    sweep_1d<8>(b, "1d_erf", make_erf1d, -3.0, 3.0);
    if constexpr (kHasCylBesselJ)
        sweep_1d<8>(b, "1d_bessel_j0", make_j0_1d, 0.5, 30.0);
    sweep_1d<10>(b, "1d_tanh_sharp", make_tanh1d, -1.0, 1.0);
    sweep_1d<8>(b, "1d_log1p", make_log1p1d, -0.9, 5.0);

    // Multi-fit scattered-access case (TRIQS/diagmc bench_chebfun shape).
    sweep_multi_fit_1d<8>(b, "1d_multi_fit", /*R=*/16, /*beta=*/10.0);

    sweep_nd<6, 2>(b, "2d_bump", make_bump2d, {0.0, 0.0}, {1.0, 1.0});
    sweep_nd<8, 2>(b, "2d_bump", make_bump2d, {0.0, 0.0}, {1.0, 1.0});
    sweep_nd<10, 2>(b, "2d_bump", make_bump2d, {0.0, 0.0}, {1.0, 1.0});
    sweep_nd<8, 2>(b, "2d_osc", make_osc2d, {-1.0, -1.0}, {1.0, 1.0});
    sweep_nd<8, 2>(b, "2d_mq", make_mq2d, {-1.0, -1.0}, {1.0, 1.0});

    sweep_nd<6, 3>(b, "3d_gauss", make_gauss3d, {-1.0, -1.0, -1.0}, {1.0, 1.0, 1.0});
    sweep_nd<8, 3>(b, "3d_gauss", make_gauss3d, {-1.0, -1.0, -1.0}, {1.0, 1.0, 1.0});
    sweep_nd<10, 3>(b, "3d_gauss", make_gauss3d, {-1.0, -1.0, -1.0}, {1.0, 1.0, 1.0});
    sweep_nd<8, 3>(b, "3d_yukawa", make_yukawa3d, {0.2, 0.2, 0.2}, {1.5, 1.5, 1.5});
    sweep_nd<8, 3>(b, "3d_imq", make_imq3d, {-1.0, -1.0, -1.0}, {1.0, 1.0, 1.0});

    return 0;
}
