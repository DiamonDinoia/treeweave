#include "treeweave/detail/errors.hpp"
#include "treeweave/detail/tol_kind.hpp"
#include <algorithm>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <treeweave/eval_scatter.hpp>
#include <treeweave/treeweave.hpp>

#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <functional>
#include <iostream>
#include <numbers>
#include <random>
#include <span>
#include <sstream>
#include <type_traits>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using treeweave::fit;
using treeweave::options;

// Deterministic seeds in tests are intentional: we want reproducible inputs
// for max-rel-error sweeps.
// NOLINTBEGIN(cert-msc51-cpp,cert-msc32-c)
namespace {
constexpr int N_SAMPLE = 5000;

template <class F1, class F2>
auto max_rel_err_1d(F1 &&exact, F2 &&approx, double a, double b, int n) -> double {
    std::mt19937                           gen(1);
    std::uniform_real_distribution<double> d(a, b);
    double                                 mx = 0.0;
    for (int i = 0; i < n; ++i) {
        const double x  = d(gen);
        const double y  = exact(x);
        const double yh = approx(x);
        if (std::abs(y) > 1e-12)
            mx = std::max(mx, std::abs((y - yh) / y));
    }
    return mx;
}

template <class F1, class F2>
auto max_rel_err_2d(F1 &&exact, F2 &&approx, std::array<double, 2> a, std::array<double, 2> b, int n) -> double {
    std::mt19937                           gen(1);
    std::uniform_real_distribution<double> dx(a[0], b[0]);
    std::uniform_real_distribution<double> dy(a[1], b[1]);
    double                                 mx = 0.0;
    for (int i = 0; i < n; ++i) {
        std::array<double, 2> const x{dx(gen), dy(gen)};
        const double                y  = exact(x);
        const double                yh = approx(x);
        if (std::abs(y) > 1e-12)
            mx = std::max(mx, std::abs((y - yh) / y));
    }
    return mx;
}
} // namespace

TEST_CASE("1D smooth sin on [0, 2pi], compile-time degree 8", "[treeweave][smooth]") {
    auto         f = [](double x) { return std::sin(5.0 * x); };
    const double a = 0.0;
    const double b = 2.0 * std::numbers::pi_v<double>;

    auto fn = fit<8>(f, a, b, /*tol=*/1e-10, options{.tol_kind = treeweave::TolKind::RelativeMax});
    // Narrow the check away from the very boundary where the fit is
    // approximate; single-point boundary eval is still fine.
    REQUIRE(max_rel_err_1d(f, fn, a + 1e-6, b - 1e-6, N_SAMPLE) < 1e-6);
}

TEST_CASE("1D tail-tolerance convergence (AbsoluteTail)", "[treeweave][smooth][tail]") {
    // Exercises the 1D-only tail-coefficient error path (tail_error_exceeds_tol).
    auto         f = [](double x) { return std::sin(5.0 * x); };
    const double a = 0.0;
    const double b = 2.0 * std::numbers::pi_v<double>;

    auto fn = fit<8>(f, a, b, /*tol=*/1e-10, options{.tol_kind = treeweave::TolKind::AbsoluteTail});
    REQUIRE(max_rel_err_1d(f, fn, a + 1e-6, b - 1e-6, N_SAMPLE) < 1e-6);
}

TEST_CASE("2D smooth polynomial, compile-time degree 8", "[treeweave][smooth][2d]") {
    // polyfit's FuncEvalND requires a tuple-like output even for a single
    // scalar; wrap in std::array<double, 1>.
    auto f = [](std::array<double, 2> x) -> std::array<double, 1> { return {x[0] * x[0] * x[0] * x[1] * x[1] + 0.1}; };
    auto exact = [](std::array<double, 2> x) { return x[0] * x[0] * x[0] * x[1] * x[1] + 0.1; };
    std::array<double, 2> const a{0.0, 0.0};
    std::array<double, 2> const b{2.0, 2.0};

    auto fn     = fit<8>(f, a, b, /*tol=*/1e-10);
    auto approx = [&](std::array<double, 2> x) { return fn(x)[0]; };
    REQUIRE(max_rel_err_2d(exact, approx, a, b, 2000) < 1e-6);
}

TEST_CASE("Runge function forces paneling", "[treeweave][runge]") {
    auto         f = [](double x) { return 1.0 / (1.0 + 25.0 * x * x); };
    const double a = -1.0, b = 1.0;

    auto fn = fit<8>(f, a, b, /*tol=*/1e-10);
    REQUIRE(max_rel_err_1d(f, fn, a + 1e-6, b - 1e-6, N_SAMPLE) < 1e-6);
}

TEST_CASE("Near-singular log forces subdivision", "[treeweave][log]") {
    const double shift = 1e-3;
    auto         f     = [shift](double x) { return std::log(x + shift); };
    const double a = 0.0, b = 1.0;

    auto fn = fit<10>(f, a, b, /*tol=*/1e-10);
    REQUIRE(max_rel_err_1d(f, fn, a + 1e-3, b - 1e-6, N_SAMPLE) < 1e-5);
}

TEST_CASE("Out-of-domain returns NaN; closed upper endpoint returns a value", "[treeweave][ood]") {
    auto         f = [](double x) { return std::sin(x); };
    const double a = 0.0, b = 1.0;
    auto         fn = fit<8>(f, a, b, /*tol=*/1e-10);

    // Lower side is open: x < a -> NaN.
    REQUIRE(std::isnan(fn(a - 0.5)));
    // Upper side is closed: operator()(b) returns the last leaf's boundary
    // value (a correct approximation of f(b)), not NaN.
    const double yb = fn(b);
    REQUIRE_FALSE(std::isnan(yb));
    REQUIRE(yb == Catch::Approx(f(b)).epsilon(1e-6));
    // Scalar operator() admits only the exact endpoint: x > b stays NaN.
    REQUIRE(std::isnan(fn(b + 0.5)));
}

TEST_CASE("print_stats reports descent-only leaf tables", "[treeweave][stats]") {
    auto fn = fit<8>([](double x) { return x * x; }, 0.0, 1.0, /*tol=*/1e-12);
    REQUIRE_FALSE(fn.has_fast_quantize());

    std::ostringstream out;
    auto              *old = std::cout.rdbuf(out.rdbuf());
    fn.print_stats();
    std::cout.rdbuf(old);
    REQUIRE(out.str().find("Leaf table: descent-only") != std::string::npos);
}

TEST_CASE("Rejects non-positive tolerance", "[treeweave][errors]") {
    auto f = [](double x) { return x * x; };
    REQUIRE_THROWS_AS(fit(f, 0.0, 1.0, /*tol=*/0.0), std::invalid_argument);
    REQUIRE_THROWS_AS(fit(f, 0.0, 1.0, /*tol=*/-1.0), std::invalid_argument);
}

