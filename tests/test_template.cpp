#include <cstddef>
#include <treeweave/treeweave.hpp>

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>

TEST_CASE("1D1 evaluations", "[treeweave_template]") {
    const double scale_factor = 1.5;
    auto         testfun_1d1  = [scale_factor](const double x) { return scale_factor * std::log(x); };

    const double     half_l = 1.0;
    const double     center = 3.0;
    const double     a      = center - half_l;
    const double     b      = center + half_l;
    constexpr double tol    = 1e-10;

    auto treeweavefunc = treeweave::fit<8>(testfun_1d1, a, b, tol);

    SECTION("evaluations at lower left") {
        const double x       = a;
        const double y_appx  = treeweavefunc(x);
        const double y_exact = testfun_1d1(x);
        REQUIRE(std::fabs((y_appx - y_exact) / y_exact) < tol);
    }

    SECTION("evaluations at center") {
        const double y_appx  = treeweavefunc(center);
        const double y_exact = testfun_1d1(center);
        REQUIRE(std::fabs((y_appx - y_exact) / y_exact) < tol);
    }
}

// Vector-valued 1D fit: array<double, 1> input + array<double, K> output.
// Regression test for the leaf-table / Value-cast / tail-error gates that
// previously rejected this shape outright. The scalar-input + array-output
// spelling stays unsupported (polyfit's compensated_horner doesn't operate
// on array OutputType); users must spell the input as array<double, 1>.
TEST_CASE("1D array->array evaluations", "[treeweave_template]") {
    auto f = [](std::array<double, 1> x) -> std::array<double, 4> {
        const double v = x[0];
        return {std::sin(v), std::cos(v), std::exp(-v * v), 1.0 / (1.0 + v * v)};
    };

    constexpr double            tol = 1e-10;
    const std::array<double, 1> a{-2.5};
    const std::array<double, 1> b{2.5};
    auto                        fn = treeweave::fit<8>(f, a, b, tol);

    SECTION("evaluation at endpoint matches all channels") {
        const auto y_appx  = fn(a);
        const auto y_exact = f(a);
        for (std::size_t k = 0; k < 4; ++k)
            REQUIRE(std::fabs(y_appx[k] - y_exact[k]) < 1e3 * tol);
    }

    SECTION("evaluation at interior point matches all channels") {
        const std::array<double, 1> x{0.37};
        const auto                  y_appx  = fn(x);
        const auto                  y_exact = f(x);
        for (std::size_t k = 0; k < 4; ++k)
            REQUIRE(std::fabs(y_appx[k] - y_exact[k]) < 1e3 * tol);
    }

    // Batch pointer overloads on a 1D vector-output Function. Previously
    // the sorted overload mixed value_type and input_type, and the
    // FuncEvalND batch entry rejected raw `double*`. The overloads now
    // reinterpret the layout-equivalent buffers at the polyfit boundary.
    SECTION("unsorted and sorted batch overloads match per-point eval") {
        constexpr std::size_t N = 256;
        std::vector<double>   xs(N);
        const double          lo = a[0];
        const double          hi = b[0];
        for (std::size_t i = 0; i < N; ++i)
            xs[i] = lo + (hi - lo) * (static_cast<double>(i) + 0.5) / static_cast<double>(N);

        std::vector<double> ref(4 * N);
        for (std::size_t i = 0; i < N; ++i) {
            const auto y = fn(std::array<double, 1>{xs[i]});
            for (std::size_t k = 0; k < 4; ++k)
                ref[i * 4 + k] = y[k];
        }

        std::vector<double> out_unsorted(4 * N, 0.0);
        fn(xs.data(), out_unsorted.data(), N);
        for (std::size_t i = 0; i < 4 * N; ++i)
            REQUIRE(out_unsorted[i] == ref[i]);

        std::vector<double> xs_sorted = xs;
        std::sort(xs_sorted.begin(), xs_sorted.end());
        std::vector<double> ref_sorted(4 * N);
        for (std::size_t i = 0; i < N; ++i) {
            const auto y = fn(std::array<double, 1>{xs_sorted[i]});
            for (std::size_t k = 0; k < 4; ++k)
                ref_sorted[i * 4 + k] = y[k];
        }
        std::vector<double> out_sorted(4 * N, 0.0);
        fn.sorted(xs_sorted.data(), out_sorted.data(), N);
        for (std::size_t i = 0; i < 4 * N; ++i)
            REQUIRE(out_sorted[i] == ref_sorted[i]);
    }
}
