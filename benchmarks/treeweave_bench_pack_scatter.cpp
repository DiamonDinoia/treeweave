// Microbench for eval_pack<N> (compile-time-N pack eval, three regimes) and
// eval_scatter_sorted (counting-sort scatter). Each surface is compared with a
// hand-rolled scalar baseline; output is consumed by bench/compare_nb.py.

#define ANKERL_NANOBENCH_IMPLEMENT
#include <nanobench.h>

#include <treeweave/eval_scatter.hpp>
#include <treeweave/treeweave.hpp>

#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <random>
#include <span>
#include <string>
#include <vector>

#include <xsimd/xsimd.hpp>

// Deterministic seeds in microbench are intentional: bench/compare_nb.py
// diffs run-to-run and any RNG noise would mask real timing deltas.
// NOLINTBEGIN(cert-msc51-cpp,cert-msc32-c)
namespace {

auto make_runge1d() {
    return [](double x) { return 1.0 / (1.0 + 25.0 * x * x); };
}
auto make_tanh_sharp1d() {
    return [](double x) { return std::tanh(50.0 * x); };
}
auto make_erf1d() {
    return [](double x) { return std::erf(x); };
}
// Borderline-deep kernels: sharper tanh + tighter tol pushes max_depth
// past 14 (the current leaf-table build threshold), so find_leaf_id
// runs the descent fallback per call. Plan Phase 1 widens that
// threshold; these cases are the regression-trackable witness.
auto make_tanh500() {
    return [](double x) { return std::tanh(500.0 * x); };
}
auto make_tanh1000() {
    return [](double x) { return std::tanh(1000.0 * x); };
}

auto make_bench() {
    ankerl::nanobench::Bench b;
    b.title("treeweave pack/scatter")
        .unit("eval")
        .relative(true)
        .warmup(3)
        .minEpochIterations(1)
        .minEpochTime(std::chrono::milliseconds(20));
    return b;
}

// eval_pack<N> sweep. Drives the pack repeatedly so the outer loop
// amortises nanobench's epoch overhead while keeping eval_pack itself
// at compile-time-N. `n_packs` controls total work per call.
template <std::size_t Deg, std::size_t N, class FitT>
void bench_pack(ankerl::nanobench::Bench &b, const char *label, const FitT &fn, double lo, double hi,
                std::size_t n_packs) {
    std::mt19937                           gen(7 + Deg * 100 + N);
    std::uniform_real_distribution<double> d(lo + 1e-6, hi - 1e-6);

    // Pre-generate `n_packs` arrays of `N` inputs each (so the bench
    // loop is pure eval, no RNG hot in the hot path).
    std::vector<std::array<double, N>> packs(n_packs);
    for (auto &p : packs)
        for (auto &x : p)
            x = d(gen);

    const std::size_t total_evals = n_packs * N;

    {
        std::string name = std::string(label) + " eval_pack<" + std::to_string(N) + "> deg=" + std::to_string(Deg);
        b.batch(static_cast<double>(total_evals));
        b.run(name, [&] {
            double acc = 0.0;
            for (const auto &p : packs) {
                const auto ys = fn.eval_pack(p);
                for (double y : ys)
                    acc += y;
            }
            ankerl::nanobench::doNotOptimizeAway(acc);
        });
    }

    // (2) Baseline: hand-rolled scalar loop over the same packs.
    //     This is the apples-to-apples lower-bound the unrolled
    //     static_for fan-out has to beat.
    {
        std::string name = std::string(label) + " scalar_loop<" + std::to_string(N) + "> deg=" + std::to_string(Deg);
        b.batch(static_cast<double>(total_evals));
        b.run(name, [&] {
            double acc = 0.0;
            for (const auto &p : packs) {
                for (std::size_t i = 0; i < N; ++i)
                    acc += fn(p[i]);
            }
            ankerl::nanobench::doNotOptimizeAway(acc);
        });
    }
}

template <std::size_t Deg, class FmakeT>
void sweep_pack_1d(ankerl::nanobench::Bench &b, const char *label, FmakeT make_f, double lo, double hi,
                   double tol = 1e-10) {
    auto fn = treeweave::fit<Deg>(make_f(), lo, hi, tol);

    // Pack sizes span the small-N unrolled fan-out (1, 4, 8, 16), the
    // medium for-loop regime (32, 64, 128, 256), and the batch path
    // (1024). 4 and 8 match xsimd lane widths on AVX2 and AVX-512.
    constexpr std::size_t kPacks = 8192;
    bench_pack<Deg, 1>(b, label, fn, lo, hi, kPacks);
    bench_pack<Deg, 4>(b, label, fn, lo, hi, kPacks);
    bench_pack<Deg, 8>(b, label, fn, lo, hi, kPacks);
    bench_pack<Deg, 16>(b, label, fn, lo, hi, kPacks);
    bench_pack<Deg, 32>(b, label, fn, lo, hi, kPacks);
    bench_pack<Deg, 64>(b, label, fn, lo, hi, kPacks / 2);
    bench_pack<Deg, 128>(b, label, fn, lo, hi, kPacks / 4);
    bench_pack<Deg, 256>(b, label, fn, lo, hi, kPacks / 8);
    bench_pack<Deg, 1024>(b, label, fn, lo, hi, kPacks / 32);
}

// eval_scatter_sorted sweep: R independent 1D fits over a shared domain; n scattered (fit_id, x) pairs per call.
template <std::size_t Deg>
void sweep_scatter_1d(ankerl::nanobench::Bench &b, const char *label, std::size_t R) {
    // Use std::function so all fits share one type (eval_scatter_sorted
    // takes a span of like pointers).
    using ff = std::function<double(double)>;
    std::mt19937                           cgen(11);
    std::uniform_real_distribution<double> cd(-1.0, 1.0);

    // Build R distinct smooth fits over [0, 1], small random Chebyshev-series
    // polynomials so every fit is exactly representable by the leaf and
    // tree depth stays uniform across fits.
    std::vector<ff> exact;
    exact.reserve(R);
    for (std::size_t r = 0; r < R; ++r) {
        std::array<double, 5> ck{};
        for (auto &c : ck)
            c = cd(cgen);
        exact.emplace_back([ck](double x) {
            const double y  = 2.0 * x - 1.0;
            double       T0 = 1.0, T1 = y;
            double       s = ck[0] + ck[1] * y;
            for (std::size_t k = 2; k < ck.size(); ++k) {
                const double T = 2.0 * y * T1 - T0;
                s += ck[k] * T;
                T0 = T1;
                T1 = T;
            }
            return s;
        });
    }

    using fit_t = decltype(treeweave::fit<Deg>(std::declval<ff &>(), 0.0, 1.0, 1e-10));
    std::vector<fit_t> fits;
    fits.reserve(R);
    for (auto &f : exact)
        fits.push_back(treeweave::fit<Deg>(f, 0.0, 1.0, 1e-10));
    std::vector<const fit_t *> fit_ptrs;
    for (auto &fn : fits)
        fit_ptrs.push_back(&fn);

    std::mt19937                                 ig(static_cast<std::uint32_t>(13 + R));
    std::uniform_int_distribution<std::uint32_t> rd(0, static_cast<std::uint32_t>(R - 1));
    std::uniform_real_distribution<double>       td(1e-6, 1.0 - 1e-6);

    for (std::size_t n_pts : {std::size_t(64), std::size_t(256), std::size_t(1024)}) {
        std::vector<std::uint32_t> ids(n_pts);
        std::vector<double>        xs(n_pts);
        for (std::size_t i = 0; i < n_pts; ++i) {
            ids[i] = rd(ig);
            xs[i]  = td(ig);
        }
        std::vector<double> ys(n_pts);

        const std::string suffix =
            std::string(" deg=") + std::to_string(Deg) + " R=" + std::to_string(R) + " N=" + std::to_string(n_pts);

        {
            std::string name = std::string(label) + " scatter_counting" + suffix;
            b.batch(static_cast<double>(n_pts));
            b.run(name, [&] {
                treeweave::eval_scatter_sorted<Deg, ff>(std::span<const fit_t *const>{fit_ptrs.data(), fit_ptrs.size()},
                                                        std::span<const std::uint32_t>{ids.data(), ids.size()},
                                                        std::span<const double>{xs.data(), xs.size()},
                                                        std::span<double>{ys.data(), ys.size()},
                                                        static_cast<std::uint32_t>(R));
                ankerl::nanobench::doNotOptimizeAway(ys.front());
            });
        }

        // (2) Baseline: hand-rolled per-pair scalar dispatch (what
        //     bench_chebfun-style consumers do today).
        {
            std::string name = std::string(label) + " scatter_naive " + suffix;
            b.batch(static_cast<double>(n_pts));
            b.run(name, [&] {
                double acc = 0.0;
                for (std::size_t i = 0; i < n_pts; ++i)
                    acc += (*fit_ptrs[ids[i]])(xs[i]);
                ankerl::nanobench::doNotOptimizeAway(acc);
            });
        }
    }
}

} // namespace