TEST_CASE("Tolerance-driven overload converges", "[treeweave][tol-driven]") {
    auto f  = [](double x) { return std::sin(5.0 * x); };
    auto fn = fit(f, 0.0, 1.0, /*tol=*/1e-8);
    REQUIRE(max_rel_err_1d(f, fn, 1e-6, 1.0 - 1e-6, N_SAMPLE) < 1e-5);
}

TEST_CASE("3D scalar exp(-r^2)", "[treeweave][smooth][3d]") {
    auto f = [](std::array<double, 3> x) -> std::array<double, 1> {
        return {std::exp(-x[0] * x[0] - x[1] * x[1] - x[2] * x[2])};
    };
    auto fn = fit<8>(f, std::array{-1.0, -1.0, -1.0}, std::array{1.0, 1.0, 1.0}, /*tol=*/1e-10);

    std::mt19937                           gen(2);
    std::uniform_real_distribution<double> d(-0.99, 0.99);
    double                                 mx = 0.0;
    for (int i = 0; i < 3000; ++i) {
        std::array<double, 3> const x{d(gen), d(gen), d(gen)};
        const double                exact  = f(x)[0];
        const double                approx = fn(x)[0];
        if (std::abs(exact) > 1e-12)
            mx = std::max(mx, std::abs(exact - approx) / std::abs(exact));
    }
    REQUIRE(mx < 1e-6);
}

TEST_CASE("Vector-valued 2D -> 2D output", "[treeweave][vector-output]") {
    auto f = [](std::array<double, 2> x) -> std::array<double, 2> {
        return {std::sin(x[0] + x[1]), std::cos(x[0] - x[1])};
    };
    auto fn = fit<8>(f, std::array{0.0, 0.0}, std::array{1.0, 1.0},
                     /*tol=*/1e-10);

    std::mt19937                           gen(3);
    std::uniform_real_distribution<double> d(1e-3, 1.0 - 1e-3);
    double                                 mx = 0.0;
    for (int i = 0; i < 2000; ++i) {
        std::array<double, 2> const x{d(gen), d(gen)};
        const auto                  exact  = f(x);
        const auto                  approx = fn(x);
        for (std::size_t k = 0; k < 2; ++k)
            mx = std::max(mx, std::abs(exact[k] - approx[k]));
    }
    REQUIRE(mx < 1e-6);
}

TEST_CASE("Sharp tanh step forces subdivision", "[treeweave][sharp]") {
    auto f  = [](double x) { return std::tanh(50.0 * (x - 0.3)); };
    auto fn = fit<8>(f, 0.0, 1.0, /*tol=*/1e-10, options{.tol_kind = treeweave::TolKind::AbsoluteMax, .max_depth = 30});
    // Check away from the exact step where we expect subdivision to give
    // good accuracy; rel_err blows up near zero-crossings so use abs.
    std::mt19937                           gen(4);
    std::uniform_real_distribution<double> d(1e-3, 1.0 - 1e-3);
    double                                 mx = 0.0;
    for (int i = 0; i < N_SAMPLE; ++i) {
        const double x = d(gen);
        mx             = std::max(mx, std::abs(f(x) - fn(x)));
    }
    REQUIRE(mx < 1e-5);
}

TEST_CASE("2D anisotropic gaussian bump", "[treeweave][2d][bump]") {
    auto f = [](std::array<double, 2> x) -> std::array<double, 1> {
        return {std::exp(-100.0 * (x[0] - 0.5) * (x[0] - 0.5) - (x[1] - 0.5) * (x[1] - 0.5))};
    };
    auto exact = [](std::array<double, 2> x) {
        return std::exp(-100.0 * (x[0] - 0.5) * (x[0] - 0.5) - (x[1] - 0.5) * (x[1] - 0.5));
    };
    auto fn     = fit<10>(f, std::array{0.0, 0.0}, std::array{1.0, 1.0},
                          /*tol=*/1e-10);
    auto approx = [&](std::array<double, 2> x) { return fn(x)[0]; };
    REQUIRE(max_rel_err_2d(exact, approx, {0.001, 0.001}, {0.999, 0.999}, 5000) < 1e-6);
}

TEST_CASE("sqrt|x - 0.5| -- not C^1, max_depth guards runaway", "[treeweave][sharp]") {
    auto f = [](double x) { return std::sqrt(std::abs(x - 0.5)); };
    REQUIRE_THROWS_AS(
        fit<8>(f, 0.0, 1.0, /*tol=*/1e-10, options{.tol_kind = treeweave::TolKind::AbsoluteMax, .max_depth = 4}),
        treeweave::MaxDepthExceeded);
    // Generous max_depth + accept best-effort leaves at the singular panel
    // — `sqrt|x-0.5|` is not C^1 at 0.5, so that panel will never reach
    // tol=1e-10. We only need the rest of the domain to be usable.
    auto fn =
        fit<10>(f, 0.0, 1.0, /*tol=*/1e-10,
                options{.tol_kind = treeweave::TolKind::AbsoluteMax, .max_depth = 50, .allow_max_depth_leaves = true});
    // Sample away from the non-smooth point.
    std::mt19937                           gen(5);
    std::uniform_real_distribution<double> d(0.0, 0.45);
    double                                 mx = 0.0;
    for (int i = 0; i < 1000; ++i) {
        const double x = d(gen);
        mx             = std::max(mx, std::abs(f(x) - fn(x)));
    }
    REQUIRE(mx < 1e-4);
}

TEST_CASE("Batch vs single evaluation agree", "[treeweave][batch]") {
    auto f  = [](double x) { return std::sin(4.0 * x); };
    auto fn = fit<8>(f, 0.0, 1.0, /*tol=*/1e-10);

    std::mt19937                           gen(6);
    std::uniform_real_distribution<double> d(1e-3, 1.0 - 1e-3);
    constexpr int                          N = 500;
    std::vector<double>                    xs(N);
    for (auto &x : xs)
        x = d(gen);

    std::vector<double> batch(N);
    fn(xs.data(), batch.data(), N);

    // Batch path uses polyfit's SIMD Horner, scalar path uses scalar Horner —
    // identical mathematically but FMA reordering can drop a ULP.
    constexpr double ulp = std::numeric_limits<double>::epsilon();
    for (std::size_t i = 0; i < static_cast<std::size_t>(N); ++i) {
        const double single = fn(xs[i]);
        REQUIRE(std::abs(single - batch[i]) <= 8.0 * ulp * std::max(1.0, std::abs(single)));
    }
}

