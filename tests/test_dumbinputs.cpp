// test_dumbinputs.cpp: degenerate inputs at every public entry point.
//
// Each case asserts one defined behaviour:
//   * the C++ fit throws std::invalid_argument,
//   * the C ABI returns its error value (NULL handle / NaN / no write) with a
//     non-empty treeweave_last_error(),
//   * an eval returns the documented value: NaN outside the closed domain
//     [a, b], the boundary value at a and b.
// No case may crash, hang or hit UB.
//
// guru.hpp primitives are documented as unchecked low-level calls (Debug
// asserts only); this file tests them on their documented contract only and
// the report lists the unchecked surfaces as documented limits.
#include <treeweave.h>
#include <treeweave/detail/errors.hpp>
#include <treeweave/eval_scatter.hpp>
#include <treeweave/treeweave.hpp>

#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <random>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using treeweave::fit;
using treeweave::options;

namespace fs = std::filesystem;

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

auto f1(double x) -> double { return std::sin(x); }
auto f2(std::array<double, 2> x) -> double { return x[0] + x[1]; }

// ---- C ABI callbacks -------------------------------------------------------
void c_sq(const double *x, double *y, void *) { y[0] = x[0] * x[0]; }
void c_2v2(const double *x, double *y, void *) {
    y[0] = x[0] + x[1];
    y[1] = x[0] - x[1];
}
void cf_sq(const float *x, float *y, void *) { y[0] = x[0] * x[0]; }

auto err_nonempty() -> bool { return std::string(treeweave_last_error()).size() > 0; }

// ---- fit-file helpers ------------------------------------------------------
auto temp_file(const std::string &name) -> std::string {
    const fs::path p = fs::temp_directory_path() / ("treeweave_test_dumbinputs_" + name + ".txt");
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

// Fresh empty working directory for one case: the fit file must write
// nothing there. The name comes from std::random_device because ctest runs
// the cases in parallel. The guard restores the old path and removes the
// directory at the end.
class TempCwdGuard {
  public:
    TempCwdGuard() : old_(fs::current_path()) {
        std::random_device rd;
        dir_ = fs::temp_directory_path() / ("treeweave_dumbinputs_" + std::to_string(rd()) + "_" + std::to_string(rd()));
        fs::create_directory(dir_);
        fs::current_path(dir_);
    }
    ~TempCwdGuard() {
        std::error_code ec;
        fs::current_path(old_, ec);
        fs::remove_all(dir_, ec);
    }
    TempCwdGuard(const TempCwdGuard &)            = delete;
    auto operator=(const TempCwdGuard &) -> TempCwdGuard & = delete;

    [[nodiscard]] auto entry_count() const -> std::size_t {
        return static_cast<std::size_t>(std::distance(fs::directory_iterator(dir_), fs::directory_iterator{}));
    }

  private:
    fs::path old_;
    fs::path dir_;
};

} // namespace

TEST_CASE("fit: degenerate 1D domain bounds throw std::invalid_argument", "[dumbinputs][domain]") {
    REQUIRE_THROWS_AS(fit(f1, 1.0, 1.0, 1e-3), std::invalid_argument);      // a == b
    REQUIRE_THROWS_AS(fit(f1, 1.0, 0.0, 1e-3), std::invalid_argument);      // a > b
    REQUIRE_THROWS_AS(fit(f1, kNaN, 1.0, 1e-3), std::invalid_argument);     // NaN a
    REQUIRE_THROWS_AS(fit(f1, 0.0, kNaN, 1e-3), std::invalid_argument);     // NaN b
    REQUIRE_THROWS_AS(fit(f1, kNaN, kNaN, 1e-3), std::invalid_argument);    // NaN both
    REQUIRE_THROWS_AS(fit(f1, 0.0, kInf, 1e-3), std::invalid_argument);     // +inf b
    REQUIRE_THROWS_AS(fit(f1, -kInf, 0.0, 1e-3), std::invalid_argument);    // -inf a
    REQUIRE_THROWS_AS(fit(f1, -kInf, kInf, 1e-3), std::invalid_argument);   // infinite span
    REQUIRE_THROWS_AS(fit(f1, kInf, kInf, 1e-3), std::invalid_argument);    // inf == inf
    // b - a overflows to +inf.
    REQUIRE_THROWS_AS(fit(f1, -1.7e308, 1.7e308, 1e-3), std::invalid_argument);
    // a + b overflows the midpoint to +inf while b - a stays finite.
    REQUIRE_THROWS_AS(fit(f1, 1e308, 1.7e308, 1e-3), std::invalid_argument);
    // float domain: same contract.
    REQUIRE_THROWS_AS(fit([](float x) { return x * x; }, 1.0F, 1.0F, 1e-3), std::invalid_argument);
    // Complex-output adaptor branch of fit: same contract.
    REQUIRE_THROWS_AS(fit([](double x) { return std::complex<double>(x, 0.0); }, 1.0, 1.0, 1e-3),
                      std::invalid_argument);
}

