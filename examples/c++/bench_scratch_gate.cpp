// Zero-alloc-after-warmup regression guard.
//
// With the public Scratch API removed (Track A), the batch path routes
// through an internal thread_local scratch. This bench just measures the
// batch path across n in {64, ..., 65536} for 1D + 3D AoS — a regression
// against the historical numbers (see bench/baseline_*.txt) flags if the
// thread_local cache ever stops amortising the alloc cost.
//
// Pin to one core for stable numbers (e.g. `taskset -c 2`).

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

namespace {

auto make_runge1d() {
    return [](double x) { return 1.0 / (1.0 + 25.0 * x * x); };
}
auto make_gauss3d() {
    return [](std::array<double, 3> x) -> std::array<double, 1> {
        return {std::exp(-x[0] * x[0] - x[1] * x[1] - x[2] * x[2])};
    };
}

constexpr std::array<std::size_t, 6> kSweep = {64, 256, 1024, 4096, 16384, 65536};

template <class Fn>
void sweep_1d(ankerl::nanobench::Bench &b, const Fn &fn, double a, double b_) {
    std::mt19937                           gen(7);
    std::uniform_real_distribution<double> d(a + 1e-3, b_ - 1e-3);
    for (std::size_t n : kSweep) {
        std::vector<double> xs(n);
        for (auto &x : xs)
            x = d(gen);
        std::vector<double> out(n);
        b.batch(static_cast<double>(n));
        b.run("1d N=" + std::to_string(n), [&] {
            fn(xs.data(), out.data(), n);
            ankerl::nanobench::doNotOptimizeAway(out.front());
        });
    }
}

template <class Fn>
void sweep_3d(ankerl::nanobench::Bench &b, const Fn &fn, std::array<double, 3> a, std::array<double, 3> b_) {
    std::mt19937                           gen(7);
    std::uniform_real_distribution<double> ud(0.0, 1.0);
    auto pick = [&](std::size_t d) { return a[d] + (b_[d] - a[d] - 1e-3) * ud(gen) + 5e-4; };
    for (std::size_t n : kSweep) {
        std::vector<double> flat(3 * n);
        for (std::size_t i = 0; i < n; ++i)
            for (std::size_t d = 0; d < 3; ++d)
                flat[3 * i + d] = pick(d);
        std::vector<double> out(n);
        b.batch(static_cast<double>(n));
        b.run("3d N=" + std::to_string(n), [&] {
            fn(flat.data(), out.data(), n);
            ankerl::nanobench::doNotOptimizeAway(out.front());
        });
    }
}

} // namespace

int main() {
    ankerl::nanobench::Bench b;
    b.title("treeweave batch (AoS, thread_local scratch)")
        .unit("eval")
        .relative(true)
        .warmup(3)
        .minEpochIterations(1)
        .minEpochTime(std::chrono::milliseconds(30));

    treeweave::options opts;
    opts.max_memory_mib = 0;
    auto fn1d           = treeweave::fit<8>(make_runge1d(), -1.0, 1.0, 1e-10, opts);
    auto fn3d           = treeweave::fit<8>(make_gauss3d(), std::array<double, 3>{-1.0, -1.0, -1.0},
                                            std::array<double, 3>{1.0, 1.0, 1.0}, 1e-10, opts);

    sweep_1d(b, fn1d, -1.0, 1.0);
    sweep_3d(b, fn3d, {-1.0, -1.0, -1.0}, {1.0, 1.0, 1.0});

    return 0;
}