TEST_CASE("Sorted-1D batch matches unsorted batch and scalar", "[treeweave][batch][sorted]") {
    auto run = [](auto fn, double a, double b) {
        std::mt19937                           gen(7);
        std::uniform_real_distribution<double> d(a, b);
        constexpr std::size_t                  N_IN     = 4096; // above kSortThreshold
        constexpr std::size_t                  N_OOD_LO = 5;
        constexpr std::size_t                  N_OOD_HI = 7;
        const std::size_t                      N        = N_IN + N_OOD_LO + N_OOD_HI;

        std::vector<double> xs;
        xs.reserve(N);
        for (std::size_t i = 0; i < N_OOD_LO; ++i)
            xs.push_back(a - 1.0 - 0.1 * static_cast<double>(i));
        for (std::size_t i = 0; i < N_IN; ++i)
            xs.push_back(d(gen));
        for (std::size_t i = 0; i < N_OOD_HI; ++i)
            xs.push_back(b + 1.0 + 0.1 * static_cast<double>(i));

        std::sort(xs.begin(), xs.end());

        std::vector<double> sorted_out(N);
        fn.sorted(xs.data(), sorted_out.data(), N);

        std::vector<double> batch_out(N);
        fn(xs.data(), batch_out.data(), N);

        constexpr double ulp = std::numeric_limits<double>::epsilon();
        for (std::size_t i = 0; i < N; ++i) {
            if (xs[i] < a) {
                // OOD-low: NaN on every path (open lower bound).
                REQUIRE(std::isnan(sorted_out[i]));
                REQUIRE(std::isnan(batch_out[i]));
            } else if (xs[i] <= b) {
                // In-domain, incl. the closed upper endpoint. The sorted path
                // uses polyfit's SIMD batch kernel directly, so its output
                // matches the unsorted batch bit-for-bit and the scalar oracle.
                REQUIRE(sorted_out[i] == batch_out[i]);
                const double scalar = fn(xs[i]);
                REQUIRE(std::abs(scalar - sorted_out[i]) <= 8.0 * ulp * std::max(1.0, std::abs(scalar)));
            } else {
                // Above b: OOD NaN on every path; leaf-table no longer clamps x>b to last leaf.
                REQUIRE(std::isnan(sorted_out[i]));
                REQUIRE(std::isnan(batch_out[i]));
                REQUIRE(std::isnan(fn(xs[i])));
            }
        }
    };

    SECTION("shallow tree (leaf-table fast path)") {
        // Force uniform refinement on a smooth fn so the leaf table is
        // guaranteed live: this drives the SIMD-quantize fast path, whose
        // above-b OOD handling this section exists to pin (a bare tol-based
        // fit of a smooth fn can land at depth 0-1 and skip the table).
        auto f  = [](double x) { return std::cos(x); };
        auto fn = fit<8>(f, 0.0, 1.0, /*tol=*/1e-8, options{.min_uniform_depth = 4});
        REQUIRE(fn.has_fast_quantize());
        run(fn, 0.0, 1.0);
    }

    SECTION("deep tree (no leaf-table)") {
        // A near-singular feature pushes the adaptive paneler past the
        // leaf-table depth cap (14 bits in 1D), exercising the descent
        // fallback in find_leaf_id.
        auto f  = [](double x) { return 1.0 / (x * x + 1e-6); };
        auto fn = fit<8>(f, -1.0, 1.0, /*tol=*/1e-8, options{.max_depth = 30, .max_memory_mib = 256});
        run(fn, -1.0, 1.0);
    }
}

TEST_CASE("Batch vs single evaluation agree -- 2D vector output", "[treeweave][batch][2d]") {
    auto f = [](std::array<double, 2> x) -> std::array<double, 2> {
        return {std::sin(x[0] + x[1]), std::cos(x[0] - x[1])};
    };
    auto fn = fit<8>(f, std::array{0.0, 0.0}, std::array{1.0, 1.0}, /*tol=*/1e-10);

    std::mt19937                           gen(42);
    std::uniform_real_distribution<double> d(1e-3, 1.0 - 1e-3);
    constexpr std::size_t                  N = 1024; // above counting-sort threshold
    std::vector<double>                    flat(2 * N);
    std::vector<std::array<double, 2>>     pts(N);
    for (std::size_t i = 0; i < N; ++i) {
        pts[i]          = {d(gen), d(gen)};
        flat[2 * i]     = pts[i][0];
        flat[2 * i + 1] = pts[i][1];
    }

    std::vector<double> batch(2 * N);
    fn(flat.data(), batch.data(), N);

    constexpr double ulp = std::numeric_limits<double>::epsilon();
    for (std::size_t i = 0; i < N; ++i) {
        const auto single = fn(pts[i]);
        REQUIRE(std::abs(single[0] - batch[2 * i]) <= 4.0 * ulp * std::max(1.0, std::abs(single[0])));
        REQUIRE(std::abs(single[1] - batch[2 * i + 1]) <= 4.0 * ulp * std::max(1.0, std::abs(single[1])));
    }
}

// Pins recompute path after leaf_ids[] removal; 1D-in 2D-out matches scalar oracle.
TEST_CASE("Batch (1D in, 2D out) matches per-point scalar after leaf_ids drop", "[treeweave][batch][1d][c1]") {
    // treeweave requires array-input for vector-output fits; spell the
    // 1D input as std::array<double, 1> to route through polyfit's
    // FuncEvalND. The eval path under test is the same SIMD-quantize
    // 1D batch kernel (input_dim == 1).
    auto f = [](std::array<double, 1> x) -> std::array<double, 2> {
        return {std::sin(3.0 * x[0]), std::cos(2.5 * x[0] + 0.1)};
    };
    auto fn = fit<8>(f, std::array{0.0}, std::array{1.0}, /*tol=*/1e-10);

    std::mt19937                           gen(123);
    std::uniform_real_distribution<double> d(1e-3, 1.0 - 1e-3);
    constexpr std::size_t                  N = 1000;
    std::vector<double>                    xs(N);
    for (std::size_t i = 0; i < N; ++i)
        xs[i] = d(gen);

    std::vector<double> batch(2 * N);
    fn(xs.data(), batch.data(), N);

    constexpr double ulp = std::numeric_limits<double>::epsilon();
    for (std::size_t i = 0; i < N; ++i) {
        const auto single = fn(std::array{xs[i]});
        REQUIRE(std::abs(single[0] - batch[2 * i]) <= 4.0 * ulp * std::max(1.0, std::abs(single[0])));
        REQUIRE(std::abs(single[1] - batch[2 * i + 1]) <= 4.0 * ulp * std::max(1.0, std::abs(single[1])));
    }
}

