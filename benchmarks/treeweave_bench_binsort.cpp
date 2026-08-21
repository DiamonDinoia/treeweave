// Bin-sort optimization harness: Phase 1 (f32 int32 quantize) and Phase 2 (descent leaf-id
// materialization) shipped; Phase 3 (radix) reverted 2–5× slower on 2 MiB-L2.

// The per-phase split uses the x86 cycle counter (__rdtsc), so this benchmark
// is x86-only; on other architectures it compiles to a no-op skip.
#if !defined(__x86_64__) && !defined(_M_X64) && !defined(__i386__) && !defined(_M_IX86)
#include <cstdio>
int main() {
    std::puts("treeweave_bench_binsort is x86-only (uses __rdtsc); skipped on this architecture.");
    return 0;
}
#else

#define TREEWEAVE_BENCH_PARTITION_HOOK

#define ANKERL_NANOBENCH_IMPLEMENT
#include <nanobench.h>

#include <treeweave/treeweave.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

#include <xsimd/xsimd.hpp>

// NOLINTBEGIN(cert-msc51-cpp,cert-msc32-c)
namespace {

// Low degree so the per-leaf Horner eval is cheap and the bin sort dominates
// the full-throughput number. Smooth kernel so `min_uniform_depth` produces an
// exactly-uniform 2^depth-leaf tree (no extra adaptive refinement).
constexpr std::size_t kDeg = 3;

// One tile's worth of points for the per-phase split. The count matches the
// batch path's default tile cap, so `counts[]` sees its real per-tile footprint.
constexpr std::size_t kTileN = 65536;
constexpr std::size_t kFullN = 1u << 20;

// Depth sweep -> n_leaves = 2^depth in 1D. 4..16 walks counts[] from 64 B
// (L1) to 256 KiB (L2/thrash).
constexpr std::array<int, 7> kDepths = {4, 6, 8, 10, 12, 14, 16};

template <class T>
auto runge() {
    return [](T x) { return T{1} / (T{1} + T{25} * x * x); };
}

template <class T>
auto build(int depth) {
    treeweave::options opts;
    opts.max_memory_mib    = 0;     // deep uniform trees blow the 4 MiB guard
    opts.min_uniform_depth = depth; // force exactly 2^depth leaves (1D)
    // Loose tol: the uniform refinement already satisfies a smooth kernel, so
    // the tree stays exactly uniform at `depth` (no extra adaptive splits).
    return treeweave::fit<kDeg>(runge<T>(), T{-1}, T{1}, /*tol=*/1e-4, opts);
}

template <class T>
void run_dtype(const char *tag) {
    std::mt19937                           gen(7);
    std::uniform_real_distribution<double> d(-1.0 + 1e-6, 1.0 - 1e-6);

    std::vector<T> xfull(kFullN);
    for (auto &x : xfull)
        x = static_cast<T>(d(gen));
    std::vector<T> resfull(kFullN);

    std::printf("\n# dtype=%s  (xsimd lane_w=%zu)\n", tag, xsimd::batch<T>::size);
    std::printf("# %-7s %-9s %-9s | per-phase cyc/pt (tile=%zu)            "
                "| full MEvals/s (N=%zu)\n",
                "depth", "n_leaves", "fastq", kTileN, kFullN);
    std::printf("# %-7s %-9s %-9s | %-10s %-10s %-10s |\n", "", "", "", "quantize", "hist", "scatter");

    for (int depth : kDepths) {
        auto              fn    = build<T>(depth);
        const std::size_t nl    = fn.num_leaves();
        const bool        fastq = fn.has_fast_quantize();

        // (1) Per-phase split over one tile. reps chosen so total work is
        //     ~tens of millions of points -> stable cycle medians.
        const std::size_t reps  = std::max<std::size_t>(64, (32u << 20) / kTileN);
        const auto        cyc   = fn.bench_partition_phases(xfull.data(), kTileN, reps);
        const double      denom = static_cast<double>(reps) * static_cast<double>(kTileN);
        const double      q_cyc = static_cast<double>(cyc[0]) / denom;
        // hist/scatter passes include the quantize; subtract to isolate.
        const double h_cyc = (static_cast<double>(cyc[1]) / denom) - q_cyc;
        const double s_cyc = (static_cast<double>(cyc[2]) / denom) - q_cyc;

        // (2) Full unsorted-batch throughput, manual median over a handful
        //     of timed reps (robust under the powersave governor; the median
        //     rejects the occasional frequency-step outlier).
        fn(xfull.data(), resfull.data(), kFullN); // warm
        std::array<double, 7> ms{};
        for (double &t : ms) {
            const auto t0 = std::chrono::steady_clock::now();
            fn(xfull.data(), resfull.data(), kFullN);
            const auto t1 = std::chrono::steady_clock::now();
            ankerl::nanobench::doNotOptimizeAway(resfull.front());
            t = std::chrono::duration<double>(t1 - t0).count();
        }
        std::sort(ms.begin(), ms.end());
        const double sec    = ms[ms.size() / 2];
        const double mevals = (sec > 0.0) ? (static_cast<double>(kFullN) / sec / 1e6) : 0.0;

        std::printf("  %-7d %-9zu %-9s | %-10.3f %-10.3f %-10.3f | %.2f\n", depth, nl, fastq ? "yes" : "no", q_cyc,
                    h_cyc, s_cyc, mevals);
    }
}

// run_descent: times unsorted batch on no-leaf-table fits (depth > 16 -> descent fallback, double tree-walk cost).
template <class T>
void run_descent(const char *tag) {
    std::mt19937                           gen(7);
    std::uniform_real_distribution<double> d(-1.0 + 1e-6, 1.0 - 1e-6);
    std::vector<T>                         xfull(kFullN);
    for (auto &x : xfull)
        x = static_cast<T>(d(gen));
    std::vector<T> resfull(kFullN);

    std::printf("\n# descent-fallback (!table)  dtype=%s\n", tag);
    std::printf("# %-7s %-9s %-9s | full MEvals/s (N=%zu)\n", "depth", "n_leaves", "fastq", kFullN);
    for (int depth : {17, 18}) {
        treeweave::options opts;
        opts.max_memory_mib     = 0;
        opts.min_uniform_depth  = depth; // > 16 -> no leaf table -> descent path
        auto              fn    = treeweave::fit<kDeg>(runge<T>(), T{-1}, T{1}, /*tol=*/1e-4, opts);
        const std::size_t nl    = fn.num_leaves();
        const bool        fastq = fn.has_fast_quantize(); // expect false

        fn(xfull.data(), resfull.data(), kFullN); // warm
        std::array<double, 7> ms{};
        for (double &t : ms) {
            const auto t0 = std::chrono::steady_clock::now();
            fn(xfull.data(), resfull.data(), kFullN);
            const auto t1 = std::chrono::steady_clock::now();
            ankerl::nanobench::doNotOptimizeAway(resfull.front());
            t = std::chrono::duration<double>(t1 - t0).count();
        }
        std::sort(ms.begin(), ms.end());
        const double sec    = ms[ms.size() / 2];
        const double mevals = (sec > 0.0) ? (static_cast<double>(kFullN) / sec / 1e6) : 0.0;
        std::printf("  %-7d %-9zu %-9s | %.2f\n", depth, nl, fastq ? "yes" : "no", mevals);
    }
}

} // namespace

int main() {
    std::printf("# treeweave bin-sort Phase 0 harness\n");
    run_dtype<double>("f64");
    run_dtype<float>("f32");
    run_descent<double>("f64");
    run_descent<float>("f32");
    return 0;
}
// NOLINTEND(cert-msc51-cpp,cert-msc32-c)

#endif // x86-only guard
