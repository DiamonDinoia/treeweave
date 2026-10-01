// test_greens.cpp: Green's function / layer-potential kernels.

#include "treeweave/detail/errors.hpp"
#include "treeweave/detail/tol_kind.hpp"
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <random>

#include <treeweave/treeweave.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using treeweave::fit;
using treeweave::options;

// fit(lambda, a, b, tol) must select the default Degree=7 specialisation.
TEST_CASE("fit canonical overload type", "[treeweave][api][type]") {
    auto f         = [](double x) { return x; };
    using actual_t = decltype(treeweave::fit(f, 0.0, 1.0, 1e-8));
    STATIC_REQUIRE(actual_t::degree == 7);
    STATIC_REQUIRE(actual_t::input_dim == 1);
    STATIC_REQUIRE(actual_t::output_dim == 1);
}

namespace {

constexpr double kTol = 1e-8;

// Sample interior points away from boundary to avoid the trivial boundary
// artefact and report max relative error.
template <class Exact, class Approx>
auto max_rel_err_1d(Exact &&ex, Approx &&ap, double a, double b, int n, unsigned seed) -> double {
    std::mt19937                           gen(seed);
    std::uniform_real_distribution<double> d(a, b);
    double                                 mx = 0.0;
    for (int i = 0; i < n; ++i) {
        const double x = d(gen);
        const double y = ex(x);
        if (std::abs(y) > 1e-14)
            mx = std::max(mx, std::abs((y - ap(x)) / y));
    }
    return mx;
}

template <std::size_t DIM, class Exact, class Approx>
auto max_rel_err_nd(Exact &&ex, Approx &&ap, std::array<double, DIM> a, std::array<double, DIM> b, int n, unsigned seed)
    -> double {
    std::mt19937                                            gen(seed);
    std::array<std::uniform_real_distribution<double>, DIM> dists;
    for (std::size_t i = 0; i < DIM; ++i)
        dists[i] = std::uniform_real_distribution<double>(a[i], b[i]);
    double mx = 0.0;
    for (int i = 0; i < n; ++i) {
        std::array<double, DIM> x{};
        for (std::size_t k = 0; k < DIM; ++k)
            x[k] = dists[k](gen);
        const double y = ex(x);
        if (std::abs(y) > 1e-14) {
            const double yh = ap(x);
            mx              = std::max(mx, std::abs((y - yh) / y));
        }
    }
    return mx;
}

// max|p - f| / max|f| on n random points: what the default `RelativeMax` bounds where |f| dips toward zero.
template <std::size_t DIM, class Exact, class Approx>
auto max_norm_err_nd(Exact &&ex, Approx &&ap, std::array<double, DIM> a, std::array<double, DIM> b, int n,
                     unsigned seed) -> double {
    std::mt19937                                            gen(seed);
    std::array<std::uniform_real_distribution<double>, DIM> dists;
    for (std::size_t i = 0; i < DIM; ++i)
        dists[i] = std::uniform_real_distribution<double>(a[i], b[i]);
    double err = 0.0, fmax = 0.0;
    for (int i = 0; i < n; ++i) {
        std::array<double, DIM> x{};
        for (std::size_t k = 0; k < DIM; ++k)
            x[k] = dists[k](gen);
        const double y  = ex(x);
        const double yh = ap(x);
        // std::max(a, NaN) keeps a, so fold NaN/inf up front or it is dropped.
        if (!std::isfinite(yh) || !std::isfinite(y))
            return std::numeric_limits<double>::infinity();
        err  = std::max(err, std::abs(y - yh));
        fmax = std::max(fmax, std::abs(y));
    }
    // Relative to max|f|; only an all-zero exact function falls back to the absolute error.
    return fmax > 0.0 ? err / fmax : err;
}

} // namespace

TEST_CASE("NaN propagates through max_norm_err_nd, all-zero exact is safe", "[treeweave][greens][helpers]") {
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    auto       z   = [](std::array<double, 2>) { return 0.0; };
    auto       n   = [nan](std::array<double, 2>) { return nan; };
    const std::array<double, 2> lo{0.0, 0.0}, hi{1.0, 1.0};
    // std::max(err, NaN) keeps err: without a guard the NaN eval scores 0.
    const double err_nan_approx = max_norm_err_nd<2>(z, n, lo, hi, 100, 3);
    const double err_nan_exact  = max_norm_err_nd<2>(n, z, lo, hi, 100, 3);
    CHECK(std::isinf(err_nan_approx));
    CHECK(std::isinf(err_nan_exact));
    // All-zero exact must not divide by zero: the result is plain max|p|.
    auto         two      = [](std::array<double, 2>) { return 2.0; };
    const double err_zero = max_norm_err_nd<2>(z, z, lo, hi, 100, 3);
    const double err_two  = max_norm_err_nd<2>(z, two, lo, hi, 100, 3);
    CHECK(err_zero == 0.0);
    CHECK(err_two == 2.0);
    // |f| < 1: the error stays relative to max|f| (5e-10 / 1e-6), it is not clamped to an absolute one.
    auto small   = [](std::array<double, 2>) { return 1e-6; };
    auto shifted = [](std::array<double, 2>) { return 1e-6 + 5e-10; };
    CHECK(max_norm_err_nd<2>(small, shifted, lo, hi, 100, 3) == Catch::Approx(5e-4).epsilon(1e-3));
}