// SoA overload: aos_out[2*k+d] == soa[d][k] bitwise.
TEST_CASE("SoA batch overload matches AoS bitwise (1D in, 2D out)", "[treeweave][batch][soa]") {
    auto f = [](std::array<double, 1> x) -> std::array<double, 2> {
        return {std::sin(3.0 * x[0]), std::cos(2.5 * x[0] + 0.1)};
    };
    auto fn = fit<8>(f, std::array{0.0}, std::array{1.0}, /*tol=*/1e-10);

    std::mt19937                           gen(2024);
    std::uniform_real_distribution<double> d(1e-3, 1.0 - 1e-3);
    constexpr std::size_t                  N = 1024; // > kSortThreshold so the batch pipeline runs
    std::vector<double>                    xs(N);
    for (auto &x : xs)
        x = d(gen);

    SECTION("unsorted (operator()(xp, soa, n))") {
        std::vector<double> aos(2 * N);
        fn(xs.data(), aos.data(), N);

        std::vector<double>     soa_buf(2 * N);
        std::array<double *, 2> soa{soa_buf.data(), soa_buf.data() + N};
        fn(xs.data(), soa, N);

        for (std::size_t k = 0; k < N; ++k) {
            REQUIRE(aos[2 * k + 0] == soa[0][k]);
            REQUIRE(aos[2 * k + 1] == soa[1][k]);
        }
    }

    SECTION("sorted (fn.sorted(xp, soa, n))") {
        // Include OOD prefix/suffix to exercise the NaN-fill SoA path.
        std::vector<double> sxs;
        sxs.reserve(N + 6);
        for (int i = 0; i < 3; ++i)
            sxs.push_back(-1.0 - 0.1 * i);
        for (auto &x : xs)
            sxs.push_back(x);
        for (int i = 0; i < 3; ++i)
            sxs.push_back(2.0 + 0.1 * i);
        std::sort(sxs.begin(), sxs.end());
        const std::size_t M = sxs.size();

        std::vector<double> aos(2 * M);
        fn.sorted(sxs.data(), aos.data(), M);

        std::vector<double>     soa_buf(2 * M);
        std::array<double *, 2> soa{soa_buf.data(), soa_buf.data() + M};
        fn.sorted(sxs.data(), soa, M);

        for (std::size_t k = 0; k < M; ++k) {
            if (std::isnan(aos[2 * k + 0])) {
                REQUIRE(std::isnan(soa[0][k]));
                REQUIRE(std::isnan(soa[1][k]));
            } else {
                REQUIRE(aos[2 * k + 0] == soa[0][k]);
                REQUIRE(aos[2 * k + 1] == soa[1][k]);
            }
        }
    }
}

// The SoA batch overload is gated on `output_dim > 1`. For scalar
// outputs, AoS and SoA coincide — users pass `value_type*` directly.

TEST_CASE("Batch vs single evaluation agree -- 3D scalar output", "[treeweave][batch][3d]") {
    auto f = [](std::array<double, 3> x) -> std::array<double, 1> {
        return {std::exp(-x[0] * x[0] - x[1] * x[1] - x[2] * x[2])};
    };
    auto fn = fit<8>(f, std::array{-1.0, -1.0, -1.0}, std::array{1.0, 1.0, 1.0}, /*tol=*/1e-10);

    std::mt19937                           gen(7);
    std::uniform_real_distribution<double> d(-0.99, 0.99);
    constexpr std::size_t                  N = 2000;
    std::vector<double>                    flat(3 * N);
    std::vector<std::array<double, 3>>     pts(N);
    for (std::size_t i = 0; i < N; ++i) {
        pts[i] = {d(gen), d(gen), d(gen)};
        for (std::size_t j = 0; j < 3; ++j)
            flat[3 * i + j] = pts[i][j];
    }

    std::vector<double> batch(N);
    fn(flat.data(), batch.data(), N);

    constexpr double ulp = std::numeric_limits<double>::epsilon();
    for (std::size_t i = 0; i < N; ++i) {
        const auto single = fn(pts[i]);
        REQUIRE(std::abs(single[0] - batch[i]) <= 4.0 * ulp * std::max(1.0, std::abs(single[0])));
    }
}

TEST_CASE("Batch vs single evaluation agree across L4 tile boundary", "[treeweave][batch][tile]") {
    // Phase 16 / Layer L4: the batch path now tiles when n_trg exceeds
    // tile_K (default 64 K). Pin the boundary by feeding a batch large
    // enough to span multiple tiles and assert per-point agreement with
    // the scalar path.
    auto f  = [](double x) { return std::sin(4.0 * x) + std::cos(7.0 * x); };
    auto fn = fit<8>(f, 0.0, 1.0, /*tol=*/1e-10);

    std::mt19937                           gen(13);
    std::uniform_real_distribution<double> d(1e-3, 1.0 - 1e-3);
    constexpr std::size_t                  N = 200'000; // > default tile_K (65 536) → ≥ 4 tiles
    std::vector<double>                    xs(N);
    for (auto &x : xs)
        x = d(gen);

    std::vector<double> batch(N);
    fn(xs.data(), batch.data(), N);

    constexpr double ulp = std::numeric_limits<double>::epsilon();
    for (std::size_t i = 0; i < N; ++i) {
        const double single = fn(xs[i]);
        REQUIRE(std::abs(single - batch[i]) <= 8.0 * ulp * std::max(1.0, std::abs(single)));
    }
}

TEST_CASE("Memory budget aborts a runaway near-singular fit", "[treeweave][memory-budget]") {
    // 3D Yukawa with a *very* tight tolerance over a domain straddling the
    // origin singularity refines aggressively. Each leaf is ~6 KiB
    // (deg=8 in 3D), so a 1 MiB budget caps at ~170 leaves before bailing
    // — well below what the smooth-tol target would otherwise pursue.
    auto f = [](std::array<double, 3> x) -> std::array<double, 1> {
        const double r = std::sqrt(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]);
        return {std::exp(-r) / r};
    };
    REQUIRE_THROWS_AS(
        fit<8>(f, std::array{0.01, 0.01, 0.01}, std::array{1.5, 1.5, 1.5},
               /*tol=*/1e-12,
               options{.tol_kind = treeweave::TolKind::AbsoluteMax, .max_depth = 50, .max_memory_mib = 1}),
        treeweave::MemoryBudgetExceeded);
    // Disabling the budget but keeping the depth ceiling still catches the
    // runaway via the existing MaxDepthExceeded path.
    auto g = [](double x) { return std::sqrt(std::abs(x - 0.5)); };
    REQUIRE_THROWS_AS(fit<8>(g, 0.0, 1.0, /*tol=*/1e-12,
                             options{.tol_kind = treeweave::TolKind::AbsoluteMax, .max_depth = 4, .max_memory_mib = 0}),
                      treeweave::MaxDepthExceeded);
}

