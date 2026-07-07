// CI regression bench: fixed representative batch-eval cases rendered as JSON for
// benchmark-action/github-action-benchmark (customSmallerIsBetter).

#include <treeweave/treeweave.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

#include <nanobench.h>

namespace {

// JSON template: value = median wall time per batch call; mustache lacks arithmetic so per-call time is tracked
// directly.
constexpr char kGabJsonTemplate[] = R"TPL([
{{#result}}  {
    "name": "{{name}}",
    "unit": "s/batch",
    "value": {{median(elapsed)}},
    "extra": "MdAPE={{medianAbsolutePercentError(elapsed)}}; batch={{batch}} pts/call"
  }{{^-last}},{{/-last}}
{{/result}}
])TPL";

template <class T = double>
auto random_points(std::size_t n, double lo, double hi, unsigned seed) -> std::vector<T> {
    std::mt19937                           gen(seed);
    std::uniform_real_distribution<double> d(lo, hi);
    std::vector<T>                         v(n);
    std::ranges::generate(v, [&] { return static_cast<T>(d(gen)); });
    return v;
}

// kDeep*: depth pins that spill the leaf table past L1/L2 (1D:14=16384, 2D:8=65536, 3D:5=32768 entries, <= 64K cap).
constexpr int kDeep1D = 14;
constexpr int kDeep2D = 8;
constexpr int kDeep3D = 5;

} // namespace

