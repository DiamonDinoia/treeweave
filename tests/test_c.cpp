/// test_c.cpp — exercises the extern "C" surface (treeweave.h) and checks it
/// against a direct C++ `treeweave::fit` of the same kernel. The C API builds
/// the same Function internally, so parity must be exact up to floating
/// noise; the comparison validates the dispatch / buffer / dtype plumbing,
/// not the approximation math (that lives in test_cpp.cpp).

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <treeweave.h>

#include <treeweave/treeweave.hpp>

using Catch::Approx;

// Deterministic seeds: reproducible parity sweeps.
// NOLINTBEGIN(cert-msc51-cpp,cert-msc32-c)

namespace {

// --- Kernels, shared between the C callbacks and the C++ reference fits ---
// Smooth and comfortably nonzero on the unit-ish boxes used below.

template <class T>
auto k_1d_1(T x) -> T {
    return std::exp(T(0.5) * x) + std::sin(T(3) * x);
}

template <class T>
auto k_1d_2(std::array<T, 1> x) -> std::array<T, 2> {
    return {std::exp(T(0.5) * x[0]), std::sin(T(3) * x[0]) + T(2)};
}

template <class T>
auto k_2d_1(std::array<T, 2> x) -> std::array<T, 1> {
    return {std::exp(T(0.3) * x[0]) + std::sin(T(2) * x[1])};
}

template <class T>
auto k_2d_3(std::array<T, 2> x) -> std::array<T, 3> {
    return {std::exp(T(0.3) * x[0]), std::sin(T(2) * x[1]) + T(2), std::cos(x[0] * x[1]) + T(2)};
}

template <class T>
auto k_3d_1(std::array<T, 3> x) -> std::array<T, 1> {
    return {std::exp(T(0.2) * x[0]) + std::sin(x[1]) + std::cos(x[2])};
}

// --- C callbacks (extern "C" so the function-pointer language linkage
//     matches treeweave_func_t / treeweavef_func_t exactly under -Werror). ---
extern "C" {
void c_1d_1(const double *x, double *y, void *) { y[0] = k_1d_1<double>(x[0]); }
void c_1d_2(const double *x, double *y, void *) {
    auto r = k_1d_2<double>({x[0]});
    y[0]   = r[0];
    y[1]   = r[1];
}
void c_2d_1(const double *x, double *y, void *) { y[0] = k_2d_1<double>({x[0], x[1]})[0]; }
void c_2d_3(const double *x, double *y, void *) {
    auto r = k_2d_3<double>({x[0], x[1]});
    y[0]   = r[0];
    y[1]   = r[1];
    y[2]   = r[2];
}
void c_3d_1(const double *x, double *y, void *) { y[0] = k_3d_1<double>({x[0], x[1], x[2]})[0]; }
void c_1d_1f(const float *x, float *y, void *) { y[0] = k_1d_1<float>(x[0]); }
void c_2d_3f(const float *x, float *y, void *) {
    auto r = k_2d_3<float>({x[0], x[1]});
    y[0]   = r[0];
    y[1]   = r[1];
    y[2]   = r[2];
}
} // extern "C"

constexpr std::size_t kNPts = 2000;

} // namespace

TEST_CASE("C API: 1D scalar parity + multi + sorted + OOD", "[c][1d]") {
    const double a = 0.0, b = 1.0;
    auto         ref = treeweave::fit<7>([](double x) { return k_1d_1<double>(x); }, a, b, 1e-10);
    treeweave_t  h   = treeweave_fit(c_1d_1, 1, 1, &a, &b, 1e-10, nullptr, nullptr);
    REQUIRE(h != nullptr);
    REQUIRE(treeweave_dtype(h) == TREEWEAVE_F64);
    REQUIRE(treeweave_input_dim(h) == 1);
    REQUIRE(treeweave_output_dim(h) == 1);

    std::mt19937                           gen(1);
    std::uniform_real_distribution<double> d(a, b);
    std::vector<double>                    xs(kNPts);
    for (auto &x : xs)
        x = d(gen);

    SECTION("scalar eval matches C++ reference") {
        for (double const x : xs) {
            double y = 0;
            treeweave_eval(h, &x, &y);
            REQUIRE(y == Approx(ref(x)).epsilon(1e-12));
        }
    }
    SECTION("multi eval matches scalar") {
        std::vector<double> ys(kNPts, 0.0);
        treeweave_batch(h, xs.data(), ys.data(), kNPts);
        for (std::size_t i = 0; i < kNPts; ++i)
            REQUIRE(ys[i] == Approx(ref(xs[i])).epsilon(1e-12));
    }
    SECTION("sorted eval matches multi on sorted input") {
        std::vector<double> sx = xs;
        std::sort(sx.begin(), sx.end());
        std::vector<double> ym(kNPts, 0.0), ysort(kNPts, 0.0);
        treeweave_batch(h, sx.data(), ym.data(), kNPts);
        treeweave_sorted(h, sx.data(), ysort.data(), kNPts);
        for (std::size_t i = 0; i < kNPts; ++i)
            REQUIRE(ysort[i] == Approx(ym[i]).epsilon(1e-14));
    }
    SECTION("domain edges: closed upper endpoint, open lower bound") {
        // x < a -> NaN (lower side stays out-of-domain).
        double xlo = a - 1.0, ylo = 0.0;
        treeweave_eval(h, &xlo, &ylo);
        REQUIRE(std::isnan(ylo));
        // x == b -> boundary value (closed upper endpoint), matching the C++
        // reference's operator()(b).
        double xb = b, yb = 0.0;
        treeweave_eval(h, &xb, &yb);
        REQUIRE_FALSE(std::isnan(yb));
        REQUIRE(yb == Approx(ref(b)).epsilon(1e-6));
        // x > b through the scalar eval keeps NaN (operator()'s inclusive guard
        // admits only the exact endpoint, not the extrapolation region).
        double xhi = b + 1.0, yhi = 0.0;
        treeweave_eval(h, &xhi, &yhi);
        REQUIRE(std::isnan(yhi));
    }
    h = treeweave_free(h);
    REQUIRE(h == nullptr);
}

