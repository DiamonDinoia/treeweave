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

// Deterministic seeds in tests are intentional: the max-rel-error sweeps need
// reproducible inputs.
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

// max|p - f| / max|f| on n + 1 uniform points of [a, b): the quantity the default `RelativeMax` bounds.
template <class F1, class F2>
auto max_norm_err_1d(F1 &&exact, F2 &&approx, double a, double b, int n) -> double {
    double err = 0.0, fmax = 0.0;
    for (int i = 0; i <= n; ++i) {
        const double x  = std::min(a + (b - a) * i / n, std::nextafter(b, a));
        const double y  = exact(x);
        const double yh = approx(x);
        // std::max(err, NaN) keeps err, so fold NaN/inf up front or it is dropped.
        if (!std::isfinite(yh) || !std::isfinite(y))
            return std::numeric_limits<double>::infinity();
        err  = std::max(err, std::abs(yh - y));
        fmax = std::max(fmax, std::abs(y));
    }
    // Relative to max|f|; an all-zero reference matches `RelativeMax`: 0 iff the error is 0, else inf.
    if (fmax > 0.0)
        return err / fmax;
    return err == 0.0 ? 0.0 : std::numeric_limits<double>::infinity();
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

TEST_CASE("degree-1 tail tolerance reads only the coefficient it has", "[treeweave][tail]") {
    // A degree-1 fit holds a single coefficient, so tail_error_exceeds_tol has
    // one element to read, not two. The read past the end is invisible without
    // a sanitizer: the ASan CI rows are what makes this case fail.
    auto f = [](double x) { return x * x; };

    auto fn =
        fit<1>(f, 0.0, 1.0, /*tol=*/1e-3,
               options{.tol_kind = treeweave::TolKind::AbsoluteTail, .max_depth = 4, .allow_max_depth_leaves = true});
    REQUIRE(std::isfinite(fn(0.5)));
}

TEST_CASE("tail tolerance reads the highest-degree coefficients", "[treeweave][tail]") {
    // coeffs() is Horner order; the cubic's top coefficients are 0, c_0 = 1: the wrong end splits.
    const auto opts  = options{.tol_kind = treeweave::TolKind::AbsoluteTail, .max_depth = 6};
    auto       cubic = fit<8>([](double x) { return 1.0 + x - 2.0 * x * x * x; }, -1.0, 1.0, /*tol=*/1e-13, opts);
    REQUIRE(cubic.num_leaves() == 1);
    // exp: the degree-7 coefficient 1/7! exceeds tol, so the root must split.
    auto ex = fit<8>([](double x) { return std::exp(x); }, -1.0, 1.0, /*tol=*/1e-10, opts);
    REQUIRE(ex.num_leaves() > 1);
}

TEST_CASE("RelativeTail is scale invariant, AbsoluteTail is not", "[treeweave][tail]") {
    constexpr double tol    = 1e-10;
    auto             leaves = [](double s, treeweave::TolKind k) {
        return fit<8>([s](double x) { return s * std::exp(x); }, -1.0, 1.0, tol, options{.tol_kind = k}).num_leaves();
    };
    using treeweave::TolKind;
    const auto rel_big = leaves(1e6, TolKind::RelativeTail), rel_small = leaves(1e-6, TolKind::RelativeTail);
    // AbsoluteTail at 1e6 never converges: 1e-10 < coefficient noise 1e6 * eps. The control uses 1e3.
    const auto abs_big = leaves(1e3, TolKind::AbsoluteTail), abs_small = leaves(1e-6, TolKind::AbsoluteTail);
    INFO("RelativeTail " << rel_big << " / " << rel_small << ", AbsoluteTail " << abs_big << " / " << abs_small);
    REQUIRE(rel_big == rel_small);
    REQUIRE(abs_big != abs_small); // control: an unscaled kind does see the scale

    for (const double s : {1e6, 1e-6}) {
        auto         f   = [s](double x) { return s * std::exp(x); };
        auto         fn  = fit<8>(f, -1.0, 1.0, tol, options{.tol_kind = TolKind::RelativeTail});
        const double err = max_norm_err_1d(f, fn, -1.0, 1.0, 1000);
        INFO("scale " << s << ": max|p-f|/max|f| = " << err);
        REQUIRE(err <= 10 * tol);
    }

    // All-zero panel: max_k |c_k| = 0 counts as converged, and the fit evaluates to exactly 0.
    auto zero_tail = fit<8>([](double) { return 0.0; }, -1.0, 1.0, tol, options{.tol_kind = TolKind::RelativeTail});
    REQUIRE(zero_tail.num_leaves() == 1);
    for (const double x : {-0.75, 0.0, 0.25, 0.9})
        REQUIRE(zero_tail(x) == 0.0);
}

TEST_CASE("RelativeMax is scale invariant, AbsoluteMax is not", "[treeweave][relmax]") {
    constexpr double tol    = 1e-10;
    auto             leaves = [](double s, treeweave::TolKind k) {
        return fit<8>([s](double x) { return s * std::exp(x); }, -1.0, 1.0, tol, options{.tol_kind = k}).num_leaves();
    };
    using treeweave::TolKind;
    const auto rel_big = leaves(1e6, TolKind::RelativeMax), rel_small = leaves(1e-6, TolKind::RelativeMax);
    // AbsoluteMax at 1e6 never converges: 1e-10 < sample noise 1e6 * eps. The control uses 1e3.
    const auto abs_big = leaves(1e3, TolKind::AbsoluteMax), abs_small = leaves(1e-6, TolKind::AbsoluteMax);
    INFO("RelativeMax " << rel_big << " / " << rel_small << ", AbsoluteMax " << abs_big << " / " << abs_small);
    REQUIRE(rel_big == rel_small);
    REQUIRE(abs_big != abs_small); // control: an unscaled kind does see the scale

    for (const double s : {1e6, 1e-6}) {
        auto         f   = [s](double x) { return s * std::exp(x); };
        auto         fn  = fit<8>(f, -1.0, 1.0, tol);
        const double err = max_norm_err_1d(f, fn, -1.0, 1.0, 1000);
        INFO("scale " << s << ": max|p-f|/max|f| = " << err);
        REQUIRE(err <= 10 * tol);
    }

    // All-zero panel: max|p - f| = 0 = tol * max|f| counts as converged, and the fit evaluates to exactly 0.
    auto zero_max = fit<8>([](double) { return 0.0; }, -1.0, 1.0, tol);
    REQUIRE(zero_max.num_leaves() == 1);
    for (const double x : {-0.75, 0.0, 0.25, 0.9})
        REQUIRE(zero_max(x) == 0.0);
}

TEST_CASE("RelativeMax still rejects an unconverged panel", "[treeweave][relmax]") {
    auto f      = [](double x) { return std::exp(10.0 * x); };
    auto forced = fit(f, -1.0, 1.0, 1e-13, options{.max_depth = 0, .allow_max_depth_leaves = true});
    REQUIRE(forced.non_converged_panels().size() == 1);
    auto fn = fit(f, -1.0, 1.0, 1e-13);
    INFO("leaves " << fn.num_leaves());
    REQUIRE(fn.non_converged_panels().empty());
}

TEST_CASE("RelativeMax fits a function with zeros", "[treeweave][relmax]") {
    // The largest |f| over the domain, not |f(x)|, normalises the error: zeros of cos are no obstacle.
    constexpr double tol = 1e-13;
    auto             f   = [](double x) { return std::cos(x); };
    for (const double b : {101.0, 1001.0}) {
        auto         fn  = fit(f, 1.0, b, tol);
        const double err = max_norm_err_1d(f, fn, 1.0, b, 20000);
        INFO("[1, " << b << "): leaves " << fn.num_leaves() << ", max|p-f|/max|f| = " << err);
        REQUIRE(err <= 10 * tol);
    }
}

TEST_CASE("RelativePointwise is per-sample, RelativeMax is not", "[treeweave][relpointwise]") {
    using treeweave::TolKind;
    // exp(-50x) decays ~22 orders of magnitude over [0, 1]: any fixed
    // max-abs error that passes RelativeMax (scaled by max|f| = 1) is a huge
    // relative error where |f| is small, so RelativePointwise refines more.
    auto             f        = [](double x) { return std::exp(-50.0 * x); };
    constexpr double tol      = 1e-8;
    auto             fn_max   = fit<8>(f, 0.0, 1.0, tol, options{.tol_kind = TolKind::RelativeMax});
    auto             fn_point = fit<8>(f, 0.0, 1.0, tol, options{.tol_kind = TolKind::RelativePointwise});
    INFO("RelativeMax leaves " << fn_max.num_leaves() << ", RelativePointwise leaves " << fn_point.num_leaves());
    REQUIRE(fn_point.num_leaves() > fn_max.num_leaves());
    // And the pointwise fit honours the per-sample contract on a dense sweep.
    const double rel_err = max_rel_err_1d(f, fn_point, 0.0, 1.0, N_SAMPLE);
    INFO("max |p-f|/|f| = " << rel_err);
    REQUIRE(rel_err <= 10 * tol);

    // Zero reference, zero error: an all-zero f converges in one leaf, exactly.
    auto zero = fit<8>([](double) { return 0.0; }, -1.0, 1.0, 1e-10, options{.tol_kind = TolKind::RelativePointwise});
    REQUIRE(zero.num_leaves() == 1);
    for (const double x : {-0.75, 0.0, 0.25, 0.9})
        REQUIRE(zero(x) == 0.0);

    // Zero reference, nonzero error: f = x - 0.0625 is exactly 0 at the first
    // cell-centre sample (0.0625 on the degree-8 8-per-panel grid of [0, 1]).
    // The polynomial roundoff there is nonzero, err <= tol*|f| = 0 fails, so
    // RelativePointwise keeps subdividing while RelativeMax accepts the root.
    auto          g          = [](double x) { return x - 0.0625; };
    constexpr int forced_max = 4;
    auto          fn_g_point = fit<8>(
        g, 0.0, 1.0, 1e-12,
        options{.tol_kind = TolKind::RelativePointwise, .max_depth = forced_max, .allow_max_depth_leaves = true});
    auto fn_g_max = fit<8>(g, 0.0, 1.0, 1e-12, options{.tol_kind = TolKind::RelativeMax});
    INFO("pointwise leaves " << fn_g_point.num_leaves() << " vs RelativeMax " << fn_g_max.num_leaves());
    REQUIRE(fn_g_point.num_leaves() > fn_g_max.num_leaves());
    // With the root panel forced (max_depth 0), the zero sits on its sample
    // grid, so RelativePointwise never converges while RelativeMax does.
    auto root_point =
        fit<8>(g, 0.0, 1.0, 1e-12,
               options{.tol_kind = TolKind::RelativePointwise, .max_depth = 0, .allow_max_depth_leaves = true});
    auto root_max = fit<8>(g, 0.0, 1.0, 1e-12,
                           options{.tol_kind = TolKind::RelativeMax, .max_depth = 0, .allow_max_depth_leaves = true});
    REQUIRE(root_point.non_converged_panels().size() == 1);
    REQUIRE(root_max.non_converged_panels().empty());
}

TEST_CASE("RelativeMax on cos far from the origin: tol 1e-14 converges, tol 2e-15 is unreachable",
          "[treeweave][relmax]") {
    // Degree-8 polyfit Horner evaluation of cos near x ~ 64 bottoms out at
    // max|p - f| ~ 3e-15 (max_abs_f = 1). tol 1e-14 lands above the floor;
    // tol 2e-15 is below it, so the paneler subdivides until the leaf memory
    // budget (4 MiB default) fires before convergence.
    auto f = [](double x) { return std::cos(x); };

    constexpr double tol_ok = 1e-14;
    auto             fn     = fit(f, 1.0, 101.0, tol_ok);
    const double     err    = max_norm_err_1d(f, fn, 1.0, 101.0, 20000);
    INFO("tol " << tol_ok << ": leaves " << fn.num_leaves() << ", max|p-f|/max|f| = " << err);
    REQUIRE(err <= 10 * tol_ok);

    REQUIRE_THROWS_AS(fit(f, 1.0, 101.0, /*tol=*/2e-15), treeweave::MemoryBudgetExceeded);
}

TEST_CASE("RelativeMax normalises by the domain-wide max|f|, not per panel", "[treeweave][relmax]") {
    // exp(-50x) falls to 2e-22 at x = 1. A global normaliser resolves the tail only to tol * max|f| absolute,
    // so the pointwise relative error there exceeds tol. A per-panel normaliser needs 64 leaves and gets 6e-9 at x = 1.
    constexpr double tol = 1e-8;
    auto             f   = [](double x) { return std::exp(-50.0 * x); };
    auto             fn  = fit(f, 0.0, 1.0, tol);
    const double     err = max_norm_err_1d(f, fn, 0.0, 1.0, 20000);
    const double     rel = std::abs(fn(1.0) - f(1.0)) / f(1.0);
    INFO("leaves " << fn.num_leaves() << ", max|p-f|/max|f| = " << err << ", relative error at 1 = " << rel);
    REQUIRE(err <= 10 * tol);
    REQUIRE(fn.num_leaves() <= 16);
    REQUIRE(rel > tol);
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
    // Check away from the exact step, where subdivision gives good accuracy.
    // rel_err blows up near zero-crossings, so use abs.
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
    // max|f| = 1 and the default tol is relative to it, so the error bound is absolute; the tails fall to 1e-11.
    std::mt19937                           gen(1);
    std::uniform_real_distribution<double> d(0.0, 1.0);
    bool                                   ok = true; // `<=` is false for NaN; std::max would drop it
    for (int i = 0; i < 5000; ++i) {
        const std::array<double, 2> x{d(gen), d(gen)};
        ok = ok && std::abs(exact(x) - fn(x)[0]) <= 1e-9;
    }
    REQUIRE(ok);
}

TEST_CASE("sqrt|x - 0.5| -- not C^1, max_depth guards runaway", "[treeweave][sharp]") {
    auto f = [](double x) { return std::sqrt(std::abs(x - 0.5)); };
    REQUIRE_THROWS_AS(
        fit<8>(f, 0.0, 1.0, /*tol=*/1e-10, options{.tol_kind = treeweave::TolKind::AbsoluteMax, .max_depth = 4}),
        treeweave::MaxDepthExceeded);
    // Generous max_depth + accept best-effort leaves at the singular panel.
    // `sqrt|x-0.5|` is not C^1 at 0.5, so that panel never reaches tol=1e-10.
    // Only the rest of the domain has to be usable.
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

    // Batch path uses polyfit's SIMD Horner, scalar path uses scalar Horner,
    // identical mathematically but FMA reordering can drop a ULP.
    constexpr double ulp = std::numeric_limits<double>::epsilon();
    for (std::size_t i = 0; i < static_cast<std::size_t>(N); ++i) {
        const double single = fn(xs[i]);
        REQUIRE(std::abs(single - batch[i]) <= 8.0 * ulp * std::max(1.0, std::abs(single)));
    }
}

TEST_CASE("Empty batch is a no-op on null and sentinel pointers", "[treeweave][batch][empty]") {
    // The MATLAB MEX stub passes NULL with n = 0 when Xflat is empty
    // (fit.eval(zeros(0, dim))). A heap write on this path corrupts the
    // glibc arenas and aborts the host process at MEX unload.
    auto fn1 = fit<8>([](double x) { return std::sin(4.0 * x); }, 0.0, 1.0, /*tol=*/1e-10);
    fn1(static_cast<const double *>(nullptr), static_cast<double *>(nullptr), 0);
    double x_sentinel = 0.5, y_sentinel = 1.25;
    fn1(&x_sentinel, &y_sentinel, 0);
    REQUIRE(y_sentinel == 1.25);
    fn1.sorted(nullptr, nullptr, 0);
    fn1.sorted(&x_sentinel, &y_sentinel, 0);
    REQUIRE(y_sentinel == 1.25);

    // 2D: the configuration of the reported crash.
    auto fn2 = fit<8>([](std::array<double, 2> x) -> std::array<double, 1> { return {x[0] * x[1]}; },
                      std::array{0.0, -1.0}, std::array{8.0, 1.0}, /*tol=*/1e-10);
    fn2(nullptr, nullptr, 0);
    double in2[2] = {0.5, 0.5};
    double out2   = 2.5;
    fn2(in2, &out2, 0);
    REQUIRE(out2 == 2.5);

    // SoA output path (output_dim > 1).
    auto fn3 = fit<8>([](std::array<double, 1> x) -> std::array<double, 2> { return {x[0], x[0] * x[0]}; },
                      std::array{0.0}, std::array{1.0}, /*tol=*/1e-10);
    fn3(nullptr, std::array<double *, 2>{nullptr, nullptr}, 0);
    double in3[1]  = {0.5};
    double out3[2] = {1.25, 3.75};
    fn3(in3, std::array<double *, 2>{&out3[0], &out3[1]}, 0);
    REQUIRE(out3[0] == 1.25);
    REQUIRE(out3[1] == 3.75);
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
// outputs, AoS and SoA coincide: users pass `value_type*` directly.

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
    // The batch path tiles when n_trg exceeds tile_K (default 64 K). Pin
    // the boundary by feeding a batch large enough to span multiple tiles
    // and assert per-point agreement with the scalar path.
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
    // (deg=8 in 3D), so a 1 MiB budget caps at ~170 leaves before bailing,
    // well below what the smooth-tol target would otherwise pursue.
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
    REQUIRE(max_norm_err_1d(f, fn, a, b, N_SAMPLE) <= 1e-8);
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
    // Wide non-square box with mismatched per-axis scales, exercises the
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
    // a looser tol (~2 MiB tree, ~13 s at -O0, fast even on the slowest Debug
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
    // specialization: eval_scatter_sorted takes a span of like pointers.
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

// Pin the leaf-table build threshold: 1D fits that land at max_depth up
// to 16 must still get a table rather than the descent fallback.
TEST_CASE("Leaf-table built at widened depth threshold", "[treeweave][leaf-table]") {
    // tanh500 at tol=1e-12 deg=6 lands at depth ~16; the table threshold
    // must cover that depth.
    auto fn = fit<6>([](double x) { return std::tanh(500.0 * x); }, -1.0, 1.0, /*tol=*/1e-12);
    REQUIRE(fn.has_fast_quantize());

    // A shallow fit must keep the fast path too.
    auto shallow = fit<8>([](double x) { return 1.0 / (1.0 + 25.0 * x * x); }, -1.0, 1.0, /*tol=*/1e-10);
    REQUIRE(shallow.has_fast_quantize());
}

// min_uniform_depth forces leaf-table live on smooth fns; tol-based stops at depth 1-2 and skips it.
TEST_CASE("min_uniform_depth forces uniform refinement and builds leaf table",
          "[treeweave][min_uniform_depth][leaf-table]") {
    auto f  = [](double x) { return std::cos(x); };
    auto fn = fit<8>(f, 0.0, 1.0, /*tol=*/1e-3, options{.min_uniform_depth = 2});

    // Leaf table is live: driving condition for the SIMD-quantize
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

    // Default (no forcing) on the same fit lands at a single leaf,
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
// (gaps in fit-id space: caller pads n_fits to cover the max id).
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
    constexpr double tol_d = 1e-5; // same value, double, passed to fit<>(tol)
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
    // Issue #23: scalar `double` in, `std::complex<double>` out, no manual
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
        // The modulus of the fit tracks the modulus of the target, i.e. the
        // complex value (not just each real channel) is a faithful interpolant.
        REQUIRE(std::abs(batch[i]) == Catch::Approx(std::abs(g(xs[i]))).margin(1e-8));
    }
}

TEST_CASE("Complex-valued scalar fit: float value_type", "[treeweave][complex][1d]") {
    // Same ergonomic path on the f32 leaf math (value_type == float): scalar
    // float in, std::complex<float> out, batch over a std::complex<float> buffer.
    using cf = std::complex<float>;
    auto g   = [](float x) -> cf { return {std::sin(3.0F * x), std::exp(-x)}; };
    // 1e-4 is a realistic f32 target: a tighter tol hits float epsilon in the
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
    // trips the scalar-input static_assert, it auto-wraps to std::array<T,1>.
    auto       h   = [](double x) -> std::array<double, 2> { return {x * x, std::cos(x)}; };
    auto       fn  = fit(h, 0.0, 1.0, /*tol=*/1e-10);
    const auto out = fn(0.37);
    REQUIRE(out[0] == Catch::Approx(0.37 * 0.37).epsilon(1e-8));
    REQUIRE(out[1] == Catch::Approx(std::cos(0.37)).epsilon(1e-8));
}

TEST_CASE("NaN propagates through max_norm_err_1d, all-zero exact is safe", "[treeweave][helpers]") {
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    auto       z   = [](double) { return 0.0; };
    CHECK(std::isinf(max_norm_err_1d(z, [nan](double) { return nan; }, 0.0, 1.0, 100)));
    CHECK(std::isinf(max_norm_err_1d([nan](double) { return nan; }, z, 0.0, 1.0, 100)));
    // All-zero exact must not divide by zero: the result is 0 when the error is 0, else inf.
    CHECK(max_norm_err_1d(z, z, 0.0, 1.0, 100) == 0.0);
    CHECK(std::isinf(max_norm_err_1d(z, [](double) { return 2.0; }, 0.0, 1.0, 100)));
    // |f| < 1: the error stays relative to max|f| (5e-10 / 1e-6), it is not clamped to an absolute one.
    CHECK(max_norm_err_1d([](double) { return 1e-6; }, [](double) { return 1e-6 + 5e-10; }, 0.0, 1.0, 100) ==
          Catch::Approx(5e-4).epsilon(1e-3));
}

TEST_CASE("Boundary sqrt singularity throws", "[treeweave][singularities]") {
    auto singular = [](double x) { return std::sqrt(x); };
    REQUIRE_THROWS_AS(treeweave::fit(singular, 0.0, 1.0, 1e-13), treeweave::MaxDepthExceeded);
}

namespace {
// Stand-in for a polyfit: tail_error_exceeds_tol reads only the type aliases, NCOEFFS and coeffs().
struct FakeFit {
    using InputType  = double;
    using OutputType = double;
    static constexpr std::size_t NCOEFFS = 8;
    std::array<double, NCOEFFS>  c{};
    [[nodiscard]] auto coeffs() const -> const std::array<double, NCOEFFS> & { return c; }
};

// Stand-in for a polyfit on the sampled path: sample_error_exceeds_tol reads
// the type aliases and calls operator()(sample_point) for grid points.
struct FakeSampleFit {
    using InputType  = double;
    using OutputType = double;
    std::function<double(double)> approx;
    auto                          operator()(double x) const -> double { return approx(x); }
};
} // namespace

TEST_CASE("the tail check rejects any non-finite coefficient", "[treeweave][tail][nonfinite]") {
    using treeweave::TolKind;
    using treeweave::detail::tail_error_exceeds_tol;
    constexpr double inf = std::numeric_limits<double>::infinity();
    constexpr double nan = std::numeric_limits<double>::quiet_NaN();
    for (const auto kind : {TolKind::RelativeTail, TolKind::AbsoluteTail}) {
        FakeFit ok;
        ok.c.back() = 1.0; // tail (c0, c1) is 0: converged
        REQUIRE(!tail_error_exceeds_tol(kind, 1e-10, ok));
        // Horner order: c0, c1 are the tail; c2..c7 are not, but the bad one must still reject.
        // A finite coefficient after it must not hide it (std::max drops a NaN).
        for (std::size_t i = 0; i < FakeFit::NCOEFFS; ++i) {
            for (const double bad : {nan, inf}) {
                FakeFit f = ok;
                f.c[i]    = bad;
                INFO("kind " << static_cast<int>(kind) << ", coefficient " << i << " = " << bad);
                REQUIRE(tail_error_exceeds_tol(kind, 1e-10, f));
            }
        }
    }
}

TEST_CASE("a non-finite sample never poisons the RelativeMax normaliser", "[treeweave][relmax][nonfinite]") {
    // f is non-finite on [0, 0.5) and a wiggle no degree-8 panel of width 0.25 fits on [0.5, 2].
    // If the inf sample raised max|f| to inf, the wiggle panels would pass unexamined.
    for (const bool use_nan : {false, true}) {
        auto f = [use_nan](double x) {
            if (x < 0.5)
                return use_nan ? std::numeric_limits<double>::quiet_NaN() : std::numeric_limits<double>::infinity();
            return std::sin(200.0 * x);
        };
        auto fn = fit(f, 0.0, 2.0, 1e-8, options{.max_depth = 3, .allow_max_depth_leaves = true});
        bool bad_low = false, bad_high = false;
        for (const auto &p : fn.non_converged_panels()) {
            bad_low  = bad_low || p.b[0] <= 0.5;
            bad_high = bad_high || p.a[0] >= 0.5;
        }
        INFO((use_nan ? "NaN branch" : "inf branch"));
        REQUIRE(bad_low);  // the non-finite region is flagged
        REQUIRE(bad_high); // the finite, badly fitted region is still flagged
    }
}

TEST_CASE("RelativeTail on degree-1/2 uses the sampled check", "[treeweave][tail][lowdegree]") {
    // With NCOEFFS <= 2 the tail is the whole polynomial, so RelativeTail could never pass on a
    // nonzero constant/linear: exact functions converge, an inexact one is still rejected.
    using treeweave::TolKind;
    const auto opts = options{.tol_kind = TolKind::RelativeTail};
    auto       cst  = fit<1>([](double) { return 1e-12; }, 0.0, 1.0, 1e-10, opts);
    REQUIRE(cst.num_leaves() == 1);
    auto lin = fit<2>([](double x) { return 1.0 + 2.0 * x; }, 0.0, 1.0, 1e-10, opts);
    REQUIRE(lin.num_leaves() == 1);
    CHECK(lin(0.37) == Catch::Approx(1.0 + 2.0 * 0.37).epsilon(1e-8));

    auto quad = fit<1>([](double x) { return x * x; }, 0.0, 1.0, 1e-10,
                       options{.tol_kind = TolKind::RelativeTail, .max_depth = 0, .allow_max_depth_leaves = true});
    REQUIRE(quad.non_converged_panels().size() == 1);
}

namespace {
// n_sample_1d = 8 grid on [1, 3] (center 2, half 1): sample i is
// 1.125 + 0.25 * i, so the sentinel lookup is exact in double.
constexpr int    kTestSamples = 8;
constexpr double kCenter = 2.0, kHalf = 1.0;
double           kTestGridPoint(std::size_t i) { return 1.125 + 0.25 * static_cast<double>(i); }

// Runs the checker with the reference closed over `ref` and every grid point
// approximated by `ref(x) + err_of(x)`. Returns the checker verdict.
template <class Ref, class Err>
bool sample_tol_check(treeweave::TolKind kind, double tol, Ref ref, Err err_of) {
    double        max_abs_f = 0.0;
    auto          func      = [ref](double x) { return ref(x); };
    FakeSampleFit fit{[ref, err_of](double x) { return ref(x) + err_of(x); }};
    return treeweave::detail::sample_error_exceeds_tol(kTestSamples, kind, tol, max_abs_f, kCenter, kHalf, func, fit);
}
} // namespace

TEST_CASE("the RelativeL2 check scales the norm, no overflow or underflow", "[treeweave][rell2][scaled]") {
    using treeweave::TolKind;
    // Distinct well-fitted grid values: err = 1e-3 * f, so the L2 ratio is 1e-3.
    // A long-double reference over the same grid gives the oracle ratio.
    const std::array<double, kTestSamples> base{1.0, 1.7, 2.3, 3.1, 3.9, 4.7, 5.3, 5.9};
    const auto                             ref_of = [&base](double x) {
        for (std::size_t i = 0; i < kTestSamples; ++i)
            if (x == kTestGridPoint(i))
                return base[i];
        return 0.0;
    };
    for (const double mag : {1e200, 1e-200}) {
        auto        ref     = [mag, ref_of](double x) { return mag * ref_of(x); };
        auto        err_of  = [mag, ref_of](double x) { return 1e-3 * mag * ref_of(x); };
        long double ssq_err = 0.0L, ssq_f = 0.0L;
        for (std::size_t i = 0; i < kTestSamples; ++i) {
            // mag cancels in the ratio; drop it so the oracle does not overflow
            // on platforms where long double has double's exponent range.
            const long double f = static_cast<long double>(base[i]);
            const long double e = 1e-3L * f;
            ssq_err += e * e;
            ssq_f += f * f;
        }
        const long double oracle = std::sqrt(ssq_err / ssq_f);
        INFO("magnitude " << mag << ", oracle relative L2 " << static_cast<double>(oracle));
        REQUIRE(sample_tol_check(TolKind::RelativeL2, 2e-3, ref, err_of) == (oracle > 2e-3L));
        REQUIRE(sample_tol_check(TolKind::RelativeL2, 5e-4, ref, err_of) == (oracle > 5e-4L));
    }
    // Strict check: sample magnitudes grow by 1e8 per step, so the running
    // scale rescales at every sample. Bisect tol until the verdict flips to
    // recover the computed relative L2; compare against a long double oracle
    // within a few ulps. A wrong rescale factor (ssq*r instead of ssq*r*r)
    // moves the result far outside this window.
    {
        const std::array<double, kTestSamples> grow{1e0, 1e8, 1e16, 1e24, 1e32, 1e40, 1e48, 1e56};
        const auto                             grow_of = [&grow](double x) {
            for (std::size_t i = 0; i < kTestSamples; ++i)
                if (x == kTestGridPoint(i))
                    return grow[i];
            return 0.0;
        };
        auto        err_of  = [grow_of](double x) { return 3e-3 * grow_of(x); };
        long double osq_err = 0.0L, osq_f = 0.0L;
        for (std::size_t i = 0; i < kTestSamples; ++i) {
            const double      fd = grow[i];
            const long double f  = static_cast<long double>(fd);
            // The checker sees (f + e) - f, rounded in double, not e itself.
            const long double e = static_cast<long double>(std::abs((fd + 3e-3 * fd) - fd));
            osq_err += e * e;
            osq_f += f * f;
        }
        const long double expected = std::sqrt(osq_err / osq_f);
        // Bisect: sample_tol_check is monotone in tol (true below the norm,
        // false at or above it). 200 iterations reach ulp-level precision.
        double lo = 0.0, hi = 1.0;
        for (int iter = 0; iter < 200; ++iter) {
            const double mid = 0.5 * (lo + hi);
            if (sample_tol_check(TolKind::RelativeL2, mid, grow_of, err_of))
                lo = mid;
            else
                hi = mid;
        }
        const double      got = 0.5 * (lo + hi);
        const long double ulp =
            static_cast<long double>(std::numeric_limits<double>::epsilon()) * static_cast<long double>(std::abs(got));
        INFO("bisected relative L2 " << got << ", oracle " << static_cast<double>(expected));
        REQUIRE(std::abs(static_cast<long double>(got) - expected) <= 8.0L * ulp);
    }
}

TEST_CASE("the AbsoluteL2 check scales the norm, no overflow or underflow", "[treeweave][absl2][scaled]") {
    using treeweave::TolKind;
    const std::array<double, kTestSamples> fbase{1.1, 1.9, 2.6, 3.4, 4.2, 4.9, 5.7, 6.1};
    const std::array<double, kTestSamples> ebase{1.3, 0.7, 2.1, 0.9, 1.7, 0.3, 1.1, 1.9};
    const auto                             ref_of = [&fbase](double x) {
        for (std::size_t i = 0; i < kTestSamples; ++i)
            if (x == kTestGridPoint(i))
                return fbase[i];
        return 0.0;
    };
    const auto err_base_of = [&ebase](double x) {
        for (std::size_t i = 0; i < kTestSamples; ++i)
            if (x == kTestGridPoint(i))
                return ebase[i];
        return 0.0;
    };
    for (const double mag : {1e200, 1e-200}) {
        auto        ref    = [mag, ref_of](double x) { return mag * ref_of(x); };
        auto        err_of = [mag, err_base_of](double x) { return mag * err_base_of(x); };
        long double ssq    = 0.0L;
        for (std::size_t i = 0; i < kTestSamples; ++i) {
            // Compute at mag=1 and scale afterwards: e = mag * ebase, so the
            // oracle scales linearly with mag. Doing the sum at mag directly
            // overflows on platforms where long double has double's exponent
            // range.
            const long double e = static_cast<long double>(ebase[i]);
            ssq += e * e;
        }
        const long double oracle =
            static_cast<long double>(mag) * std::sqrt(ssq) / static_cast<long double>(kTestSamples);
        INFO("magnitude " << mag << ", oracle absolute L2 " << static_cast<long double>(oracle));
        REQUIRE(sample_tol_check(TolKind::AbsoluteL2, 2.0 * static_cast<double>(oracle), ref, err_of) == false);
        REQUIRE(sample_tol_check(TolKind::AbsoluteL2, 0.5 * static_cast<double>(oracle), ref, err_of) == true);
    }
    // Strict check: error magnitudes grow by 1e8 per step, so the running
    // scale rescales at every sample. Bisect tol until the verdict flips to
    // recover the computed absolute L2; compare against a long double oracle
    // within a few ulps. A wrong rescale factor (ssq*r instead of ssq*r*r)
    // moves the result far outside this window.
    {
        const std::array<double, kTestSamples> egrow{1e0, 1e8, 1e16, 1e24, 1e32, 1e40, 1e48, 1e56};
        const auto                             err_of = [&egrow](double x) {
            for (std::size_t i = 0; i < kTestSamples; ++i)
                if (x == kTestGridPoint(i))
                    return egrow[i];
            return 0.0;
        };
        auto        ref_none = [](double) { return 1.0; };
        long double osq      = 0.0L;
        for (std::size_t i = 0; i < kTestSamples; ++i) {
            const double      ed = egrow[i];
            const long double e  = static_cast<long double>(std::abs((1.0 + ed) - 1.0));
            osq += e * e;
        }
        const long double expected = std::sqrt(osq) / static_cast<long double>(kTestSamples);
        // Bisect: sample_tol_check is monotone in tol (true below the norm,
        // false at or above it). 200 iterations reach ulp-level precision.
        const double scale = static_cast<double>(expected);
        double       lo = 0.0, hi = 4.0 * scale;
        for (int iter = 0; iter < 200; ++iter) {
            const double mid = 0.5 * (lo + hi);
            if (sample_tol_check(TolKind::AbsoluteL2, mid, ref_none, err_of))
                lo = mid;
            else
                hi = mid;
        }
        const double      got = 0.5 * (lo + hi);
        const long double ulp =
            static_cast<long double>(std::numeric_limits<double>::epsilon()) * static_cast<long double>(std::abs(got));
        INFO("bisected absolute L2 " << got << ", oracle " << static_cast<double>(expected));
        REQUIRE(std::abs(static_cast<long double>(got) - expected) <= 8.0L * ulp);
    }
}

TEST_CASE("the RelativeL2 check matches the sibling rule on an all-zero reference", "[treeweave][rell2][zeroref]") {
    using treeweave::TolKind;
    // Sibling rule (RelativeMax): an all-zero f converges iff the error is 0.
    // AbsoluteL2 is excluded: with no reference to normalise by, a 1e-13
    // error under a 1e-10 absolute tol correctly passes.
    auto zero_ref = [](double) { return 0.0; };
    auto zero_err = [](double) { return 0.0; };
    auto bad_err  = [](double x) { return x > 2.0 ? 1e-13 : -1e-13; };
    REQUIRE(!sample_tol_check(TolKind::RelativeL2, 1e-10, zero_ref, zero_err));
    REQUIRE(sample_tol_check(TolKind::RelativeL2, 1e-10, zero_ref, bad_err));
}

TEST_CASE("an isolated non-finite error is rejected at every grid position", "[treeweave][rell2][nonfinite]") {
    using treeweave::TolKind;
    constexpr double inf = std::numeric_limits<double>::infinity();
    constexpr double nan = std::numeric_limits<double>::quiet_NaN();
    // Distinct nonzero reference and error so only the injected value is bad.
    const std::array<double, kTestSamples> base{1.2, 2.1, 2.9, 3.7, 4.4, 5.2, 5.8, 6.6};
    auto                                   ref = [&base](double x) {
        for (std::size_t i = 0; i < kTestSamples; ++i)
            if (x == kTestGridPoint(i))
                return base[i];
        return 1.0;
    };
    for (const auto kind : {TolKind::RelativeL2, TolKind::AbsoluteL2, TolKind::RelativeMax, TolKind::AbsoluteMax}) {
        for (const std::size_t pos : {std::size_t{0}, std::size_t{4}, std::size_t{kTestSamples - 1}}) {
            for (const double bad : {nan, inf}) {
                // Inject through the polyfit side: approx = ref + err is bad at pos.
                auto err_of = [&base, bad, pos](double x) {
                    if (x == kTestGridPoint(pos))
                        return bad;
                    return x == x ? 1e-12 * base[1] : 0.0; // base[1] keeps values distinct
                };
                double        max_abs_f = 0.0;
                auto          func      = [ref](double x) { return ref(x); };
                FakeSampleFit fit{[ref, err_of](double x) { return ref(x) + err_of(x); }};
                INFO("kind " << static_cast<int>(kind) << ", position " << pos << ", value " << bad);
                REQUIRE(treeweave::detail::sample_error_exceeds_tol(kTestSamples, kind, 1e-8, max_abs_f, kCenter, kHalf,
                                                                    func, fit));
            }
        }
    }
}
// NOLINTEND(cert-msc51-cpp,cert-msc32-c)