int main(int argc, char **argv) {
    constexpr std::size_t N = 1u << 16; // points per batch eval

    ankerl::nanobench::Bench bench;
    bench.title("treeweave batch eval").unit("eval").batch(static_cast<double>(N)).relative(true).minEpochIterations(8);

    {
        auto fn  = treeweave::fit([](double x) { return 1.0 / (1.0 + 25.0 * x * x); }, -1.0, 1.0, /*tol=*/1e-10);
        auto xs  = random_points(N, -0.999, 0.999, 1);
        auto out = std::vector<double>(N);
        bench.run("eval/1d/runge/f64", [&] {
            fn(xs.data(), out.data(), N);
            ankerl::nanobench::doNotOptimizeAway(out.front());
        });
    }

    // 1D — Runge, deep uniform tree (large f64 leaf table; L1/L2-spilling lookup).
    {
        auto fn  = treeweave::fit([](double x) { return 1.0 / (1.0 + 25.0 * x * x); }, -1.0, 1.0, /*tol=*/1e-10,
                                  treeweave::options{.min_uniform_depth = kDeep1D});
        auto xs  = random_points(N, -0.999, 0.999, 1);
        auto out = std::vector<double>(N);
        bench.run("eval/1d/runge-deep/f64", [&] {
            fn(xs.data(), out.data(), N);
            ankerl::nanobench::doNotOptimizeAway(out.front());
        });
    }

    // 1D — Runge in f32, deep uniform tree (large f32 leaf table; the gather path).
    {
        auto fn  = treeweave::fit([](float x) { return 1.0F / (1.0F + 25.0F * x * x); }, -1.0F, 1.0F, /*tol=*/1e-6,
                                  treeweave::options{.min_uniform_depth = kDeep1D});
        auto xs  = random_points<float>(N, -0.999, 0.999, 1);
        auto out = std::vector<float>(N);
        bench.run("eval/1d/runge-deep/f32", [&] {
            fn(xs.data(), out.data(), N);
            ankerl::nanobench::doNotOptimizeAway(out.front());
        });
    }

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

    // 2D -> 1D — bump, deep uniform tree (large 2D leaf table).
    {
        auto fn = treeweave::fit(
            [](std::array<double, 2> x) -> std::array<double, 1> {
                return {std::exp(-100.0 * (x[0] - 0.5) * (x[0] - 0.5) - (x[1] - 0.5) * (x[1] - 0.5))};
            },
            std::array{0.0, 0.0}, std::array{1.0, 1.0}, /*tol=*/1e-8, treeweave::options{.min_uniform_depth = kDeep2D});
        auto flat = random_points(2 * N, 0.001, 0.999, 2);
        auto out  = std::vector<double>(N);
        bench.run("eval/2d/bump-deep/f64", [&] {
            fn(flat.data(), out.data(), N);
            ankerl::nanobench::doNotOptimizeAway(out.front());
        });
    }

    // 3D -> 1D — smooth, deep uniform tree (large 3D leaf table).
    {
        auto fn = treeweave::fit(
            [](std::array<double, 3> x) -> std::array<double, 1> {
                return {std::exp(0.3 * x[0]) + std::sin(2.0 * x[1]) + x[2] * x[2]};
            },
            std::array{0.0, 0.0, 0.0}, std::array{1.0, 1.0, 1.0}, /*tol=*/1e-7,
            treeweave::options{.min_uniform_depth = kDeep3D});
        auto flat = random_points(3 * N, 0.001, 0.999, 3);
        auto out  = std::vector<double>(N);
        bench.run("eval/3d/smooth-deep/f64", [&] {
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

    // --- Scalar operator()(x) cases -------------------------------------
    // Per-point eval: the path per-point callers (ODE RHS, etc.) take. Unlike
    // the batch cases above it exercises `Function::operator()` (function.hpp),
    // which always runs get_linear_bin -> find_leaf_id (two quantizes) even when
    // there is a single subtree — the redundant outer quantize that the batch
    // paths skip via their `subtrees_.size() == 1` fast path.
    //
    // Note on multi-scale: a downstream proposal (imwofx) reported a two-*subtree*
    // regression at near-eps tol (~1e-15). Empirically the current builder yields
    // 1 subtree for every practical (degree, tol) — >1 subtree needs a near-eps
    // fit that overruns the memory budget at the default degree, so it is not a
    // CI-stable case. These cases pin the common 1-subtree scalar cost instead;
    // the printed subtrees/leaf-table line makes any future split visible.
    auto scalar_case = [&](const char *name, const auto &fn, const auto &xs) {
        using T = std::remove_cvref_t<decltype(xs[0])>;
        bench.batch(static_cast<double>(xs.size())).run(name, [&] {
            T acc{};
            for (auto x : xs)
                acc += fn(x);
            ankerl::nanobench::doNotOptimizeAway(acc);
        });
        // §6 diagnostics: subtree count + leaf-table size. print_stats() writes
        // to cout; redirect to cerr so the stdout JSON stays clean when no
        // output path is given.
        std::cerr << "[" << name << "]\n";
        auto *old = std::cout.rdbuf(std::cerr.rdbuf());
        fn.print_stats();
        std::cout.rdbuf(old);
    };

    {
        auto runge = [](double x) { return 1.0 / (1.0 + 25.0 * x * x); };
        auto fn    = treeweave::fit(runge, -1.0, 1.0, /*tol=*/1e-10);
        auto xs    = random_points(N, -0.999, 0.999, 5);
        scalar_case("eval-scalar/1d/runge/f64", fn, xs);
    }
    {
        // Deep uniform tree -> large (L1/L2-spilling) leaf table.
        auto runge = [](double x) { return 1.0 / (1.0 + 25.0 * x * x); };
        auto fn    = treeweave::fit(runge, -1.0, 1.0, /*tol=*/1e-10, treeweave::options{.min_uniform_depth = kDeep1D});
        auto xs    = random_points(N, -0.999, 0.999, 5);
        scalar_case("eval-scalar/1d/runge-deep/f64", fn, xs);
    }
    {
        auto runge = [](float x) { return 1.0F / (1.0F + 25.0F * x * x); };
        auto fn    = treeweave::fit(runge, -1.0F, 1.0F, /*tol=*/1e-6, treeweave::options{.min_uniform_depth = kDeep1D});
        auto xs    = random_points<float>(N, -0.999, 0.999, 5);
        scalar_case("eval-scalar/1d/runge-deep/f32", fn, xs);
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