TEST_CASE("C API: 1D vector output (SoA == AoS) + sorted", "[c][1d][vector]") {
    const double a = 0.0, b = 1.0;
    auto ref = treeweave::fit<7>([](std::array<double, 1> x) { return k_1d_2<double>(x); }, std::array<double, 1>{a},
                                 std::array<double, 1>{b}, 1e-9);
    treeweave_t h = treeweave_fit(c_1d_2, 1, 2, &a, &b, 1e-9, nullptr, nullptr);
    REQUIRE(h != nullptr);
    REQUIRE(treeweave_output_dim(h) == 2);

    std::mt19937                           gen(2);
    std::uniform_real_distribution<double> d(a, b);
    std::vector<double>                    xs(kNPts);
    for (auto &x : xs)
        x = d(gen);

    std::vector<double> aos(kNPts * 2, 0.0);
    treeweave_batch(h, xs.data(), aos.data(), kNPts);
    for (std::size_t i = 0; i < kNPts; ++i) {
        auto r = ref(std::array<double, 1>{xs[i]});
        REQUIRE(aos[2 * i + 0] == Approx(r[0]).epsilon(1e-12));
        REQUIRE(aos[2 * i + 1] == Approx(r[1]).epsilon(1e-12));
    }

    SECTION("SoA matches AoS") {
        std::vector<double> c0(kNPts, 0.0), c1(kNPts, 0.0);
        double             *soa[2] = {c0.data(), c1.data()};
        treeweave_transposed(h, xs.data(), soa, kNPts);
        for (std::size_t i = 0; i < kNPts; ++i) {
            REQUIRE(c0[i] == Approx(aos[2 * i + 0]).epsilon(1e-14));
            REQUIRE(c1[i] == Approx(aos[2 * i + 1]).epsilon(1e-14));
        }
    }
    SECTION("sorted matches multi (vector output)") {
        std::vector<double> sx = xs;
        std::sort(sx.begin(), sx.end());
        std::vector<double> ym(kNPts * 2, 0.0), ysort(kNPts * 2, 0.0);
        treeweave_batch(h, sx.data(), ym.data(), kNPts);
        treeweave_sorted(h, sx.data(), ysort.data(), kNPts);
        for (std::size_t i = 0; i < kNPts * 2; ++i)
            REQUIRE(ysort[i] == Approx(ym[i]).epsilon(1e-14));
    }
    treeweave_free(h);
}

