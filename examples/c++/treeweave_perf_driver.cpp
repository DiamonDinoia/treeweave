// Focused perf driver: 1D gauss/runge, 2D bump, 3D gauss — N=1e6 each.
// Tight outer loop so perf samples concentrate on operator() batch eval.
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <string_view>
#include <treeweave/treeweave.hpp>
#include <vector>

namespace {
auto make_gauss1d() {
    return [](double x) -> double { return std::exp(-x * x); };
}
auto make_runge1d() {
    return [](double x) -> double { return 1.0 / (1.0 + 25.0 * x * x); };
}
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

template <std::size_t Dim, class Fn>
void hammer(const char *label, Fn &&fn, std::array<double, Dim> a, std::array<double, Dim> b, double seconds) {
    constexpr std::size_t                  N = 1'000'000;
    std::mt19937                           gen(7);
    std::uniform_real_distribution<double> ud(0.0, 1.0);
    std::vector<double>                    flat(Dim * N);
    for (std::size_t i = 0; i < N; ++i)
        for (std::size_t d = 0; d < Dim; ++d)
            flat[Dim * i + d] = a[d] + (b[d] - a[d] - 1e-3) * ud(gen) + 5e-4;
    std::vector<double> out(N);

    auto        t0    = std::chrono::steady_clock::now();
    std::size_t iters = 0;
    double      sink  = 0.0;
    while (true) {
        fn(flat.data(), out.data(), N);
        sink += out[(iters * 7919u) % N];
        ++iters;
        auto dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        if (dt >= seconds) {
            std::printf("# %s: %zu iters in %.2fs (%.2f Mevals/s), sink=%g\n", label, iters, dt,
                        double(iters) * double(N) / dt / 1e6, sink);
            break;
        }
    }
}
} // namespace

int main(int argc, char **argv) {
    double secs = (argc > 1) ? std::atof(argv[1]) : 15.0;
    // Optional 2nd argv: comma-separated dim list, e.g. "1d", "1d,2d",
    // "all" (default). Skipped scenarios are silently omitted;
    // parse_paired.py treats missing scenarios as no-data per scenario
    // (it only drops a run when an explicit "SKIPPED" line is printed,
    // which we avoid).
    const std::string filter   = (argc > 2) ? argv[2] : "all";
    auto              contains = [&](std::string_view tag) {
        if (filter == "all")
            return true;
        std::string needle{tag};
        // Match as a comma-bounded token: ",1d," in ",<filter>,"
        std::string padded = "," + filter + ",";
        return padded.find("," + needle + ",") != std::string::npos;
    };
    const bool run_1d = contains("1d");
    const bool run_2d = contains("2d");
    const bool run_3d = contains("3d");

    if (run_1d) {
        auto fn = treeweave::fit<8>(make_gauss1d(), -3.0, 3.0, 1e-10);
        std::printf("== 1d_gauss ==\n");
        fn.print_stats();
        hammer<1>("1d_gauss deg=8 N=1e6", fn, {-3.0}, {3.0}, secs);
    }
    if (run_1d) {
        auto fn = treeweave::fit<8>(make_runge1d(), -1.0, 1.0, 1e-10);
        std::printf("== 1d_runge ==\n");
        fn.print_stats();
        hammer<1>("1d_runge deg=8 N=1e6", fn, {-1.0}, {1.0}, secs);
    }
    if (run_2d) {
        auto fn =
            treeweave::fit<8>(make_bump2d(), std::array<double, 2>{0.0, 0.0}, std::array<double, 2>{1.0, 1.0}, 1e-10);
        std::printf("== 2d_bump ==\n");
        fn.print_stats();
        hammer<2>("2d_bump deg=8 N=1e6", fn, {0.0, 0.0}, {1.0, 1.0}, secs);
    }
    if (run_3d) {
        auto fn = treeweave::fit<8>(make_gauss3d(), std::array<double, 3>{-1.0, -1.0, -1.0},
                                    std::array<double, 3>{1.0, 1.0, 1.0}, 1e-10);
        std::printf("== 3d_gauss ==\n");
        fn.print_stats();
        hammer<3>("3d_gauss deg=8 N=1e6", fn, {-1.0, -1.0, -1.0}, {1.0, 1.0, 1.0}, secs);
    }
    return 0;
}
