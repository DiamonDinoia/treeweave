// treeweave_codspeed_bench — the CI regression bench rendered for CodSpeed.io.
//
// This is the Google Benchmark twin of treeweave_ci_bench (nanobench). CodSpeed
// C++ supports only Google Benchmark, so we mirror the exact same four
// representative batch-eval cases here — same case names, seeds, and N = 1<<16
// batch — so the CodSpeed (instruction-count) dashboard lines up with the
// nanobench → gh-pages (wall-time) dashboard. The two are complementary views
// of the same hot path; keep the cases in sync when either changes.
//
// CodSpeed runs this binary once under a simulated CPU (CODSPEED_MODE=simulation,
// passed by codspeed.yml); locally it is a plain Google Benchmark. As in the
// nanobench bench, each treeweave::fit(...) is done once outside the timed loop
// and only the batch eval is measured.

#include <treeweave/treeweave.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <random>
#include <vector>

#include <benchmark/benchmark.h>

namespace {

constexpr std::size_t N = 1u << 16; // points per batch eval

std::vector<double> random_points(std::size_t n, double lo, double hi, unsigned seed) {
    std::mt19937                           gen(seed);
    std::uniform_real_distribution<double> d(lo, hi);
    std::vector<double>                    v(n);
    for (auto &x : v)
        x = d(gen);
    return v;
}

// 1D — Runge function.
void eval_1d_runge_f64(benchmark::State &state) {
    auto fn  = treeweave::fit([](double x) { return 1.0 / (1.0 + 25.0 * x * x); }, -1.0, 1.0, /*tol=*/1e-10);
    auto xs  = random_points(N, -0.999, 0.999, 1);
    auto out = std::vector<double>(N);
    for (auto _ : state) {
        fn(xs.data(), out.data(), N);
        benchmark::DoNotOptimize(out.front());
    }
}

// 2D -> 1D — Gaussian bump.
void eval_2d_bump_f64(benchmark::State &state) {
    auto fn = treeweave::fit(
        [](std::array<double, 2> x) -> std::array<double, 1> {
            return {std::exp(-100.0 * (x[0] - 0.5) * (x[0] - 0.5) - (x[1] - 0.5) * (x[1] - 0.5))};
        },
        std::array{0.0, 0.0}, std::array{1.0, 1.0}, /*tol=*/1e-8);
    auto flat = random_points(2 * N, 0.001, 0.999, 2);
    auto out  = std::vector<double>(N);
    for (auto _ : state) {
        fn(flat.data(), out.data(), N);
        benchmark::DoNotOptimize(out.front());
    }
}

// 3D -> 1D — smooth product.
void eval_3d_smooth_f64(benchmark::State &state) {
    auto fn = treeweave::fit(
        [](std::array<double, 3> x) -> std::array<double, 1> {
            return {std::exp(0.3 * x[0]) + std::sin(2.0 * x[1]) + x[2] * x[2]};
        },
        std::array{0.0, 0.0, 0.0}, std::array{1.0, 1.0, 1.0}, /*tol=*/1e-7);
    auto flat = random_points(3 * N, 0.001, 0.999, 3);
    auto out  = std::vector<double>(N);
    for (auto _ : state) {
        fn(flat.data(), out.data(), N);
        benchmark::DoNotOptimize(out.front());
    }
}

// 2D -> 3D — vector-valued output.
void eval_2d_to_3d_vector_f64(benchmark::State &state) {
    auto fn = treeweave::fit(
        [](std::array<double, 2> x) -> std::array<double, 3> {
            return {std::exp(0.3 * x[0]) + std::sin(2.0 * x[1]), std::cos(x[0] * x[1]) + 2.0, x[0] * x[0] + x[1] + 1.0};
        },
        std::array{0.0, 0.0}, std::array{1.0, 1.0}, /*tol=*/1e-7);
    auto flat = random_points(2 * N, 0.001, 0.999, 4);
    auto out  = std::vector<double>(3 * N);
    for (auto _ : state) {
        fn(flat.data(), out.data(), N);
        benchmark::DoNotOptimize(out.front());
    }
}

} // namespace

// Case names mirror treeweave_ci_bench exactly so the two dashboards align.
BENCHMARK(eval_1d_runge_f64)->Name("eval/1d/runge/f64");
BENCHMARK(eval_2d_bump_f64)->Name("eval/2d/bump/f64");
BENCHMARK(eval_3d_smooth_f64)->Name("eval/3d/smooth/f64");
BENCHMARK(eval_2d_to_3d_vector_f64)->Name("eval/2d->3d/vector/f64");

BENCHMARK_MAIN();