TEST_CASE("C API: 2D scalar + 2D vector(SoA) + 3D scalar", "[c][2d][3d]") {
    SECTION("2D -> 1D parity") {
        const double a[2] = {0.0, 0.0}, b[2] = {1.0, 1.0};
        auto        ref = treeweave::fit<7>([](std::array<double, 2> x) { return k_2d_1<double>(x); },
                                            std::array<double, 2>{a[0], a[1]}, std::array<double, 2>{b[0], b[1]}, 1e-8);
        treeweave_t h   = treeweave_fit(c_2d_1, 2, 1, a, b, 1e-8, nullptr, nullptr);
        REQUIRE(h != nullptr);

        std::mt19937                           gen(3);
        std::uniform_real_distribution<double> d(0.0, 1.0);
        std::vector<double>                    xs(kNPts * 2);
        for (auto &x : xs)
            x = d(gen);
        std::vector<double> ys(kNPts, 0.0);
        treeweave_batch(h, xs.data(), ys.data(), kNPts);
        for (std::size_t i = 0; i < kNPts; ++i) {
            auto r = ref(std::array<double, 2>{xs[2 * i], xs[2 * i + 1]});
            REQUIRE(ys[i] == Approx(r[0]).epsilon(1e-11));
        }
        // sorted is 1D-only: must error and not write.
        double y = 123.0;
        treeweave_sorted(h, xs.data(), &y, 1);
        REQUIRE(std::string(treeweave_last_error()).find("input_dim") != std::string::npos);
        treeweave_free(h);
    }
    SECTION("2D -> 3D SoA parity") {
        const double a[2] = {0.0, 0.0}, b[2] = {1.0, 1.0};
        auto        ref = treeweave::fit<7>([](std::array<double, 2> x) { return k_2d_3<double>(x); },
                                            std::array<double, 2>{a[0], a[1]}, std::array<double, 2>{b[0], b[1]}, 1e-8);
        treeweave_t h   = treeweave_fit(c_2d_3, 2, 3, a, b, 1e-8, nullptr, nullptr);
        REQUIRE(h != nullptr);

        std::mt19937                           gen(4);
        std::uniform_real_distribution<double> d(0.0, 1.0);
        std::vector<double>                    xs(kNPts * 2);
        for (auto &x : xs)
            x = d(gen);
        std::vector<double> aos(kNPts * 3, 0.0);
        treeweave_batch(h, xs.data(), aos.data(), kNPts);

        std::vector<double> c0(kNPts), c1(kNPts), c2(kNPts);
        double             *soa[3] = {c0.data(), c1.data(), c2.data()};
        treeweave_transposed(h, xs.data(), soa, kNPts);
        for (std::size_t i = 0; i < kNPts; ++i) {
            auto r = ref(std::array<double, 2>{xs[2 * i], xs[2 * i + 1]});
            REQUIRE(aos[3 * i + 0] == Approx(r[0]).epsilon(1e-11));
            REQUIRE(c0[i] == Approx(aos[3 * i + 0]).epsilon(1e-14));
            REQUIRE(c1[i] == Approx(aos[3 * i + 1]).epsilon(1e-14));
            REQUIRE(c2[i] == Approx(aos[3 * i + 2]).epsilon(1e-14));
        }
        treeweave_free(h);
    }
    SECTION("3D -> 1D parity") {
        const double a[3] = {0.0, 0.0, 0.0}, b[3] = {1.0, 1.0, 1.0};
        auto         ref =
            treeweave::fit<7>([](std::array<double, 3> x) { return k_3d_1<double>(x); },
                              std::array<double, 3>{a[0], a[1], a[2]}, std::array<double, 3>{b[0], b[1], b[2]}, 1e-7);
        treeweave_t h = treeweave_fit(c_3d_1, 3, 1, a, b, 1e-7, nullptr, nullptr);
        REQUIRE(h != nullptr);

        std::mt19937                           gen(5);
        std::uniform_real_distribution<double> d(0.0, 1.0);
        std::vector<double>                    xs(kNPts * 3);
        for (auto &x : xs)
            x = d(gen);
        std::vector<double> ys(kNPts, 0.0);
        treeweave_batch(h, xs.data(), ys.data(), kNPts);
        for (std::size_t i = 0; i < kNPts; ++i) {
            auto r = ref(std::array<double, 3>{xs[3 * i], xs[3 * i + 1], xs[3 * i + 2]});
            REQUIRE(ys[i] == Approx(r[0]).epsilon(1e-10));
        }
        treeweave_free(h);
    }
}

