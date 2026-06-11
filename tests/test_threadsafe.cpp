// Phase 8a — pin down the operator() thread-safety contract.
//
// Contract: a single Function built once and not mutated may be called
// concurrently from many threads provided each thread's xp/res slices do
// not overlap with another thread's.
//
// Test strategy: race-free behaviour is asserted by *bit-exact* equality
// of the threaded output across many repeats with the same chunking. Any
// race on shared state would surface as flapping bits between repeats.
//
// We also compare against a serial reference, but only within a tight
// relative tolerance: changing the chunking changes per-leaf counts,
// which changes the SIMD-batch vs scalar-tail mix in polyfit's
// `FuncEvalND::operator()` — a deterministic but path-dependent ~1 ULP
// drift. That drift is NOT a race; it is non-associative FP.

#include <algorithm>
#include <cstdint>
#include <treeweave/treeweave.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <latch>
#include <limits>
#include <random>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

constexpr std::size_t kN       = 65'536;
constexpr int         kThreads = 8;
constexpr int         kRepeats = 16;

auto make_bump2d() {
    return [](std::array<double, 2> x) -> std::array<double, 1> {
        return {std::exp(-100.0 * (x[0] - 0.5) * (x[0] - 0.5) - (x[1] - 0.5) * (x[1] - 0.5))};
    };
}
auto make_gauss3d() {
    return [](std::array<double, 3> x) -> std::array<double, 1> {
        return {std::exp(-x[0] * x[0] - x[1] * x[1] - x[2] * x[2])};
    };
}

template <std::size_t Dim>
auto make_inputs(std::array<double, Dim> a, std::array<double, Dim> b, std::size_t n, std::uint32_t seed)
    -> std::vector<double> {
    std::mt19937                           gen(seed);
    std::uniform_real_distribution<double> ud(0.0, 1.0);
    std::vector<double>                    flat(Dim * n);
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t d = 0; d < Dim; ++d)
            flat[Dim * i + d] = a[d] + (b[d] - a[d] - 1e-3) * ud(gen) + 5e-4;
    return flat;
}

template <std::size_t Dim, class Fn>
auto run_chunked(Fn &fn, const std::vector<double> &xp, std::size_t n) -> std::vector<double> {
    std::vector<double>      res(n, std::nan(""));
    std::latch               start(kThreads + 1);
    std::vector<std::thread> ts;
    ts.reserve(kThreads);
    for (std::size_t t = 0; t < std::size_t{kThreads}; ++t) {
        const std::size_t lo = (n * t) / kThreads;
        const std::size_t hi = (n * (t + 1)) / kThreads;
        ts.emplace_back([&, lo, hi] {
            start.arrive_and_wait();
            fn(xp.data() + Dim * lo, res.data() + lo, hi - lo);
        });
    }
    start.arrive_and_wait();
    for (auto &th : ts)
        th.join();
    return res;
}

template <std::size_t Dim, class Fn>
void run_threadsafe_check(Fn &fn, const std::vector<double> &xp, const std::vector<double> &res_ref) {
    const std::size_t n = res_ref.size();

    // First call establishes the canonical threaded output. Repeated calls
    // with the same chunking must match it bit-for-bit — that's how a race
    // would show up.
    auto res0 = run_chunked<Dim>(fn, xp, n);
    for (int rep = 1; rep < kRepeats; ++rep) {
        auto res = run_chunked<Dim>(fn, xp, n);
        REQUIRE(std::memcmp(res.data(), res0.data(), n * sizeof(double)) == 0);
    }

    // Threaded vs serial reference: chunking changes per-leaf cnts which
    // changes polyfit's SIMD-batch / scalar-tail mix. Deterministic but
    // path-dependent ~1 ULP drift — not a race. Bound it tightly.
    double max_abs = 0.0, max_rel = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double d = std::abs(res0[i] - res_ref[i]);
        max_abs        = std::max(max_abs, d);
        if (std::abs(res_ref[i]) > 1e-300)
            max_rel = std::max(max_rel, d / std::abs(res_ref[i]));
    }
    REQUIRE(max_rel < 1e-12);
}

// f32 helpers — separate from the f64 helpers to keep the template
// arithmetic in the correct precision.

auto make_inputs_1d_f32(float a, float b, std::size_t n, std::uint32_t seed) -> std::vector<float> {
    std::mt19937                          gen(seed);
    std::uniform_real_distribution<float> ud(0.0F, 1.0F);
    std::vector<float>                    flat(n);
    for (std::size_t i = 0; i < n; ++i)
        flat[i] = a + (b - a - 1e-3F) * ud(gen) + 5e-4F;
    return flat;
}