TEST_CASE("1D Laplace 1/|x| on [0.1, 2]", "[treeweave][greens][laplace]") {
    auto f  = [](double x) { return 1.0 / x; };
    auto fn = fit(f, 0.1, 2.0, kTol);
    REQUIRE(max_rel_err_1d(f, fn, 0.11, 1.99, 2000, 1) < 50 * kTol);
}

TEST_CASE("2D Laplace 1/r on shifted disk", "[treeweave][greens][laplace][2d]") {
    // Domain shifted off origin so 1/r is smooth.
    auto f = [](std::array<double, 2> x) -> std::array<double, 1> {
        return {1.0 / std::sqrt(x[0] * x[0] + x[1] * x[1])};
    };
    auto                        ex = [](std::array<double, 2> x) { return 1.0 / std::sqrt(x[0] * x[0] + x[1] * x[1]); };
    std::array<double, 2> const a{0.2, 0.2};
    std::array<double, 2> const b{1.5, 1.5};
    auto                        fn = fit(f, a, b, kTol);
    auto                        ap = [&](std::array<double, 2> x) { return fn(x)[0]; };
    REQUIRE(max_rel_err_nd<2>(ex, ap, {0.21, 0.21}, {1.49, 1.49}, 2000, 2) < 50 * kTol);
}

TEST_CASE("3D Laplace 1/r on shifted box", "[treeweave][greens][laplace][3d]") {
    auto f = [](std::array<double, 3> x) -> std::array<double, 1> {
        return {1.0 / std::sqrt(x[0] * x[0] + x[1] * x[1] + x[2] * x[2])};
    };
    auto ex = [](std::array<double, 3> x) { return 1.0 / std::sqrt(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]); };
    std::array<double, 3> const a{0.2, 0.2, 0.2};
    std::array<double, 3> const b{1.5, 1.5, 1.5};
    auto                        fn = fit(f, a, b, kTol);
    auto                        ap = [&](std::array<double, 3> x) { return fn(x)[0]; };
    REQUIRE(max_rel_err_nd<3>(ex, ap, {0.21, 0.21, 0.21}, {1.49, 1.49, 1.49}, 1500, 3) < 50 * kTol);
}

TEST_CASE("2D Laplace log|x| on shifted disk", "[treeweave][greens][log]") {
    auto f = [](std::array<double, 2> x) -> std::array<double, 1> {
        return {std::log(std::sqrt(x[0] * x[0] + x[1] * x[1]))};
    };
    auto ex = [](std::array<double, 2> x) { return std::log(std::sqrt(x[0] * x[0] + x[1] * x[1])); };
    std::array<double, 2> const a{0.1, -0.5};
    std::array<double, 2> const b{1.5, 0.9};
    auto                        fn = fit(f, a, b, kTol);
    auto                        ap = [&](std::array<double, 2> x) { return fn(x)[0]; };
    REQUIRE(max_norm_err_nd<2>(ex, ap, {0.11, -0.49}, {1.49, 0.89}, 2000, 4) <= 10 * kTol); // log r = 0 on r = 1
}

TEST_CASE("1D Yukawa on [0.1, 3], k=1 and k=10", "[treeweave][greens][yukawa]") {
    for (double const k : {1.0, 10.0}) {
        auto f  = [k](double x) { return std::exp(-k * x) / x; };
        auto fn = fit(f, 0.1, 3.0, kTol);
        auto ex = [&f](std::array<double, 1> x) { return f(x[0]); };
        auto ap = [&fn](std::array<double, 1> x) { return fn(x[0]); };
        REQUIRE(max_norm_err_nd<1>(ex, ap, {0.11}, {2.99}, 2000, 5) <= 10 * kTol); // f spans 14 decades at k = 10
    }
}

TEST_CASE("3D Yukawa on shifted box, k=1 and k=10", "[treeweave][greens][yukawa][3d]") {
    for (double const k : {1.0, 10.0}) {
        auto f = [k](std::array<double, 3> x) -> std::array<double, 1> {
            const double r = std::sqrt(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]);
            return {std::exp(-k * r) / r};
        };
        auto ex = [k](std::array<double, 3> x) {
            const double r = std::sqrt(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]);
            return std::exp(-k * r) / r;
        };
        std::array<double, 3> const a{0.2, 0.2, 0.2};
        std::array<double, 3> const b{1.5, 1.5, 1.5};
        // Fits within the auto dimension-scaled default (16 MiB in 3D).
        auto fn = fit(f, a, b, kTol);
        auto ap = [&](std::array<double, 3> x) { return fn(x)[0]; };
        REQUIRE(max_norm_err_nd<3>(ex, ap, {0.21, 0.21, 0.21}, {1.49, 1.49, 1.49}, 1000, 6) <= 10 * kTol);
    }
}