int main() {
    std::printf("# treeweave pack/scatter bench (nanobench, xsimd lane_w=%zu)\n", xsimd::batch<double>::size);

    auto b = make_bench();

    // eval_pack<N> on three 1D kernels covering smooth / sigmoid /
    // near-singular regimes. Two degrees expose the FMA chain
    // length effect.
    sweep_pack_1d<8>(b, "1d_runge", make_runge1d, -1.0, 1.0);
    sweep_pack_1d<10>(b, "1d_runge", make_runge1d, -1.0, 1.0);
    sweep_pack_1d<8>(b, "1d_erf", make_erf1d, -3.0, 3.0);
    sweep_pack_1d<10>(b, "1d_tanh_sharp", make_tanh_sharp1d, -1.0, 1.0);
    // Borderline-deep cases: depth 15/16 under tol=1e-12, just past
    // the 14-bit leaf-table build threshold so find_leaf_id descends.
    sweep_pack_1d<6>(b, "1d_tanh500_deep", make_tanh500, -1.0, 1.0, 1e-12);
    sweep_pack_1d<8>(b, "1d_tanh1000_deep", make_tanh1000, -1.0, 1.0, 1e-12);

    // Scatter benches at the (R, N) shapes that match diagmc's
    // bench_chebfun Wick fill (R = number of fits, N = Wick-pair count
    // per call).
    sweep_scatter_1d<8>(b, "1d_scatter", /*R=*/16);
    sweep_scatter_1d<8>(b, "1d_scatter", /*R=*/64);

    // ND scalar-call shape: 256 scalar operator()(x) calls per epoch,
    // exercises the inlining of polyfit's evalCanonical into treeweave.
    // Probe asm showed 1 callq/eval in 2D and 2 in 3D before
    // TREEWEAVE_FLATTEN on operator(); 1 callq stays in both after.
    {
        auto bump2d = [](std::array<double, 2> x) -> std::array<double, 1> {
            return {std::exp(-100.0 * (x[0] - 0.5) * (x[0] - 0.5) - (x[1] - 0.5) * (x[1] - 0.5))};
        };
        auto gauss3d = [](std::array<double, 3> x) -> std::array<double, 1> {
            return {std::exp(-x[0] * x[0] - x[1] * x[1] - x[2] * x[2])};
        };
        auto fn2d = treeweave::fit<8>(bump2d, std::array{0.0, 0.0}, std::array{1.0, 1.0}, /*tol=*/1e-10);
        auto fn3d = treeweave::fit<8>(gauss3d, std::array{-1.0, -1.0, -1.0}, std::array{1.0, 1.0, 1.0}, /*tol=*/1e-10);

        constexpr std::size_t                  kN = 256;
        std::vector<std::array<double, 2>>     xs2(kN);
        std::vector<std::array<double, 3>>     xs3(kN);
        std::mt19937                           gen(99);
        std::uniform_real_distribution<double> d2(0.0 + 1e-3, 1.0 - 1e-3);
        std::uniform_real_distribution<double> d3(-1.0 + 1e-3, 1.0 - 1e-3);
        for (std::size_t i = 0; i < kN; ++i) {
            xs2[i] = {d2(gen), d2(gen)};
            xs3[i] = {d3(gen), d3(gen), d3(gen)};
        }
        b.batch(static_cast<double>(kN));
        b.run("2d_bump scalar_loop<256> deg=8", [&] {
            double acc = 0.0;
            for (std::size_t i = 0; i < kN; ++i)
                acc += fn2d(xs2[i])[0];
            ankerl::nanobench::doNotOptimizeAway(acc);
        });
        b.run("3d_gauss scalar_loop<256> deg=8", [&] {
            double acc = 0.0;
            for (std::size_t i = 0; i < kN; ++i)
                acc += fn3d(xs3[i])[0];
            ankerl::nanobench::doNotOptimizeAway(acc);
        });
    }

    return 0;
}
// NOLINTEND(cert-msc51-cpp,cert-msc32-c)