TEST_CASE("fit: one bad axis in a multi-dim domain throws", "[dumbinputs][domain]") {
    REQUIRE_THROWS_AS(fit(f2, std::array{0.0, 0.0}, std::array{1.0, 0.0}, 1e-3), std::invalid_argument);
    REQUIRE_THROWS_AS(fit(f2, std::array{0.0, 2.0}, std::array{1.0, 1.0}, 1e-3), std::invalid_argument);
    REQUIRE_THROWS_AS(fit(f2, std::array{0.0, kNaN}, std::array{1.0, 1.0}, 1e-3), std::invalid_argument);
    using f3_t = double (*)(std::array<double, 3>);
    const f3_t f3 = +[](std::array<double, 3> x) { return x[0] + x[1] + x[2]; };
    REQUIRE_THROWS_AS(fit(f3, std::array{0.0, 0.0, 0.0}, std::array{1.0, 1.0, kInf}, 1e-3),
                      std::invalid_argument);
}

TEST_CASE("fit: tolerance must be finite and positive", "[dumbinputs][tol]") {
    REQUIRE_THROWS_AS(fit(f1, 0.0, 1.0, 0.0), std::invalid_argument);
    REQUIRE_THROWS_AS(fit(f1, 0.0, 1.0, -1.0), std::invalid_argument);
    REQUIRE_THROWS_AS(fit(f1, 0.0, 1.0, kNaN), std::invalid_argument);
    REQUIRE_THROWS_AS(fit(f1, 0.0, 1.0, kInf), std::invalid_argument);
    REQUIRE_THROWS_AS(fit(f1, 0.0, 1.0, -kInf), std::invalid_argument);
}

TEST_CASE("eval: documented values at and around the closed domain [a, b]", "[dumbinputs][eval]") {
    auto       sq = [](double x) { return x * x; };
    const auto fn = fit(sq, 0.0, 1.0, 1e-10);
    REQUIRE(fn(0.0) == Catch::Approx(0.0).margin(1e-9));   // exactly a
    REQUIRE(fn(1.0) == Catch::Approx(1.0).epsilon(1e-9));  // exactly b (closed)
    REQUIRE(std::isnan(fn(-1e-9)));                        // just below a
    REQUIRE(std::isnan(fn(1.0 + 1e-9)));                   // just above b
    REQUIRE(std::isnan(fn(kNaN)));
    REQUIRE(std::isnan(fn(kInf)));
    REQUIRE(std::isnan(fn(-kInf)));

    const std::array<double, 3> xs{0.0, 0.5, 1.0};
    std::array<double, 3>       ys{};
    fn(xs.data(), ys.data(), xs.size());
    REQUIRE(ys[0] == Catch::Approx(0.0).margin(1e-9));
    REQUIRE(ys[1] == Catch::Approx(0.25).epsilon(1e-9));
    REQUIRE(ys[2] == Catch::Approx(1.0).epsilon(1e-9));

    std::array<double, 3> ys_sorted{};
    fn.sorted(xs.data(), ys_sorted.data(), xs.size());
    REQUIRE(ys_sorted == ys);

    const auto packed = fn.eval_pack(std::array<double, 2>{0.25, 0.75});
    REQUIRE(packed[0] == Catch::Approx(0.0625).epsilon(1e-9));
    REQUIRE(packed[1] == Catch::Approx(0.5625).epsilon(1e-9));
    const auto packed_nan = fn.eval_pack(std::array<double, 1>{kNaN});
    REQUIRE(std::isnan(packed_nan[0]));
}

