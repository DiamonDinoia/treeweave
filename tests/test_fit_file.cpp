// options::filename: the first fit writes a plain-text fit file, the next fit
// with the same arguments loads it without calling f. The file rounds the
// coefficients to the digits that tol needs, so every check compares the
// loaded Function with the fitted one point by point against that bound.
#include <treeweave/treeweave.hpp>

#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

#include <catch2/catch_test_macros.hpp>

namespace fs = std::filesystem;

namespace {

auto temp_file(const std::string &name) -> std::string {
    const fs::path p = fs::temp_directory_path() / ("treeweave_test_fit_file_" + name + ".txt");
    fs::remove(p);
    return p.string();
}

auto read_text(const std::string &path) -> std::string {
    std::ifstream      in(path, std::ios::binary);
    std::ostringstream buf;
    buf << in.rdbuf();
    return std::move(buf).str();
}

auto write_text(const std::string &path, const std::string &text) -> void {
    std::ofstream(path, std::ios::binary | std::ios::trunc) << text;
}

// Bitwise equality, so -0.0 vs 0.0 and NaN payloads also count.
template <class T>
auto same_bits(T a, T b) -> bool {
    return std::memcmp(&a, &b, sizeof(T)) == 0;
}

// Fit once with a file, refit with the same arguments, and require: the
// second fit makes no call to f, and both Functions differ by at most
// `bound` in every output component at `n` points spread over [lo, hi]
// (scalar) or a diagonal (array domains). bound 0 means bit for bit.
// Return the largest difference.
template <class Domain, class F>
auto check_round_trip(const std::string &name, F f, Domain lo, Domain hi, double tol, double bound,
                      treeweave::options opts = {}) -> double {
    const std::string path = temp_file(name);
    opts.filename          = path;
    int  calls             = 0;
    auto counted           = [&](const Domain &x) {
        ++calls;
        return f(x);
    };
    const auto fitted = fit(counted, lo, hi, tol, opts);
    REQUIRE(calls > 0);
    REQUIRE(fs::exists(path));
    REQUIRE_FALSE(fs::exists(path + ".tmp"));
    const std::string first = read_text(*opts.filename);

    calls             = 0;
    const auto loaded = fit(counted, lo, hi, tol, opts);
    CHECK(calls == 0);
    // A load does not rewrite the file.
    CHECK(read_text(*opts.filename) == first);

    constexpr int n          = 257;
    int           mismatches = 0;
    double        max_diff   = 0.0;
    const auto    cmp        = [&](auto a, auto b) {
        const double d = std::abs(static_cast<double>(a) - static_cast<double>(b));
        max_diff       = std::max(max_diff, d);
        mismatches += (bound == 0.0 ? same_bits(a, b) : d <= bound) ? 0 : 1;
    };
    for (int i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / (n - 1);
        Domain       x{};
        if constexpr (std::is_arithmetic_v<Domain>) {
            x = static_cast<Domain>(static_cast<double>(lo) + t * static_cast<double>(hi - lo));
        } else {
            // A diagonal plus a rotated diagonal, so every axis varies.
            for (std::size_t d = 0; d < x.size(); ++d) {
                const double td = d % 2 == 0 ? t : 1.0 - t;
                x[d]            = static_cast<typename Domain::value_type>(static_cast<double>(lo[d]) +
                                                                           td * static_cast<double>(hi[d] - lo[d]));
            }
        }
        const auto ya = fitted(x);
        const auto yb = loaded(x);
        if constexpr (std::is_arithmetic_v<decltype(ya)>) {
            cmp(ya, yb);
        } else if constexpr (requires { ya.real(); }) {
            cmp(ya.real(), yb.real());
            cmp(ya.imag(), yb.imag());
        } else {
            for (std::size_t k = 0; k < ya.size(); ++k)
                cmp(ya[k], yb[k]);
        }
    }
    CHECK(mismatches == 0);
    fs::remove(*opts.filename);
    return max_diff;
}

// AbsoluteMax: the scale is 1, so the rounding bound is tol / 10.
const treeweave::options abs_max{.tol_kind = treeweave::TolKind::AbsoluteMax};

} // namespace

