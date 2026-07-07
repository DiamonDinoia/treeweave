// Google Benchmark twin of treeweave_ci_bench for CodSpeed.io (instruction-count); same case names, seeds, and N.
// (see devel/agents/build-notes.md)

#include <treeweave/treeweave.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <random>
#include <vector>

#include <benchmark/benchmark.h>

namespace {

constexpr std::size_t N = 1u << 16; // points per batch eval

template <class T = double>
auto random_points(std::size_t n, double lo, double hi, unsigned seed) -> std::vector<T> {
    std::mt19937                           gen(seed);
    std::uniform_real_distribution<double> d(lo, hi);
    std::vector<T>                         v(n);
    std::ranges::generate(v, [&] { return static_cast<T>(d(gen)); });
    return v;
}

// kDeep*: depth pins that spill the leaf table past L1/L2 (1D:14, 2D:8, 3D:5 — at/below 64K-entry cap).
// (see devel/agents/build-notes.md)
constexpr int kDeep1D = 14;
constexpr int kDeep2D = 8;
constexpr int kDeep3D = 5;

void eval_1d_runge_f64(benchmark::State &state) {
    auto fn  = treeweave::fit([](double x) { return 1.0 / (1.0 + 25.0 * x * x); }, -1.0, 1.0, /*tol=*/1e-10);
    auto xs  = random_points(N, -0.999, 0.999, 1);
    auto out = std::vector<double>(N);
    for (auto _ : state) {
        fn(xs.data(), out.data(), N);
        benchmark::DoNotOptimize(out.front());
    }
}

// 1D — Runge, deep uniform tree (large f64 leaf table; L1/L2-spilling lookup).
void eval_1d_runge_deep_f64(benchmark::State &state) {
    auto fn  = treeweave::fit([](double x) { return 1.0 / (1.0 + 25.0 * x * x); }, -1.0, 1.0, /*tol=*/1e-10,
                              treeweave::options{.min_uniform_depth = kDeep1D});
    auto xs  = random_points(N, -0.999, 0.999, 1);
    auto out = std::vector<double>(N);
    for (auto _ : state) {
        fn(xs.data(), out.data(), N);
        benchmark::DoNotOptimize(out.front());
    }
}

// 1D — Runge in f32, deep uniform tree (large f32 leaf table; the gather path).
void eval_1d_runge_deep_f32(benchmark::State &state) {
    auto fn  = treeweave::fit([](float x) { return 1.0F / (1.0F + 25.0F * x * x); }, -1.0F, 1.0F, /*tol=*/1e-6,
                              treeweave::options{.min_uniform_depth = kDeep1D});
    auto xs  = random_points<float>(N, -0.999, 0.999, 1);
    auto out = std::vector<float>(N);
    for (auto _ : state) {
        fn(xs.data(), out.data(), N);
        benchmark::DoNotOptimize(out.front());
    }
}

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

// 2D -> 1D — bump, deep uniform tree (large 2D leaf table).
void eval_2d_bump_deep_f64(benchmark::State &state) {
    auto fn = treeweave::fit(
        [](std::array<double, 2> x) -> std::array<double, 1> {
            return {std::exp(-100.0 * (x[0] - 0.5) * (x[0] - 0.5) - (x[1] - 0.5) * (x[1] - 0.5))};
        },
        std::array{0.0, 0.0}, std::array{1.0, 1.0}, /*tol=*/1e-8, treeweave::options{.min_uniform_depth = kDeep2D});
    auto flat = random_points(2 * N, 0.001, 0.999, 2);
    auto out  = std::vector<double>(N);
    for (auto _ : state) {
        fn(flat.data(), out.data(), N);
        benchmark::DoNotOptimize(out.front());
    }
}

// 3D -> 1D — smooth, deep uniform tree (large 3D leaf table).
void eval_3d_smooth_deep_f64(benchmark::State &state) {
    auto fn = treeweave::fit(
        [](std::array<double, 3> x) -> std::array<double, 1> {
            return {std::exp(0.3 * x[0]) + std::sin(2.0 * x[1]) + x[2] * x[2]};
        },
        std::array{0.0, 0.0, 0.0}, std::array{1.0, 1.0, 1.0}, /*tol=*/1e-7,
        treeweave::options{.min_uniform_depth = kDeep3D});
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
BENCHMARK(eval_1d_runge_deep_f64)->Name("eval/1d/runge-deep/f64");
BENCHMARK(eval_1d_runge_deep_f32)->Name("eval/1d/runge-deep/f32");
BENCHMARK(eval_2d_bump_f64)->Name("eval/2d/bump/f64");
BENCHMARK(eval_2d_bump_deep_f64)->Name("eval/2d/bump-deep/f64");
BENCHMARK(eval_3d_smooth_f64)->Name("eval/3d/smooth/f64");
BENCHMARK(eval_3d_smooth_deep_f64)->Name("eval/3d/smooth-deep/f64");
BENCHMARK(eval_2d_to_3d_vector_f64)->Name("eval/2d->3d/vector/f64");

BENCHMARK_MAIN();