TEST_CASE("allow_max_depth_leaves accepts unconverged panels", "[treeweave][maxdepth][lossy]") {
    auto f = [](double x) { return std::sqrt(std::abs(x - 0.5)); };

    // Default path (allow_max_depth_leaves=false) must throw and the
    // exception must carry every unconverged panel, not just the first.
    try {
        (void)fit<8>(f, 0.0, 1.0, /*tol=*/1e-10, options{.tol_kind = treeweave::TolKind::AbsoluteMax, .max_depth = 4});
        FAIL("expected MaxDepthExceeded");
    } catch (const treeweave::MaxDepthExceeded &e) {
        REQUIRE_FALSE(e.panels().empty());
        for (const auto &p : e.panels()) {
            REQUIRE(p.a.size() == 1);
            REQUIRE(p.b.size() == 1);
            REQUIRE(p.a[0] < p.b[0]);
            REQUIRE(p.depth == 4);
        }
        REQUIRE(e.a() == e.panels().front().a);
        REQUIRE(e.b() == e.panels().front().b);
    }

    // Opt-in path: same fit completes, and the unconverged panels are
    // surfaced via Function::non_converged_panels(). Eval still produces
    // a finite (best-effort) value at the singular point.
    auto fn =
        fit<8>(f, 0.0, 1.0, /*tol=*/1e-10,
               options{.tol_kind = treeweave::TolKind::AbsoluteMax, .max_depth = 4, .allow_max_depth_leaves = true});
    REQUIRE_FALSE(fn.non_converged_panels().empty());
    REQUIRE(std::isfinite(fn(0.5)));
}

TEST_CASE("1D smooth fit on large symmetric domain [-1e6, 1e6]", "[treeweave][large-domain]") {
    // Slow oscillation so the tree stays shallow even on a wide domain.
    const double a = -1.0e6;
    const double b = 1.0e6;
    auto         f = [](double x) { return std::sin(1e-5 * x) + 0.25 * std::cos(3e-6 * x); };

    auto fn = fit<8>(f, a, b, /*tol=*/1e-9);
    REQUIRE(max_rel_err_1d(f, fn, a + 1.0, b - 1.0, N_SAMPLE) < 1e-7);
    // OOD on the wide domain still NaNs cleanly.
    REQUIRE(std::isnan(fn(a - 1.0)));
    REQUIRE(std::isnan(fn(b + 1.0)));
}

TEST_CASE("1D smooth fit on far-shifted domain centred at 1e6", "[treeweave][large-domain][shifted]") {
    // Unit interval shifted by 1e6 catches precision bugs (b-a cancellation, leaf-table quantize at large x); ~6 digits
    // lost so tols reflect that floor.
    const double centre = 1.0e6;
    const double a      = centre;
    const double b      = centre + 1.0;
    auto         f      = [centre](double x) { return std::sin(5.0 * (x - centre)); };

    auto fn = fit<8>(f, a, b, /*tol=*/1e-8);
    REQUIRE(max_rel_err_1d(f, fn, a + 1e-6, b - 1e-6, N_SAMPLE) < 1e-5);
}

TEST_CASE("2D smooth fit on large asymmetric domain", "[treeweave][large-domain][2d]") {
    // Wide non-square box with mismatched per-axis scales — exercises the
    // anisotropic-domain top-level paneling.
    auto f = [](std::array<double, 2> x) -> std::array<double, 1> {
        return {std::sin(1e-3 * x[0]) * std::cos(1e-2 * x[1])};
    };
    auto exact = [](std::array<double, 2> x) { return std::sin(1e-3 * x[0]) * std::cos(1e-2 * x[1]); };
    std::array<double, 2> const a{-1.0e3, -1.0e2};
    std::array<double, 2> const b{1.0e3, 1.0e2};

    auto fn     = fit<8>(f, a, b, /*tol=*/1e-9);
    auto approx = [&](std::array<double, 2> x) { return fn(x)[0]; };
    REQUIRE(max_rel_err_2d(exact, approx, a, b, 2000) < 1e-7);
}

TEST_CASE("Memory budget caps runaway fits -- opt-in to raise", "[treeweave][memory-budget]") {
    // Budget guard split into two checks to avoid the ~33 MiB/157 s old fit.
    REQUIRE(treeweave::detail::auto_memory_budget_mib(1) == 4);  // 1D
    REQUIRE(treeweave::detail::auto_memory_budget_mib(2) == 8);  // 2D
    REQUIRE(treeweave::detail::auto_memory_budget_mib(3) == 16); // 3D

    // Then exercise the cap mechanism on a near-singular Yukawa-like 3D fit at
    // a looser tol (~2 MiB tree, ~13 s at -O0 — fast even on the slowest Debug
    // toolchains). max_memory_mib is integer MiB, so a 1 MiB cap is burst by
    // the ~2 MiB tree mid-fit and throws; an 8 MiB cap completes the same fit.
    auto f = [](std::array<double, 3> x) -> std::array<double, 1> {
        const double r = std::sqrt(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]);
        return {std::exp(-r) / r};
    };
    REQUIRE_THROWS_AS(fit<8>(f, std::array{0.05, 0.05, 0.05}, std::array{1.5, 1.5, 1.5},
                             /*tol=*/1e-9, options{.tol_kind = treeweave::TolKind::AbsoluteMax, .max_memory_mib = 1}),
                      treeweave::MemoryBudgetExceeded);
    auto fn = fit<8>(f, std::array{0.05, 0.05, 0.05}, std::array{1.5, 1.5, 1.5},
                     /*tol=*/1e-9, options{.tol_kind = treeweave::TolKind::AbsoluteMax, .max_memory_mib = 8});
    REQUIRE(std::isfinite(fn(std::array{1.0, 1.0, 1.0})[0]));
}