TEST_CASE("fit file: a load stays within tol / 10 and calls f zero times", "[treeweave][fit_file]") {
    using treeweave::TolKind;
    double max_diff = 0.0; // the largest difference over all sections
    SECTION("1D double") {
        max_diff = check_round_trip<double>(
            "1d_f64", [](double x) { return std::sin(5 * x) * std::exp(x); }, -1.0, 2.0, 1e-12, 1e-13, abs_max);
    }
    SECTION("1D float") {
        max_diff = check_round_trip<float>(
            "1d_f32", [](float x) { return std::cos(3 * x); }, 0.0F, 3.0F, 1e-5, 1e-6, abs_max);
    }
    SECTION("1D complex output") {
        max_diff = check_round_trip<double>(
            "1d_c", [](double x) { return std::complex<double>(std::cos(x), std::sin(2 * x)); }, 0.0, 4.0, 1e-10,
            1e-11, abs_max);
    }
    SECTION("2D -> 1") {
        using A  = std::array<double, 2>;
        max_diff = check_round_trip<A>(
            "2d_1", [](const A &x) { return std::exp(x[0]) * std::cos(2 * x[1]); }, A{-1, 0}, A{1, 3}, 1e-9, 1e-10,
            abs_max);
    }
    SECTION("2D float -> 2") {
        using A  = std::array<float, 2>;
        max_diff = check_round_trip<A>(
            "2d_2_f32", [](const A &x) { return std::array<float, 2>{std::sin(x[0] + x[1]), x[0] * x[1]}; }, A{0, 0},
            A{2, 1}, 1e-5, 1e-6, abs_max);
    }
    SECTION("3D -> 3") {
        using A  = std::array<double, 3>;
        max_diff = check_round_trip<A>(
            "3d_3", [](const A &x) { return A{std::sin(x[0]), std::cos(x[1] * x[2]), x[0] + x[1] + x[2]}; }, A{0, 0, 0},
            A{1, 1, 2}, 1e-7, 1e-8, treeweave::options{.tol_kind = TolKind::AbsoluteMax, .max_memory_mib = 0});
    }
    SECTION("leaf table path (min_uniform_depth)") {
        max_diff = check_round_trip<double>(
            "1d_uniform", [](double x) { return std::exp(x); }, 0.0, 1.0, 1e-10, 1e-11,
            treeweave::options{.tol_kind = TolKind::AbsoluteMax, .min_uniform_depth = 4});
    }
    SECTION("RelativeMax scales the bound by max|f|") {
        // max|exp| on [0, 1] is e, and the fit samples at most that.
        max_diff = check_round_trip<double>(
            "1d_relmax", [](double x) { return std::exp(x); }, 0.0, 1.0, 1e-3, 1e-3 * std::exp(1.0) / 10);
    }
    // Positive control: a loose tol must round, else the bound proves nothing.
    SECTION("tol 1e-3 rounds") {
        max_diff = check_round_trip<double>(
            "1d_loose", [](double x) { return std::sin(5 * x); }, 0.0, 2.0, 1e-3, 1e-4, abs_max);
        CHECK(max_diff > 0.0);
    }
    // Kinds with no pointwise bound keep every digit: bit for bit.
    SECTION("RelativePointwise is bit-exact") {
        max_diff = check_round_trip<double>(
            "1d_pointwise", [](double x) { return std::exp(x); }, 0.0, 1.0, 1e-8, 0.0,
            treeweave::options{.tol_kind = TolKind::RelativePointwise});
        CHECK(max_diff == 0.0);
    }
    SECTION("RelativeL2 is bit-exact") {
        using A  = std::array<double, 2>;
        max_diff = check_round_trip<A>(
            "2d_l2", [](const A &x) { return std::exp(x[0] * x[1]); }, A{0, 0}, A{1, 1}, 1e-8, 0.0,
            treeweave::options{.tol_kind = TolKind::RelativeL2});
        CHECK(max_diff == 0.0);
    }
}

TEST_CASE("fit file: tol sets the digits of the coefficients", "[treeweave][fit_file]") {
    // The same fit at tol 1e-3 writes fewer characters per leaf line than
    // RelativePointwise, which keeps max_digits10.
    const auto leaf_chars = [](treeweave::TolKind kind) {
        treeweave::options opts{.tol_kind = kind};
        opts.filename = temp_file("digits");
        [[maybe_unused]] const auto fn = fit([](double x) { return std::exp(x); }, 0.0, 1.0, 1e-3, opts);
        const std::string text = read_text(*opts.filename);
        fs::remove(*opts.filename);
        const auto pos = text.find("\nleaf ");
        return text.find('\n', pos + 1) - pos;
    };
    CHECK(leaf_chars(treeweave::TolKind::AbsoluteMax) < leaf_chars(treeweave::TolKind::RelativePointwise));
}

TEST_CASE("fit file: non-converged panels survive the round trip", "[treeweave][fit_file]") {
    treeweave::options opts{.max_depth = 3, .allow_max_depth_leaves = true};
    opts.filename     = temp_file("panels");
    auto       f      = [](double x) { return std::sqrt(std::abs(x)); };
    const auto fitted = fit(f, -1.0, 1.0, 1e-14, opts);
    REQUIRE_FALSE(fitted.non_converged_panels().empty());
    const auto loaded =
        fit([](double) -> double { throw std::logic_error("f called on load"); }, -1.0, 1.0, 1e-14, opts);
    const auto &pa = fitted.non_converged_panels();
    const auto &pb = loaded.non_converged_panels();
    REQUIRE(pa.size() == pb.size());
    for (std::size_t i = 0; i < pa.size(); ++i) {
        CHECK(pa[i].depth == pb[i].depth);
        CHECK(pa[i].a == pb[i].a);
        CHECK(pa[i].b == pb[i].b);
    }
    fs::remove(*opts.filename);
}