TEST_CASE("eval: zero-count batches are no-ops", "[dumbinputs][eval]") {
    auto       sq = [](double x) { return x * x; };
    const auto fn = fit(sq, 0.0, 1.0, 1e-10);
    REQUIRE_NOTHROW(fn(nullptr, nullptr, 0));
    REQUIRE_NOTHROW(fn.sorted(nullptr, nullptr, 0));
    REQUIRE(fn.eval_pack(std::array<double, 0>{}).empty());
}

TEST_CASE("eval_scatter_sorted: documented contract only", "[dumbinputs][guru]") {
    auto       sq = [](double x) { return x * x; };
    const auto f0 = fit(sq, 0.0, 1.0, 1e-10);
    using fn_t    = std::remove_cvref_t<decltype(f0)>;
    const fn_t *fits_arr[1] = {&f0};
    const std::span<const fn_t *const> fits(fits_arr);

    // n == 0: documented early return.
    const std::span<const std::uint32_t> ids0;
    const std::span<const double>        xs0;
    const std::span<double>              ys0;
    REQUIRE_NOTHROW(treeweave::eval_scatter_sorted(fits, ids0, xs0, ys0, 1));

    // Valid contract: one in-domain point routes to the named fit.
    const std::uint32_t ids_arr[1] = {0};
    const double        xs_arr[1]  = {0.5};
    double              ys_arr[1]  = {0.0};
    treeweave::eval_scatter_sorted(fits, std::span<const std::uint32_t>(ids_arr, 1),
                                   std::span<const double>(xs_arr, 1), std::span<double>(ys_arr, 1), 1);
    REQUIRE(ys_arr[0] == Catch::Approx(0.25).epsilon(1e-9));
}

TEST_CASE("C ABI: fit rejects null, bad dims, degenerate domains and bad tol", "[dumbinputs][c]") {
    const double a = 0.0, b = 1.0;

    REQUIRE(treeweave_fit(nullptr, 1, 1, &a, &b, 1e-3, nullptr, nullptr) == nullptr);
    REQUIRE(err_nonempty());
    REQUIRE(treeweave_fit(c_sq, 1, 1, nullptr, &b, 1e-3, nullptr, nullptr) == nullptr);
    REQUIRE(err_nonempty());
    REQUIRE(treeweave_fit(c_sq, 1, 1, &a, nullptr, 1e-3, nullptr, nullptr) == nullptr);
    REQUIRE(err_nonempty());

    REQUIRE(treeweave_fit(c_sq, 0, 1, &a, &b, 1e-3, nullptr, nullptr) == nullptr);
    REQUIRE(err_nonempty());
    REQUIRE(treeweave_fit(c_sq, -1, 1, &a, &b, 1e-3, nullptr, nullptr) == nullptr);
    REQUIRE(err_nonempty());
    REQUIRE(treeweave_fit(c_sq, 4, 1, &a, &b, 1e-3, nullptr, nullptr) == nullptr);
    REQUIRE(err_nonempty());
    REQUIRE(treeweave_fit(c_sq, 1, 0, &a, &b, 1e-3, nullptr, nullptr) == nullptr);
    REQUIRE(err_nonempty());
    REQUIRE(treeweave_fit(c_sq, 1, 4, &a, &b, 1e-3, nullptr, nullptr) == nullptr);
    REQUIRE(err_nonempty());

    const double cases[4][2] = {{1.0, 1.0}, {1.0, 0.0}, {kNaN, 1.0}, {0.0, kInf}};
    for (const auto &ab : cases) {
        REQUIRE(treeweave_fit(c_sq, 1, 1, &ab[0], &ab[1], 1e-3, nullptr, nullptr) == nullptr);
        REQUIRE(err_nonempty());
    }

    REQUIRE(treeweave_fit(c_sq, 1, 1, &a, &b, 0.0, nullptr, nullptr) == nullptr);
    REQUIRE(err_nonempty());
    REQUIRE(treeweave_fit(c_sq, 1, 1, &a, &b, kNaN, nullptr, nullptr) == nullptr);
    REQUIRE(err_nonempty());
    REQUIRE(treeweave_fit(c_sq, 1, 1, &a, &b, kInf, nullptr, nullptr) == nullptr);
    REQUIRE(err_nonempty());

    // An unnamed tol_kind value (7 is in the enum range): the fit cannot
    // converge and fails with a message instead of a handle.
    treeweave_opts co{};
    treeweave_default_opts(&co);
    co.tol_kind = static_cast<treeweave_tol_kind_t>(7);
    REQUIRE(treeweave_fit(c_sq, 1, 1, &a, &b, 1e-3, nullptr, &co) == nullptr);
    REQUIRE(err_nonempty());

    const float af = 1.0F, bf = 1.0F;
    REQUIRE(treeweavef_fit(cf_sq, 1, 1, &af, &bf, 1e-3, nullptr, nullptr) == nullptr);
    REQUIRE(err_nonempty());
}

