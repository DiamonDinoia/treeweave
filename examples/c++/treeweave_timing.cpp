#include <treeweave/treeweave.hpp>

#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <random>
#include <string>
#include <vector>

namespace {

using clock_t_ = std::chrono::steady_clock;

double seconds_since(clock_t_::time_point t0) { return std::chrono::duration<double>(clock_t_::now() - t0).count(); }

template <class X>
struct dim_of;
template <class T, std::size_t N>
struct dim_of<std::array<T, N>> {
    static constexpr std::size_t value = N;
};
template <>
struct dim_of<double> {
    static constexpr std::size_t value = 1;
};

template <class Fn, class X>
void time_and_check(const std::string &label, Fn &fn, const std::vector<X> &xs, auto exact) {
    constexpr std::size_t in_dim = dim_of<X>::value;

    // Flatten point array for the batch API.
    const std::size_t   n = xs.size();
    std::vector<double> flat(in_dim * n);
    for (std::size_t i = 0; i < n; ++i) {
        if constexpr (in_dim == 1)
            flat[i] = xs[i];
        else
            for (std::size_t d = 0; d < in_dim; ++d)
                flat[in_dim * i + d] = xs[i][d];
    }

    // Probe output dimensionality once.
    constexpr std::size_t out_dim = []() -> std::size_t {
        if constexpr (requires { std::declval<Fn>()(std::declval<X>())[0]; })
            return std::tuple_size_v<std::remove_cvref_t<decltype(std::declval<Fn>()(std::declval<X>()))>>;
        else
            return 1;
    }();

    std::vector<double> out(out_dim * n);
    const auto          t0 = clock_t_::now();
    fn(flat.data(), out.data(), n);
    const double dt = seconds_since(t0);

    double acc = 0.0;
    for (double v : out)
        acc += v;

    double mx_rel = 0.0, mx_abs = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double ref    = exact(xs[i]);
        const double approx = out[out_dim * i];
        const double e      = std::abs(ref - approx);
        mx_abs              = std::max(mx_abs, e);
        if (std::abs(ref) > 1e-12)
            mx_rel = std::max(mx_rel, e / std::abs(ref));
    }
    std::cout << "[" << label << "] MEvals/s = " << static_cast<double>(n) / (dt * 1e6) << "  max_rel_err = " << mx_rel
              << "  max_abs_err = " << mx_abs << "  (ignore " << acc << ")\n";
}

void run_1d(std::size_t n_points) {
    auto f  = [](double x) { return 1.0 / (1.0 + 25.0 * x * x); };
    auto fn = treeweave::fit<10>(f, -1.0, 1.0, /*tol=*/1e-10);

    std::mt19937                           gen(1);
    std::uniform_real_distribution<double> d(-0.999, 0.999);
    std::vector<double>                    xs(n_points);
    for (auto &x : xs)
        x = d(gen);

    fn.print_stats();
    time_and_check("1D Runge", fn, xs, f);
}

void run_2d(std::size_t n_points) {
    auto f = [](std::array<double, 2> x) -> std::array<double, 1> {
        return {std::exp(-100.0 * (x[0] - 0.5) * (x[0] - 0.5) - (x[1] - 0.5) * (x[1] - 0.5))};
    };
    auto exact = [](std::array<double, 2> x) {
        return std::exp(-100.0 * (x[0] - 0.5) * (x[0] - 0.5) - (x[1] - 0.5) * (x[1] - 0.5));
    };
    auto fn = treeweave::fit<10>(f, std::array{0.0, 0.0}, std::array{1.0, 1.0}, /*tol=*/1e-10);

    std::mt19937                           gen(1);
    std::uniform_real_distribution<double> d(0.001, 0.999);
    std::vector<std::array<double, 2>>     xs(n_points);
    for (auto &x : xs)
        x = {d(gen), d(gen)};

    fn.print_stats();
    time_and_check("2D anisotropic bump", fn, xs, exact);
}

void run_3d(std::size_t n_points) {
    auto f = [](std::array<double, 3> x) -> std::array<double, 1> {
        return {std::exp(-x[0] * x[0] - x[1] * x[1] - x[2] * x[2])};
    };
    auto exact = [](std::array<double, 3> x) { return std::exp(-x[0] * x[0] - x[1] * x[1] - x[2] * x[2]); };
    auto fn    = treeweave::fit<8>(f, std::array{-1.0, -1.0, -1.0}, std::array{1.0, 1.0, 1.0}, /*tol=*/1e-10);

    std::mt19937                           gen(1);
    std::uniform_real_distribution<double> d(-0.999, 0.999);
    std::vector<std::array<double, 3>>     xs(n_points);
    for (auto &x : xs)
        x = {d(gen), d(gen), d(gen)};

    fn.print_stats();
    time_and_check("3D gaussian", fn, xs, exact);
}

void run_yukawa_3d(std::size_t n_points) {
    // Yukawa e^{-r}/r on a shifted box (off the singularity) — a realistic
    // ND kernel workload surfacing the per-eval cost of leaf polyfits.
    auto f = [](std::array<double, 3> x) -> std::array<double, 1> {
        const double r = std::sqrt(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]);
        return {std::exp(-r) / r};
    };
    auto exact = [](std::array<double, 3> x) {
        const double r = std::sqrt(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]);
        return std::exp(-r) / r;
    };
    auto fn = treeweave::fit<8>(f, std::array{0.2, 0.2, 0.2}, std::array{1.5, 1.5, 1.5}, /*tol=*/1e-8);

    std::mt19937                           gen(1);
    std::uniform_real_distribution<double> d(0.21, 1.49);
    std::vector<std::array<double, 3>>     xs(n_points);
    for (auto &x : xs)
        x = {d(gen), d(gen), d(gen)};

    fn.print_stats();
    time_and_check("3D Yukawa e^{-r}/r", fn, xs, exact);
}

} // namespace

int main(int argc, char *argv[]) {
    std::size_t      n_points = 1'000'000;
    std::vector<int> dims{1, 2, 3, 4};
    if (argc >= 2)
        n_points = static_cast<std::size_t>(std::atoll(argv[1]));
    if (argc > 2) {
        dims.clear();
        for (int i = 2; i < argc; ++i)
            dims.push_back(std::atoi(argv[i]));
    }

    for (int d : dims) {
        switch (d) {
        case 1:
            run_1d(n_points);
            break;
        case 2:
            run_2d(n_points);
            break;
        case 3:
            run_3d(n_points);
            break;
        case 4:
            run_yukawa_3d(n_points);
            break;
        default:
            std::cerr << "Only 1, 2, 3, 4(=yukawa3d) supported; got " << d << "\n";
            return 1;
        }
    }
    return 0;
}