TEST_CASE("fit file: a changed argument refits and overwrites", "[treeweave][fit_file]") {
    treeweave::options opts{};
    opts.filename = temp_file("mismatch");
    int  calls    = 0;
    auto f        = [&](double x) {
        ++calls;
        return std::exp(x);
    };
    auto ff = [&](float x) {
        ++calls;
        return std::exp(x);
    };
    [[maybe_unused]] const auto fit1  = fit(f, 0.0, 1.0, 1e-8, opts);
    const std::string           first = read_text(*opts.filename);

    // Each changed argument is a different header line.
    calls                            = 0;
    [[maybe_unused]] const auto fit2 = fit(f, 0.0, 1.0, 1e-10, opts);
    CHECK(calls > 0);
    CHECK(read_text(*opts.filename) != first);

    calls                            = 0;
    [[maybe_unused]] const auto fit3 = fit(f, 0.0, 2.0, 1e-10, opts);
    CHECK(calls > 0);

    calls                            = 0;
    [[maybe_unused]] const auto fit4 = fit(ff, 0.0F, 2.0F, 1e-6, opts); // float: dtype line
    CHECK(calls > 0);

    calls                            = 0;
    auto opt2                        = opts;
    opt2.max_depth                   = 40;
    [[maybe_unused]] const auto fit5 = fit(ff, 0.0F, 2.0F, 1e-6, opt2);
    CHECK(calls > 0);

    // And the same arguments again load.
    calls                            = 0;
    [[maybe_unused]] const auto fit6 = fit(ff, 0.0F, 2.0F, 1e-6, opt2);
    CHECK(calls == 0);

    // A file from another treeweave or polyfit version refits.
    for (const std::string key : {"treeweave_version ", "polyfit_version "}) {
        std::string text = read_text(*opts.filename);
        const auto  pos  = text.find("\n" + key);
        REQUIRE(pos != std::string::npos);
        text.insert(pos + 1 + key.size(), "0.0.0-other");
        write_text(*opts.filename, text);
        calls                            = 0;
        [[maybe_unused]] const auto fit7 = fit(ff, 0.0F, 2.0F, 1e-6, opt2);
        CHECK(calls > 0);
    }
    fs::remove(*opts.filename);
}

TEST_CASE("fit file: the loader reads the stored coefficients", "[treeweave][fit_file]") {
    // Positive control for the round-trip test: edit one coefficient in the
    // file and require the loaded Function to change. A loader that refit
    // instead of reading the file would fail here.
    treeweave::options opts{};
    opts.filename      = temp_file("edited");
    auto        f      = [](double x) { return std::exp(x); };
    const auto  fitted = fit(f, 0.0, 1.0, 1e-10, opts);
    std::string text   = read_text(*opts.filename);
    const auto  leaf   = text.find("\nleaf ");
    REQUIRE(leaf != std::string::npos);
    const auto eol = text.find('\n', leaf + 1);
    // The last number on the line is the constant coefficient (Horner order).
    const auto last = text.rfind(' ', eol);
    text.replace(last + 1, eol - last - 1, "100");
    write_text(*opts.filename, text);

    const auto loaded  = fit(f, 0.0, 1.0, 1e-10, opts);
    int        changed = 0;
    for (int i = 0; i <= 64; ++i) {
        const double x = i / 64.0;
        changed += same_bits(fitted(x), loaded(x)) ? 0 : 1;
    }
    CHECK(changed > 0);
    fs::remove(*opts.filename);
}

TEST_CASE("fit file: a malformed body throws", "[treeweave][fit_file]") {
    treeweave::options opts{};
    opts.filename                    = temp_file("malformed");
    auto                        f    = [](double x) { return std::exp(x); };
    [[maybe_unused]] const auto fit7 = fit(f, 0.0, 1.0, 1e-10, opts);
    const std::string           good = read_text(*opts.filename);

    // Truncated after the header and part of the body.
    write_text(*opts.filename, good.substr(0, good.find("subtrees")));
    CHECK_THROWS_AS(fit(f, 0.0, 1.0, 1e-10, opts), std::runtime_error);

    // A node that names a leaf that does not exist.
    std::string bad = good;
    const auto  pos = bad.rfind("\nleaf ");
    REQUIRE(pos != std::string::npos);
    bad.replace(pos, bad.find('\n', pos + 1) - pos, "\nleaf 999999");
    write_text(*opts.filename, bad);
    CHECK_THROWS_AS(fit(f, 0.0, 1.0, 1e-10, opts), std::runtime_error);

    // Trailing data.
    write_text(*opts.filename, good + "extra\n");
    CHECK_THROWS_AS(fit(f, 0.0, 1.0, 1e-10, opts), std::runtime_error);
    fs::remove(*opts.filename);
}

TEST_CASE("fit file: an unwritable path throws", "[treeweave][fit_file]") {
    treeweave::options opts{};
    opts.filename = (fs::temp_directory_path() / "treeweave_no_such_dir" / "fit.txt").string();
    CHECK_THROWS(fit([](double x) { return x; }, 0.0, 1.0, 1e-10, opts));
}

TEST_CASE("fit file: off by default", "[treeweave][fit_file]") {
    CHECK_FALSE(treeweave::options{}.filename.has_value());
}