TEST_CASE("C ABI: eval rejects null buffers with last_error", "[dumbinputs][c]") {
    const double a = 0.0, b = 1.0;
    treeweave_t  h = treeweave_fit(c_sq, 1, 1, &a, &b, 1e-3, nullptr, nullptr);
    REQUIRE(h != nullptr);

    double y = 42.0;
    treeweave_eval(h, nullptr, &y);
    REQUIRE(y == 42.0);
    REQUIRE(err_nonempty());
    const double x = 0.5;
    treeweave_eval(h, &x, nullptr);
    REQUIRE(err_nonempty());

    double res[2] = {1.0, 2.0};
    treeweave_batch(h, nullptr, res, 1);
    REQUIRE(res[0] == 1.0);
    REQUIRE(err_nonempty());
    treeweave_batch(h, &x, nullptr, 1);
    REQUIRE(err_nonempty());
    treeweave_sorted(h, nullptr, res, 1);
    REQUIRE(err_nonempty());
    treeweave_sorted(h, &x, nullptr, 1);
    REQUIRE(err_nonempty());

    double      o0 = 1.0, o1 = 2.0;
    double     *soa_ok[2]   = {&o0, &o1};
    double     *soa_bad[2]  = {&o0, nullptr};
    treeweave_transposed(h, &x, nullptr, 1);
    REQUIRE(err_nonempty());
    treeweave_transposed(h, &x, soa_bad, 1);
    REQUIRE(err_nonempty());

    // f32 twins share the guards.
    const float af2 = 0.0F, bf2 = 1.0F;
    treeweave_t hf  = treeweavef_fit(cf_sq, 1, 1, &af2, &bf2, 1e-3, nullptr, nullptr);
    REQUIRE(hf != nullptr);
    float yf = 42.0F;
    treeweavef_eval(hf, nullptr, &yf);
    REQUIRE(yf == 42.0F);
    REQUIRE(err_nonempty());
    const float xf = 0.5F;
    treeweavef_sorted(hf, &xf, nullptr, 1);
    REQUIRE(err_nonempty());

    // Documented: SoA needs output_dim > 1, sorted needs input_dim == 1.
    treeweave_transposed(h, &x, soa_ok, 1);
    REQUIRE(err_nonempty());

    h  = treeweave_free(h);
    hf = treeweave_free(hf);
}