TEST_CASE("Stokeslet tensor diagonal 2D", "[treeweave][greens][stokes]") {
    // G_ii(x) = 1/|x| + x_i^2 / |x|^3 (constants absorbed).
    auto f = [](std::array<double, 2> x) -> std::array<double, 2> {
        const double r2     = x[0] * x[0] + x[1] * x[1];
        const double r      = std::sqrt(r2);
        const double inv_r  = 1.0 / r;
        const double inv_r3 = inv_r / r2;
        return {inv_r + x[0] * x[0] * inv_r3, inv_r + x[1] * x[1] * inv_r3};
    };
    std::array<double, 2> const a{0.2, 0.2};
    std::array<double, 2> const b{1.5, 1.5};
    auto                        fn = fit(f, a, b, kTol);

    std::mt19937                           gen(7);
    std::uniform_real_distribution<double> dx(0.21, 1.49);
    std::uniform_real_distribution<double> dy(0.21, 1.49);
    double                                 mx = 0.0;
    for (int i = 0; i < 1500; ++i) {
        std::array<double, 2> const x{dx(gen), dy(gen)};
        const auto                  exact  = f(x);
        const auto                  approx = fn(x);
        for (std::size_t k = 0; k < 2; ++k) {
            if (std::abs(exact[k]) > 1e-14)
                mx = std::max(mx, std::abs((exact[k] - approx[k]) / exact[k]));
        }
    }
    REQUIRE(mx < 100 * kTol);
}

TEST_CASE("Oscillatory sin(k r)/r in 1D", "[treeweave][greens][oscillatory]") {
    for (double const k : {5.0, 25.0}) {
        auto f  = [k](double x) { return std::sin(k * x) / x; };
        auto fn = fit(f, 0.01, 5.0, kTol);
        auto ex = [&f](std::array<double, 1> x) { return f(x[0]); };
        auto ap = [&fn](std::array<double, 1> x) { return fn(x[0]); };
        REQUIRE(max_norm_err_nd<1>(ex, ap, {0.02}, {4.99}, 4000, 8) <= 10 * kTol); // zeros of sin(k x)
    }
}

TEST_CASE("Oscillatory sin(k r)/r in 3D", "[treeweave][greens][oscillatory][3d]") {
    for (double const k : {5.0}) {
        auto f = [k](std::array<double, 3> x) -> std::array<double, 1> {
            const double r = std::sqrt(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]);
            return {std::sin(k * r) / r};
        };
        auto ex = [k](std::array<double, 3> x) {
            const double r = std::sqrt(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]);
            return std::sin(k * r) / r;
        };
        std::array<double, 3> const a{0.2, 0.2, 0.2};
        std::array<double, 3> const b{1.5, 1.5, 1.5};
        // 3D oscillatory near-singular fit refines aggressively; opt in
        // to a generous budget rather than the strict 4 MiB default.
        auto fn = fit(f, a, b, kTol, treeweave::options{.max_memory_mib = 64});
        auto ap = [&](std::array<double, 3> x) { return fn(x)[0]; };
        REQUIRE(max_rel_err_nd<3>(ex, ap, {0.21, 0.21, 0.21}, {1.49, 1.49, 1.49}, 1000, 9) < 500 * kTol);
    }
}

TEST_CASE("3D anisotropic gaussian on [-1,1]^3", "[treeweave][greens][smooth][3d]") {
    auto f = [](std::array<double, 3> x) -> std::array<double, 1> {
        return {std::exp(-0.5 * x[0] * x[0] - 1.5 * x[1] * x[1] - 2.0 * x[2] * x[2])};
    };
    auto ex = [](std::array<double, 3> x) {
        return std::exp(-0.5 * x[0] * x[0] - 1.5 * x[1] * x[1] - 2.0 * x[2] * x[2]);
    };
    std::array<double, 3> const a{-1.0, -1.0, -1.0};
    std::array<double, 3> const b{1.0, 1.0, 1.0};
    auto                        fn = fit(f, a, b, kTol);
    auto                        ap = [&](std::array<double, 3> x) { return fn(x)[0]; };
    REQUIRE(max_rel_err_nd<3>(ex, ap, {-0.99, -0.99, -0.99}, {0.99, 0.99, 0.99}, 1500, 10) < 100 * kTol);
}

TEST_CASE("MaxDepthExceeded on tight max_depth", "[treeweave][greens][maxdepth]") {
    auto f = [](double x) { return std::sin(50.0 * x) / x; };
    REQUIRE_THROWS_AS(fit(f, 0.01, 5.0, kTol, options{.max_depth = 4}), treeweave::MaxDepthExceeded);
}
