// treeweave_ci_bench — a small, stable benchmark for continuous regression
// tracking in CI. Unlike the exploratory benches in this directory, it runs a
// fixed handful of representative batch-eval cases (the production hot path)
// and renders the results as the JSON that
// benchmark-action/github-action-benchmark consumes in `customSmallerIsBetter`
// mode: a flat array of {name, unit, value} where value is the wall time of one
// batch-eval call (smaller is better). Pass an output path as argv[1] (default:
// stdout).
//
//   treeweave_ci_bench bench.json
//
// Keep the case set stable across commits so the published history stays
// comparable; add cases rather than renaming, since the name is the series key.

#include <treeweave/treeweave.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include <nanobench.h>

namespace {

// github-action-benchmark customSmallerIsBetter: one object per nanobench
// result. `value` is the median wall time of one batch-eval call (nanobench's
// per-iteration `elapsed`, in seconds); each call evaluates kBatch points, so
// the per-point cost is value/kBatch. We track the per-call time directly —
// mustache has no arithmetic, and the constant kBatch factor leaves the trend
// unchanged. MdAPE (measurement noise) rides along in `extra`.
constexpr char kGabJsonTemplate[] = R"TPL([
{{#result}}  {
    "name": "{{name}}",
    "unit": "s/batch",
    "value": {{median(elapsed)}},
    "extra": "MdAPE={{medianAbsolutePercentError(elapsed)}}; batch={{batch}} pts/call"
  }{{^-last}},{{/-last}}
{{/result}}
])TPL";

std::vector<double> random_points(std::size_t n, double lo, double hi, unsigned seed) {
    std::mt19937                           gen(seed);
    std::uniform_real_distribution<double> d(lo, hi);
    std::vector<double>                    v(n);
    for (auto &x : v)
        x = d(gen);
    return v;
}

} // namespace

int main(int argc, char **argv) {
    constexpr std::size_t N = 1u << 16; // points per batch eval

    ankerl::nanobench::Bench bench;
    bench.title("treeweave batch eval").unit("eval").batch(static_cast<double>(N)).relative(true).minEpochIterations(8);

    // 1D — Runge function.
    {
        auto fn  = treeweave::fit([](double x) { return 1.0 / (1.0 + 25.0 * x * x); }, -1.0, 1.0, /*tol=*/1e-10);
        auto xs  = random_points(N, -0.999, 0.999, 1);
        auto out = std::vector<double>(N);
        bench.run("eval/1d/runge/f64", [&] {
            fn(xs.data(), out.data(), N);
            ankerl::nanobench::doNotOptimizeAway(out.front());
        });
    }

    // 2D -> 1D — Gaussian bump.
    {
        auto fn = treeweave::fit(
            [](std::array<double, 2> x) -> std::array<double, 1> {
                return {std::exp(-100.0 * (x[0] - 0.5) * (x[0] - 0.5) - (x[1] - 0.5) * (x[1] - 0.5))};
            },
            std::array{0.0, 0.0}, std::array{1.0, 1.0}, /*tol=*/1e-8);
        auto flat = random_points(2 * N, 0.001, 0.999, 2);
        auto out  = std::vector<double>(N);
        bench.run("eval/2d/bump/f64", [&] {
            fn(flat.data(), out.data(), N);
            ankerl::nanobench::doNotOptimizeAway(out.front());
        });
    }

    // 3D -> 1D — smooth product.
    {
        auto fn = treeweave::fit(
            [](std::array<double, 3> x) -> std::array<double, 1> {
                return {std::exp(0.3 * x[0]) + std::sin(2.0 * x[1]) + x[2] * x[2]};
            },
            std::array{0.0, 0.0, 0.0}, std::array{1.0, 1.0, 1.0}, /*tol=*/1e-7);
        auto flat = random_points(3 * N, 0.001, 0.999, 3);
        auto out  = std::vector<double>(N);
        bench.run("eval/3d/smooth/f64", [&] {
            fn(flat.data(), out.data(), N);
            ankerl::nanobench::doNotOptimizeAway(out.front());
        });
    }

    // 2D -> 3D — vector-valued output.
    {
        auto fn = treeweave::fit(
            [](std::array<double, 2> x) -> std::array<double, 3> {
                return {std::exp(0.3 * x[0]) + std::sin(2.0 * x[1]), std::cos(x[0] * x[1]) + 2.0,
                        x[0] * x[0] + x[1] + 1.0};
            },
            std::array{0.0, 0.0}, std::array{1.0, 1.0}, /*tol=*/1e-7);
        auto flat = random_points(2 * N, 0.001, 0.999, 4);
        auto out  = std::vector<double>(3 * N);
        bench.run("eval/2d->3d/vector/f64", [&] {
            fn(flat.data(), out.data(), N);
            ankerl::nanobench::doNotOptimizeAway(out.front());
        });
    }

    std::string json;
    {
        std::ostringstream os;
        bench.render(kGabJsonTemplate, os);
        json = os.str();
    }

    if (argc > 1) {
        std::ofstream f(argv[1]);
        f << json;
        std::cout << "wrote " << argv[1] << "\n";
    } else {
        std::cout << json;
    }
    return 0;
}