TEST_CASE("C ABI: documented errors for arity, dtype and null handle", "[dumbinputs][c]") {
    const double a = 0.0, b = 1.0;
    treeweave_t  h = treeweave_fit(c_sq, 1, 1, &a, &b, 1e-3, nullptr, nullptr);
    REQUIRE(h != nullptr);

    REQUIRE(std::isnan(treeweave_eval_1d(nullptr, 0.5)));
    REQUIRE(err_nonempty());
    REQUIRE(std::isnan(treeweave_eval_2d(h, 0.5, 0.5)));
    REQUIRE(err_nonempty());
    REQUIRE(std::isnan(treeweavef_eval_1d(h, 0.5F)));
    REQUIRE(err_nonempty());

    REQUIRE(treeweave_dtype(nullptr) == TREEWEAVE_F64);
    REQUIRE(err_nonempty());
    REQUIRE(treeweave_input_dim(nullptr) == 0);
    REQUIRE(err_nonempty());
    REQUIRE(treeweave_output_dim(nullptr) == 0);
    REQUIRE(err_nonempty());
    REQUIRE(treeweave_memory_usage(nullptr) == 0);
    REQUIRE(treeweave_free(nullptr) == nullptr);

    h = treeweave_free(h);
}

TEST_CASE("C ABI: eval values at domain edges", "[dumbinputs][c]") {
    const double a = 0.0, b = 1.0;
    treeweave_t  h = treeweave_fit(c_sq, 1, 1, &a, &b, 1e-3, nullptr, nullptr);
    REQUIRE(h != nullptr);

    REQUIRE(treeweave_eval_1d(h, 0.0) == Catch::Approx(0.0).margin(1e-9));  // exactly a
    REQUIRE(treeweave_eval_1d(h, 1.0) == Catch::Approx(1.0).epsilon(1e-9)); // exactly b
    REQUIRE(std::isnan(treeweave_eval_1d(h, -1e-9)));
    REQUIRE(std::isnan(treeweave_eval_1d(h, 1.0 + 1e-9)));
    REQUIRE(std::isnan(treeweave_eval_1d(h, kNaN)));
    REQUIRE(std::isnan(treeweave_eval_1d(h, kInf)));
    REQUIRE(std::isnan(treeweave_eval_1d(h, -kInf)));

    h = treeweave_free(h);
}

TEST_CASE("C ABI: 2-in 2-out SoA guards, values and zero-count", "[dumbinputs][c]") {
    const double a2[2] = {0.0, 0.0}, b2[2] = {1.0, 1.0};
    treeweave_t  h2 = treeweave_fit(c_2v2, 2, 2, a2, b2, 1e-3, nullptr, nullptr);
    REQUIRE(h2 != nullptr);

    const double x2[2] = {0.25, 0.5};
    double       o0 = 42.0, o1 = 42.0;
    double      *soa_ok[2]  = {&o0, &o1};
    double      *soa_bad[2] = {&o0, nullptr};
    treeweave_transposed(h2, x2, soa_bad, 1); // second buffer NULL: no write
    REQUIRE(o0 == 42.0);
    REQUIRE(err_nonempty());

    treeweave_transposed(h2, x2, soa_ok, 1);
    REQUIRE(o0 == Catch::Approx(0.75).epsilon(1e-6));   // x0 + x1
    REQUIRE(o1 == Catch::Approx(-0.25).epsilon(1e-6));  // x0 - x1

    // Zero count: a documented no-op, buffers untouched.
    double res0[2] = {7.0, 8.0};
    treeweave_batch(h2, x2, res0, 0);
    REQUIRE(res0[0] == 7.0);
    REQUIRE(res0[1] == 8.0);

    h2 = treeweave_free(h2);
}