TEST_CASE("eval_pack matches scalar operator() across small N", "[treeweave][pack]") {
    auto f  = [](double x) { return std::sin(5.0 * x); };
    auto fn = fit<8>(f, 0.0, 1.0, /*tol=*/1e-10);

    // Small-N (unrolled scalar fan-out) and large-N (batch path) branches.
    const std::array<double, 4> xs4{0.1, 0.3, 0.5, 0.7};
    const auto                  ys4 = fn.eval_pack(xs4);
    for (std::size_t i = 0; i < xs4.size(); ++i)
        REQUIRE(ys4[i] == fn(xs4[i]));

    std::array<double, 64> xs64{};
    for (std::size_t i = 0; i < xs64.size(); ++i)
        xs64[i] = (static_cast<double>(i) + 0.5) / static_cast<double>(xs64.size());
    const auto ys64 = fn.eval_pack(xs64);
    // Batch path uses SIMD coeff layout; results agree with scalar up to
    // a few ULPs from associativity differences in the Hybrid chain.
    for (std::size_t i = 0; i < xs64.size(); ++i)
        REQUIRE(ys64[i] == Catch::Approx(fn(xs64[i])).margin(1e-14));
}

TEST_CASE("eval_scatter_sorted matches per-pair scalar evals", "[treeweave][scatter]") {
    // Use a single Func type (std::function) so all fits share a Function
    // specialization — eval_scatter_sorted takes a span of like pointers.
    using ff = std::function<double(double)>;
    std::vector<ff> const exact{ff{[](double x) { return std::sin(3.0 * x); }},
                                ff{[](double x) { return std::cos(7.0 * x); }},
                                ff{[](double x) { return x * x - 0.5; }}};

    using fn_t = decltype(fit<8>(exact[0], 0.0, 1.0, 1e-10));
    std::vector<fn_t> fns;
    fns.reserve(exact.size());
    for (const auto &g : exact)
        fns.push_back(fit<8>(g, 0.0, 1.0, /*tol=*/1e-10));

    std::vector<const fn_t *> fit_ptrs;
    fit_ptrs.reserve(fns.size());
    for (const auto &fn : fns)
        fit_ptrs.push_back(&fn);

    std::mt19937                                 gen(42);
    std::uniform_real_distribution<double>       dx(0.0, 1.0);
    std::uniform_int_distribution<std::uint32_t> di(0, 2);
    constexpr std::size_t                        n = 137; // not a multiple of any SIMD width
    std::vector<std::uint32_t>                   ids(n);
    std::vector<double>                          xs(n);
    for (std::size_t i = 0; i < n; ++i) {
        ids[i] = di(gen);
        xs[i]  = dx(gen);
    }
    std::vector<double> ys(n);
    treeweave::eval_scatter_sorted<8, ff>(std::span<const fn_t *const>{fit_ptrs.data(), fit_ptrs.size()},
                                          std::span<const std::uint32_t>{ids.data(), ids.size()},
                                          std::span<const double>{xs.data(), xs.size()},
                                          std::span<double>{ys.data(), ys.size()},
                                          /*n_fits=*/static_cast<std::uint32_t>(fit_ptrs.size()));

    // Batched eval per fit goes through the SIMD-batch path; per-fit
    // results match the scalar operator() up to a few ULPs.
    for (std::size_t i = 0; i < n; ++i)
        REQUIRE(ys[i] == Catch::Approx((*fit_ptrs[ids[i]])(xs[i])).margin(1e-14));
}

TEST_CASE("Batch handles out-of-domain points as NaN", "[treeweave][batch][ood]") {
    auto f  = [](double x) { return std::sin(x); };
    auto fn = fit<8>(f, 0.0, 1.0, /*tol=*/1e-10);

    std::vector<double> xs{0.1, -0.5, 0.3, 2.0, 0.9, 5.0};
    std::vector<double> out(xs.size());
    fn(xs.data(), out.data(), xs.size());

    REQUIRE(out[0] == fn(0.1));
    REQUIRE(std::isnan(out[1]));
    REQUIRE(out[2] == fn(0.3));
    REQUIRE(std::isnan(out[3]));
    REQUIRE(out[4] == fn(0.9));
    REQUIRE(std::isnan(out[5]));
}

// Phase-0 parity sweep across all eval_pack<N> branches the bench
// exercises. Catches kernel-layout drift between the scalar fan-out
// path (N < 32) and the batch-path delegation (N >= 32) up front.
TEST_CASE("eval_pack<N> parity sweep matches scalar operator()", "[treeweave][pack][parity]") {
    auto f  = [](double x) { return std::tanh(10.0 * x) * std::sin(3.0 * x); };
    auto fn = fit<8>(f, -1.0, 1.0, /*tol=*/1e-10);

    auto check = [&](auto N_const) {
        std::array<double, N_const()> xs{};
        for (std::size_t i = 0; i < N_const; ++i)
            xs[i] = -1.0 + (2.0 * static_cast<double>(i) + 1.0) / (2.0 * static_cast<double>(N_const));
        const auto ys = fn.eval_pack(xs);
        for (std::size_t i = 0; i < N_const; ++i)
            REQUIRE(ys[i] == Catch::Approx(fn(xs[i])).margin(1e-14));
    };
    // Cover both branches: scalar fan-out (< 32) and SIMD batch path (>= 32).
    check(std::integral_constant<std::size_t, 1>{});
    check(std::integral_constant<std::size_t, 4>{});
    check(std::integral_constant<std::size_t, 8>{});
    check(std::integral_constant<std::size_t, 16>{});
    check(std::integral_constant<std::size_t, 32>{});
    check(std::integral_constant<std::size_t, 64>{});
}

// Counting-sort overload of eval_scatter_sorted: parity with per-pair
// scalar evals across small/large n and dense/sparse-id shapes.
TEST_CASE("eval_scatter_sorted counting-sort matches per-pair scalar", "[treeweave][scatter][counting-sort]") {
    using ff                  = std::function<double(double)>;
    constexpr std::uint32_t R = 16;
    std::vector<ff>         exact;
    exact.reserve(R);
    std::mt19937                           g(7);
    std::uniform_real_distribution<double> cd(-1.0, 1.0);
    for (std::uint32_t r = 0; r < R; ++r) {
        const double a = cd(g), b = cd(g), c = cd(g);
        exact.emplace_back([a, b, c](double x) {
            return ((a * x + b) * x + c); // simple quadratic
        });
    }
    using fn_t = decltype(fit<8>(exact[0], 0.0, 1.0, 1e-10));
    std::vector<fn_t> fns;
    fns.reserve(exact.size());
    for (auto &fexact : exact)
        fns.push_back(fit<8>(fexact, 0.0, 1.0, /*tol=*/1e-10));
    std::vector<const fn_t *> fit_ptrs;
    fit_ptrs.reserve(fns.size());
    for (auto &fn : fns)
        fit_ptrs.push_back(&fn);

    auto check = [&](std::vector<std::uint32_t> ids, std::vector<double> xs) {
        const std::size_t   n = ids.size();
        std::vector<double> ys(n);
        treeweave::eval_scatter_sorted<8, ff>(std::span<const fn_t *const>{fit_ptrs.data(), fit_ptrs.size()},
                                              std::span<const std::uint32_t>{ids.data(), ids.size()},
                                              std::span<const double>{xs.data(), xs.size()},
                                              std::span<double>{ys.data(), ys.size()}, R);
        for (std::size_t i = 0; i < n; ++i)
            REQUIRE(ys[i] == Catch::Approx((*fit_ptrs[ids[i]])(xs[i])).margin(1e-14));
    };

    check({}, {});                             // n=0
    check({0}, {0.5});                         // n=1
    check({3, 3, 3, 3}, {0.1, 0.2, 0.3, 0.4}); // single-id
    check({0, 15, 0, 15, 0, 15, 0},            // sparse-use ids 0, 15
          {0.1, 0.9, 0.2, 0.8, 0.3, 0.7, 0.4});

    std::uniform_int_distribution<std::uint32_t> di(0, R - 1);
    std::uniform_real_distribution<double>       dx(0.0 + 1e-6, 1.0 - 1e-6);
    std::mt19937                                 ig(42);
    constexpr std::size_t                        n = 256;
    std::vector<std::uint32_t>                   ids(n);
    std::vector<double>                          xs(n);
    for (std::size_t i = 0; i < n; ++i) {
        ids[i] = di(ig);
        xs[i]  = dx(ig);
    }
    check(ids, xs);
}

