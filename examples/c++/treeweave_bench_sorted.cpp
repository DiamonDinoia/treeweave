// Focused A/B microbench for the 1D sorted-input batch path:
//   Function::sorted(const value_type* xp, value_type* res, size_t n)
//
// `sorted()` is the perf-critical presorted-1D surface (the unsorted batch
// `operator()(xp,res,n)` is covered by treeweave_microbench / pack_scatter).
// No other bench exercises it, so this is its canonical A/B harness — used
// to decide whether sharing the AoS/SoA `sorted` scan skeleton is
// perf-neutral.
//
// Stable measurement on a powersave/turbo machine: pin to a P-core and warm
// it first so the governor has ramped to its sustained ceiling before timing.
// High `minEpochTime` keeps each cell's MdAPE < ~1%.
//
//   taskset -c 2 ./treeweave_bench_sorted > /tmp/sorted_after.txt
//   ./bench/compare_nb.py /tmp/sorted_before.txt /tmp/sorted_after.txt

#define ANKERL_NANOBENCH_IMPLEMENT
#include <nanobench.h>

#include <treeweave/treeweave.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

#include <xsimd/xsimd.hpp>

// NOLINTBEGIN(cert-msc51-cpp,cert-msc32-c)
namespace {

auto make_runge1d() {
    return [](double x) { return 1.0 / (1.0 + 25.0 * x * x); };
}
auto make_tanh_sharp1d() {
    return [](double x) { return std::tanh(50.0 * x); };
}
// Sharp tanh + tight tol pushes max_depth past the leaf-table build
// threshold, so find_leaf_id runs the per-call descent fallback — the
// slower of the two `sorted` regimes.
auto make_tanh1000() {
    return [](double x) { return std::tanh(1000.0 * x); };
}

auto make_bench() {
    ankerl::nanobench::Bench b;
    b.title("treeweave sorted")
        .unit("eval")
        .relative(true)
        .warmup(10)
        .minEpochIterations(1)
        // Long epochs so small-N cells accumulate enough iterations to settle
        // below ~1% MdAPE despite powersave frequency jitter.
        .minEpochTime(std::chrono::milliseconds(300));
    return b;
}

// Busy-spin to ramp the pinned P-core to its sustained frequency before any
// measured epoch (powersave starts cores low; the first cell would otherwise
// time a ramping clock).
void warm_core() {
    double     acc = 0.0;
    const auto t0  = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(300))
        for (int i = 0; i < 100000; ++i)
            acc += std::sqrt(static_cast<double>(i) + 1.0);
    ankerl::nanobench::doNotOptimizeAway(acc);
}

template <std::size_t Deg, class FmakeT>
void sweep_sorted_1d(ankerl::nanobench::Bench &b, const char *label, FmakeT make_f, double lo, double hi,
                     double tol = 1e-10) {
    auto fn = treeweave::fit<Deg>(make_f(), lo, hi, tol);

    std::mt19937                           gen(7 + Deg * 100);
    std::uniform_real_distribution<double> d(lo + 1e-6, hi - 1e-6);

    for (std::size_t n : {std::size_t(32), std::size_t(1024), std::size_t(1'000'000)}) {
        std::vector<double> xs(n);
        for (auto &x : xs)
            x = d(gen);
        std::sort(xs.begin(), xs.end()); // sorted() requires ascending input
        std::vector<double> res(n);

        // N=1e6 cells are long per call; cap epoch time so the sweep stays short.
        if (n >= 1'000'000)
            b.minEpochTime(std::chrono::milliseconds(100));
        else
            b.minEpochTime(std::chrono::milliseconds(300));

        std::string name = std::string(label) + " sorted deg=" + std::to_string(Deg) + " N=" + std::to_string(n);
        b.batch(static_cast<double>(n));
        b.run(name, [&] {
            fn.sorted(xs.data(), res.data(), n);
            ankerl::nanobench::doNotOptimizeAway(res.front());
        });
    }
}

} // namespace

int main() {
    std::printf("# treeweave sorted bench (nanobench, xsimd lane_w=%zu)\n", xsimd::batch<double>::size);
    warm_core();

    auto b = make_bench();
    // Leaf-table fast-path regime.
    sweep_sorted_1d<8>(b, "1d_runge", make_runge1d, -1.0, 1.0);
    sweep_sorted_1d<10>(b, "1d_tanh_sharp", make_tanh_sharp1d, -1.0, 1.0);
    // Descent-fallback regime (depth past the leaf-table threshold).
    sweep_sorted_1d<8>(b, "1d_tanh1000_deep", make_tanh1000, -1.0, 1.0, 1e-12);

    return 0;
}
// NOLINTEND(cert-msc51-cpp,cert-msc32-c)