template <class Fn>
auto run_chunked_1d_f32(Fn &fn, const std::vector<float> &xp, std::size_t n) -> std::vector<float> {
    std::vector<float>       res(n, std::numeric_limits<float>::quiet_NaN());
    std::latch               start(kThreads + 1);
    std::vector<std::thread> ts;
    ts.reserve(kThreads);
    for (std::size_t t = 0; t < std::size_t{kThreads}; ++t) {
        const std::size_t lo = (n * t) / kThreads;
        const std::size_t hi = (n * (t + 1)) / kThreads;
        ts.emplace_back([&, lo, hi] {
            start.arrive_and_wait();
            fn(xp.data() + lo, res.data() + lo, hi - lo);
        });
    }
    start.arrive_and_wait();
    for (auto &th : ts)
        th.join();
    return res;
}

template <class Fn>
void run_threadsafe_check_1d_f32(Fn &fn, const std::vector<float> &xp, const std::vector<float> &res_ref) {
    const std::size_t n = res_ref.size();

    auto res0 = run_chunked_1d_f32(fn, xp, n);
    for (int rep = 1; rep < kRepeats; ++rep) {
        auto res = run_chunked_1d_f32(fn, xp, n);
        REQUIRE(std::memcmp(res.data(), res0.data(), n * sizeof(float)) == 0);
    }

    float max_rel = 0.0F;
    for (std::size_t i = 0; i < n; ++i) {
        const float d = std::abs(res0[i] - res_ref[i]);
        if (std::abs(res_ref[i]) > 1e-30F)
            max_rel = std::max(max_rel, d / std::abs(res_ref[i]));
    }
    // Allow a wider relative tolerance for f32 (single-precision FMA reordering
    // between chunked and serial paths can reach a few ULPs).
    REQUIRE(max_rel < 1e-4F);
}

} // namespace

TEST_CASE("operator() is thread-safe -- 2d_bump deg=8", "[treeweave][threadsafe]") {
    auto fn = treeweave::fit<8>(make_bump2d(), std::array<double, 2>{0.0, 0.0}, std::array<double, 2>{1.0, 1.0}, 1e-10);
    auto xp = make_inputs<2>({0.0, 0.0}, {1.0, 1.0}, kN, 11u);
    std::vector<double> res_ref(kN);
    fn(xp.data(), res_ref.data(), kN);
    run_threadsafe_check<2>(fn, xp, res_ref);
}

TEST_CASE("operator() is thread-safe -- 3d_gauss deg=8", "[treeweave][threadsafe]") {
    auto                fn = treeweave::fit<8>(make_gauss3d(), std::array<double, 3>{-1.0, -1.0, -1.0},
                                               std::array<double, 3>{1.0, 1.0, 1.0}, 1e-10);
    auto                xp = make_inputs<3>({-1.0, -1.0, -1.0}, {1.0, 1.0, 1.0}, kN, 13u);
    std::vector<double> res_ref(kN);
    fn(xp.data(), res_ref.data(), kN);
    run_threadsafe_check<3>(fn, xp, res_ref);
}

// TST1: add 1D f64 and 1D f32 thread-safety coverage (previously only 2D/3D
// f64 were exercised).

TEST_CASE("operator() is thread-safe -- 1d_sin deg=8", "[treeweave][threadsafe]") {
    // 1D scalar-input fit: exercises the 1D leaf-table quantize fast path and
    // the 1D batch counting-sort pipeline under concurrent access.
    auto fn = treeweave::fit<8>([](double x) { return std::sin(5.0 * x) + std::cos(3.0 * x); }, 0.0, 1.0, 1e-10);
    auto xp = make_inputs<1>({0.0}, {1.0}, kN, 17u);
    std::vector<double> res_ref(kN);
    fn(xp.data(), res_ref.data(), kN);
    run_threadsafe_check<1>(fn, xp, res_ref);
}

TEST_CASE("operator() is thread-safe -- 1d_f32 deg=7", "[treeweave][threadsafe][f32]") {
    // 1D f32 fit: same race-detection strategy as the f64 tests but in
    // single precision.  kRepeats bit-exact comparisons catch any data race
    // on shared coefficient storage; the relative-tolerance check against the
    // serial reference bounds legitimate FMA non-associativity.
    auto fn = treeweave::fit<7>([](float x) { return std::exp(0.5F * x) + std::sin(3.0F * x); }, 0.0F, 1.0F, 1e-5);
    auto xp = make_inputs_1d_f32(0.0F, 1.0F, kN, 19u);
    std::vector<float> res_ref(kN);
    fn(xp.data(), res_ref.data(), kN);
    run_threadsafe_check_1d_f32(fn, xp, res_ref);
}