// Pin the leaf-table build threshold: fits that land at max_depth up
// to 16 (in 1D) must still get a table — the descent fallback otherwise
// drops IPC from ~4.5 to ~1.9 and lights up branch-mispredict (measured
// in bench_pack_scatter on tanh500_deep / tanh1000_deep).
TEST_CASE("Leaf-table built at widened depth threshold", "[treeweave][leaf-table]") {
    // tanh500 at tol=1e-12 deg=6 lands at depth ~16; the table threshold
    // must cover that depth.
    auto fn = fit<6>([](double x) { return std::tanh(500.0 * x); }, -1.0, 1.0, /*tol=*/1e-12);
    REQUIRE(fn.has_fast_quantize());

    // Sanity at the previously-supported depth too — must not regress.
    auto shallow = fit<8>([](double x) { return 1.0 / (1.0 + 25.0 * x * x); }, -1.0, 1.0, /*tol=*/1e-10);
    REQUIRE(shallow.has_fast_quantize());
}

// min_uniform_depth forces leaf-table live on smooth fns; tol-based stops at depth 1-2 and skips it.
TEST_CASE("min_uniform_depth forces uniform refinement and builds leaf table",
          "[treeweave][min_uniform_depth][leaf-table]") {
    auto f  = [](double x) { return std::cos(x); };
    auto fn = fit<8>(f, 0.0, 1.0, /*tol=*/1e-3, options{.min_uniform_depth = 2});

    // Leaf table is live — driving condition for the SIMD-quantize
    // fast path in the batch eval pipeline.
    REQUIRE(fn.has_fast_quantize());

    // Total leaves >= 2^min_uniform_depth = 4 across all subtrees.
    auto count_leaves = [&]() {
        std::size_t n_leaves = 0;
        for (const auto &subtree : fn.get_subtrees())
            for (const auto &node : subtree.get_nodes())
                n_leaves += static_cast<std::size_t>(node.is_leaf());
        return n_leaves;
    };
    REQUIRE(count_leaves() >= 4u);

    // Default (no forcing) on the same fit lands at a single leaf —
    // proves the knob is the cause of the multi-leaf result above.
    auto        fn_default     = fit<8>(f, 0.0, 1.0, /*tol=*/1e-3);
    std::size_t default_leaves = 0;
    for (const auto &subtree : fn_default.get_subtrees())
        for (const auto &node : subtree.get_nodes())
            default_leaves += static_cast<std::size_t>(node.is_leaf());
    REQUIRE(default_leaves < count_leaves());
}

// Edge cases for eval_scatter_sorted: n=0 (no-op), n=1 (single pair),
// single-fit-id (all runs collapse to one), and a sparse-id case
// (gaps in fit-id space — caller pads n_fits to cover the max id).
TEST_CASE("eval_scatter_sorted edge cases", "[treeweave][scatter][edge]") {
    using ff = std::function<double(double)>;
    std::vector<ff> const exact{ff{[](double x) { return std::sin(x); }}, ff{[](double x) { return std::cos(x); }}};
    using fn_t = decltype(fit<8>(exact[0], 0.0, 1.0, 1e-10));
    std::vector<fn_t> fns;
    fns.reserve(exact.size());
    for (const auto &g : exact)
        fns.push_back(fit<8>(g, 0.0, 1.0, /*tol=*/1e-10));
    std::vector<const fn_t *> fit_ptrs;
    fit_ptrs.reserve(fns.size());
    for (const auto &fn : fns)
        fit_ptrs.push_back(&fn);

    auto run = [&](std::vector<std::uint32_t> ids, std::vector<double> xs) {
        std::vector<double> ys(xs.size());
        const std::uint32_t n_fits = static_cast<std::uint32_t>(fit_ptrs.size());
        treeweave::eval_scatter_sorted<8, ff>(std::span<const fn_t *const>{fit_ptrs.data(), fit_ptrs.size()},
                                              std::span<const std::uint32_t>{ids.data(), ids.size()},
                                              std::span<const double>{xs.data(), xs.size()},
                                              std::span<double>{ys.data(), ys.size()}, n_fits);
        for (std::size_t i = 0; i < xs.size(); ++i)
            REQUIRE(ys[i] == Catch::Approx((*fit_ptrs[ids[i]])(xs[i])).margin(1e-14));
    };

    run({}, {});                                     // n=0
    run({0}, {0.5});                                 // n=1
    run({1, 1, 1, 1, 1}, {0.1, 0.2, 0.3, 0.4, 0.5}); // single-fit-id
    run({0, 1, 0, 1, 0, 1, 0}, {0.1, 0.9, 0.2, 0.8, 0.3, 0.7, 0.4});
}