TEST_CASE("C API: float (f32) parity", "[c][f32]") {
    SECTION("1D scalar") {
        const float a = 0.0F, b = 1.0F;
        auto        ref = treeweave::fit<7>([](float x) { return k_1d_1<float>(x); }, a, b, 1e-5);
        treeweave_t h   = treeweavef_fit(c_1d_1f, 1, 1, &a, &b, 1e-5, nullptr, nullptr);
        REQUIRE(h != nullptr);
        REQUIRE(treeweave_dtype(h) == TREEWEAVE_F32);

        std::mt19937                          gen(6);
        std::uniform_real_distribution<float> d(a, b);
        std::vector<float>                    xs(kNPts);
        for (auto &x : xs)
            x = d(gen);
        std::vector<float> ys(kNPts, 0.0F);
        treeweavef_batch(h, xs.data(), ys.data(), kNPts);
        for (std::size_t i = 0; i < kNPts; ++i)
            REQUIRE(ys[i] == Approx(ref(xs[i])).epsilon(1e-5));
        treeweave_free(h);
    }
    SECTION("2D -> 3D SoA") {
        const float a[2] = {0.0F, 0.0F}, b[2] = {1.0F, 1.0F};
        treeweave_t h = treeweavef_fit(c_2d_3f, 2, 3, a, b, 1e-5, nullptr, nullptr);
        REQUIRE(h != nullptr);
        std::mt19937                          gen(7);
        std::uniform_real_distribution<float> d(0.0F, 1.0F);
        std::vector<float>                    xs(kNPts * 2);
        for (auto &x : xs)
            x = d(gen);
        std::vector<float> aos(kNPts * 3, 0.0F);
        treeweavef_batch(h, xs.data(), aos.data(), kNPts);
        std::vector<float> c0(kNPts), c1(kNPts), c2(kNPts);
        float             *soa[3] = {c0.data(), c1.data(), c2.data()};
        treeweavef_transposed(h, xs.data(), soa, kNPts);
        for (std::size_t i = 0; i < kNPts; ++i) {
            REQUIRE(c0[i] == Approx(aos[3 * i + 0]).epsilon(1e-6));
            REQUIRE(c1[i] == Approx(aos[3 * i + 1]).epsilon(1e-6));
            REQUIRE(c2[i] == Approx(aos[3 * i + 2]).epsilon(1e-6));
        }
        treeweave_free(h);
    }
}

TEST_CASE("C API: error handling", "[c][errors]") {
    const double a = 0.0, b = 1.0;

    SECTION("dtype mismatch errors cleanly without writing") {
        treeweave_t h = treeweave_fit(c_1d_1, 1, 1, &a, &b, 1e-10, nullptr, nullptr);
        REQUIRE(h != nullptr);
        float x = 0.5F, y = 42.0F;
        treeweavef_eval(h, &x, &y); // wrong dtype
        REQUIRE(y == 42.0F);        // untouched
        REQUIRE(std::strlen(treeweave_last_error()) > 0);
        treeweave_free(h);
    }
    SECTION("unsupported input_dim returns NULL + error") {
        const double a4[4] = {0, 0, 0, 0}, b4[4] = {1, 1, 1, 1};
        treeweave_t  h = treeweave_fit(c_1d_1, 4, 1, a4, b4, 1e-10, nullptr, nullptr);
        REQUIRE(h == nullptr);
        REQUIRE(std::string(treeweave_last_error()).find("input_dim") != std::string::npos);
    }
    SECTION("non-positive tol returns NULL + error") {
        treeweave_t h = treeweave_fit(c_1d_1, 1, 1, &a, &b, 0.0, nullptr, nullptr);
        REQUIRE(h == nullptr);
        REQUIRE(std::strlen(treeweave_last_error()) > 0);
    }
    SECTION("default options struct is sane") {
        REQUIRE(treeweave_default_opts.max_depth == 50);
        REQUIRE(treeweave_default_opts.tol_kind == TREEWEAVE_RELATIVE_MAX);
    }
    SECTION("free(NULL) and eval(NULL) are safe") {
        REQUIRE(treeweave_free(nullptr) == nullptr);
        double x = 0.0, y = 0.0;
        treeweave_eval(nullptr, &x, &y); // must not crash
        REQUIRE(std::strlen(treeweave_last_error()) > 0);
    }
}

TEST_CASE("C API: treeweave_last_error is thread-local", "[c][errors][thread]") {
    // Error state is thread_local; relocated from pure-C test (Apple SDK omits C11 <threads.h>) (see
    // devel/agents/build-notes.md).
    std::atomic<bool> err_ok{false};
    std::atomic<bool> clean_ok{false};

    std::thread err_thread([&] {
        const double a4[4] = {0.0, 0.0, 0.0, 0.0}, b4[4] = {1.0, 1.0, 1.0, 1.0};
        treeweave_t  h = treeweave_fit(c_1d_1, 4, 1, a4, b4, 1e-9, nullptr, nullptr);
        err_ok         = (h == nullptr) && (std::strlen(treeweave_last_error()) > 0);
        treeweave_free(h);
    });
    std::thread clean_thread([&] {
        const double a = 0.0, b = 1.0;
        treeweave_t  h = treeweave_fit(c_1d_1, 1, 1, &a, &b, 1e-9, nullptr, nullptr);
        clean_ok       = (h != nullptr) && (std::strlen(treeweave_last_error()) == 0);
        treeweave_free(h);
    });
    err_thread.join();
    clean_thread.join();

    REQUIRE(err_ok);
    REQUIRE(clean_ok);
}

// NOLINTEND(cert-msc51-cpp,cert-msc32-c)