TEST_CASE("fit file: dumb inputs to save and load", "[dumbinputs][fit_file]") {
    auto       f   = [](double x) { return std::exp(x); };
    const auto tol = 1e-10;

    SECTION("missing file: fit writes it") {
        treeweave::options opts{};
        opts.filename = temp_file("missing");
        REQUIRE_NOTHROW((void)fit(f, 0.0, 1.0, tol, opts));
        REQUIRE(fs::exists(*opts.filename));
        fs::remove(*opts.filename);
    }
    SECTION("empty and garbage files are rejected and overwritten") {
        treeweave::options opts{};
        opts.filename = temp_file("garbage");
        write_text(*opts.filename, "");
        REQUIRE_NOTHROW((void)fit(f, 0.0, 1.0, tol, opts));
        write_text(*opts.filename, "not a fit file\n");
        REQUIRE_NOTHROW((void)fit(f, 0.0, 1.0, tol, opts));
        REQUIRE_FALSE(read_text(*opts.filename).empty());
        fs::remove(*opts.filename);
    }
    SECTION("truncated body throws std::runtime_error") {
        treeweave::options opts{};
        opts.filename                    = temp_file("truncated");
        [[maybe_unused]] const auto kept = fit(f, 0.0, 1.0, tol, opts);
        const std::string           good = read_text(*opts.filename);
        write_text(*opts.filename, good.substr(0, good.find("subtrees")));
        REQUIRE_THROWS_AS(fit(f, 0.0, 1.0, tol, opts), std::runtime_error);
        fs::remove(*opts.filename);
    }
    SECTION("unwritable path throws std::runtime_error") {
        treeweave::options opts{};
        opts.filename = (fs::temp_directory_path() / "treeweave_no_such_dir_dumb" / "fit.txt").string();
        REQUIRE_THROWS_AS(fit(f, 0.0, 1.0, tol, opts), std::runtime_error);
    }
    SECTION("a file whose domain is degenerate never loads") {
        treeweave::options opts{};
        opts.filename                    = temp_file("degenerate_domain");
        [[maybe_unused]] const auto kept = fit(f, 0.0, 1.0, tol, opts);
        std::string                bad   = read_text(*opts.filename);
        const auto                 pos   = bad.find("upper 1");
        REQUIRE(pos != std::string::npos);
        bad.replace(pos, 7, "upper 0"); // stored domain [0, 0]
        write_text(*opts.filename, bad);

        // A degenerate request is rejected before the load runs, even though
        // the file header matches it.
        REQUIRE_THROWS_AS(fit(f, 0.0, 0.0, tol, opts), std::invalid_argument);
        // A valid request does not load it: header mismatch refits and
        // overwrites the file with the valid domain.
        REQUIRE_NOTHROW((void)fit(f, 0.0, 1.0, tol, opts));
        REQUIRE(read_text(*opts.filename).find("upper 1") != std::string::npos);
        fs::remove(*opts.filename);
    }
}

TEST_CASE("fit file: an empty filename means no file (C++)", "[dumbinputs][fit_file]") {
    const TempCwdGuard cwd;
    auto               f = [](double x) { return std::exp(x); };
    treeweave::options opts{};
    opts.filename = std::string("");
    REQUIRE_NOTHROW((void)fit(f, 0.0, 1.0, 1e-10, opts));
    REQUIRE(cwd.entry_count() == 0);
}

TEST_CASE("C ABI: an empty filename means no file", "[dumbinputs][fit_file][c]") {
    const TempCwdGuard cwd;
    const double       a = 0.0, b = 1.0;
    treeweave_opts     co{};
    treeweave_default_opts(&co);
    co.filename = "";
    treeweave_t h = treeweave_fit(c_sq, 1, 1, &a, &b, 1e-3, nullptr, &co);
    REQUIRE(h != nullptr);
    h = treeweave_free(h);
    REQUIRE(cwd.entry_count() == 0);
}

TEST_CASE("fit file: a failed save leaves no temporary file", "[dumbinputs][fit_file]") {
    const TempCwdGuard cwd;
    auto               f = [](double x) { return std::exp(x); };
    treeweave::options opts{};
    // The target names an existing directory: the save writes the temporary
    // file first, then the rename to the target fails.
    const fs::path target = fs::current_path() / "target_dir";
    fs::create_directory(target);
    opts.filename = target.string();
    REQUIRE_THROWS_AS(fit(f, 0.0, 1.0, 1e-10, opts), std::filesystem::filesystem_error);
    REQUIRE(cwd.entry_count() == 1); // the target directory only
    REQUIRE_FALSE(fs::exists(target.string() + ".tmp"));
}