// TST1 / COV-G3: f32 parity + sorted path; tol_f*100 tracks fit quality tightly.
TEST_CASE("f32 parity: scalar, batch, and sorted agree with reference fit", "[treeweave][f32]") {
    constexpr float  tol_f = 1e-5F;
    constexpr double tol_d = 1e-5; // same value, double — passed to fit<>(tol)
    // Smooth, comfortably non-zero, and f32-representable on [0, 1].
    auto        func_exact = [](float x) { return std::exp(0.5F * x) + std::sin(3.0F * x); };
    const float a = 0.0F, b = 1.0F;
    auto        fn = fit<7>(func_exact, a, b, tol_d);

    std::mt19937                          gen(99);
    std::uniform_real_distribution<float> d(a + 0.01F, b - 0.01F);
    constexpr std::size_t                 N = 2000;
    std::vector<float>                    xs(N);
    for (auto &x : xs)
        x = d(gen);

    for (std::size_t i = 0; i < N; ++i) {
        const float approx = fn(xs[i]);
        const float exact  = func_exact(xs[i]);
        REQUIRE(std::abs(approx - exact) < 100.0F * tol_f * std::max(1.0F, std::abs(exact)));
    }

    std::vector<float> batch(N);
    fn(xs.data(), batch.data(), N);
    constexpr float ulp_f = std::numeric_limits<float>::epsilon();
    for (std::size_t i = 0; i < N; ++i)
        REQUIRE(std::abs(fn(xs[i]) - batch[i]) <= 8.0F * ulp_f * std::max(1.0F, std::abs(fn(xs[i]))));

    // Sorted path (COV-G3): OOD-low (NaN), in-domain, above-b (leaf-table may extrapolate finite, matching batch).
    constexpr std::size_t N_OOD_LO = 3, N_OOD_HI = 4;
    std::vector<float>    sxs;
    sxs.reserve(N + N_OOD_LO + N_OOD_HI);
    for (std::size_t i = 0; i < N_OOD_LO; ++i)
        sxs.push_back(a - 1.0F - 0.1F * static_cast<float>(i));
    for (float x : xs)
        sxs.push_back(x);
    for (std::size_t i = 0; i < N_OOD_HI; ++i)
        sxs.push_back(b + 1.0F + 0.1F * static_cast<float>(i));
    std::sort(sxs.begin(), sxs.end());
    const std::size_t M = sxs.size();

    std::vector<float> sorted_out(M);
    fn.sorted(sxs.data(), sorted_out.data(), M);

    std::vector<float> batch_out_sorted(M);
    fn(sxs.data(), batch_out_sorted.data(), M);

    for (std::size_t i = 0; i < M; ++i) {
        if (sxs[i] < a) {
            // OOD-low: NaN on every path.
            REQUIRE(std::isnan(sorted_out[i]));
            REQUIRE(std::isnan(batch_out_sorted[i]));
        } else if (sxs[i] <= b) {
            // In-domain: sorted and batch agree.
            REQUIRE(std::isfinite(sorted_out[i]));
            REQUIRE(sorted_out[i] == batch_out_sorted[i]);
        } else {
            // Above b: sorted and batch agree on NaN-ness; when both are
            // finite (leaf-table fast path extrapolation) they must match.
            REQUIRE(std::isnan(sorted_out[i]) == std::isnan(batch_out_sorted[i]));
            if (!std::isnan(sorted_out[i])) {
                REQUIRE(std::isfinite(sorted_out[i]));
                REQUIRE(std::isfinite(batch_out_sorted[i]));
                REQUIRE(sorted_out[i] == batch_out_sorted[i]);
            }
        }
    }
}
TEST_CASE("Complex-valued scalar fit: double in, std::complex out", "[treeweave][complex][1d]") {
    // Issue #23: scalar `double` in, `std::complex<double>` out — no manual
    // array (un)packing at the fit or eval boundary.
    using cd = std::complex<double>;
    auto g   = [](double x) -> cd { return {std::sin(3.0 * x), std::exp(-x)}; };
    auto fn  = fit(g, 0.1, 2.0, /*tol=*/1e-10);

    for (double x = 0.15; x < 1.95; x += 0.01) {
        const cd got = fn(x);
        REQUIRE(std::abs(got - g(x)) < 1e-8);
    }

    // Out-of-domain is NaN in both components (matches the real batch path).
    const cd ood = fn(5.0);
    REQUIRE(std::isnan(ood.real()));
    REQUIRE(std::isnan(ood.imag()));

    // Batch + sorted over a std::complex<double> buffer (reinterpret path).
    std::vector<double> xs;
    for (double x = 0.15; x < 1.95; x += 0.01)
        xs.push_back(x);
    std::vector<cd> batch(xs.size());
    std::vector<cd> srt(xs.size());
    fn(xs.data(), batch.data(), xs.size());
    fn.sorted(xs.data(), srt.data(), xs.size());
    for (std::size_t i = 0; i < xs.size(); ++i) {
        REQUIRE(std::abs(batch[i] - g(xs[i])) < 1e-8);
        REQUIRE(srt[i] == batch[i]);
        // The modulus of the fit tracks the modulus of the target — i.e. the
        // complex value (not just each real channel) is a faithful interpolant.
        REQUIRE(std::abs(batch[i]) == Catch::Approx(std::abs(g(xs[i]))).margin(1e-8));
    }
}

TEST_CASE("Complex-valued scalar fit: float value_type", "[treeweave][complex][1d]") {
    // Same ergonomic path on the f32 leaf math (value_type == float): scalar
    // float in, std::complex<float> out, batch over a std::complex<float> buffer.
    using cf = std::complex<float>;
    auto g   = [](float x) -> cf { return {std::sin(3.0F * x), std::exp(-x)}; };
    // 1e-4 is a realistic f32 target — a tighter tol hits float epsilon in the
    // relative convergence check and over-panels past the auto memory budget.
    auto fn = fit(g, 0.1F, 2.0F, /*tol=*/1e-4);

    std::vector<float> xs;
    for (float x = 0.15F; x < 1.95F; x += 0.01F)
        xs.push_back(x);
    std::vector<cf> batch(xs.size());
    fn(xs.data(), batch.data(), xs.size());
    for (std::size_t i = 0; i < xs.size(); ++i) {
        REQUIRE(std::abs(fn(xs[i]) - g(xs[i])) < 1e-2F);
        REQUIRE(std::abs(batch[i] - g(xs[i])) < 1e-2F);
    }
}

TEST_CASE("Scalar double input with array output routes through ND path", "[treeweave][complex][1d]") {
    // Issue #23 part (a): a plain `double` domain with vector output no longer
    // trips the scalar-input static_assert — it auto-wraps to std::array<T,1>.
    auto       h   = [](double x) -> std::array<double, 2> { return {x * x, std::cos(x)}; };
    auto       fn  = fit(h, 0.0, 1.0, /*tol=*/1e-10);
    const auto out = fn(0.37);
    REQUIRE(out[0] == Catch::Approx(0.37 * 0.37).epsilon(1e-8));
    REQUIRE(out[1] == Catch::Approx(std::cos(0.37)).epsilon(1e-8));
}
// NOLINTEND(cert-msc51-cpp,cert-msc32-c)
