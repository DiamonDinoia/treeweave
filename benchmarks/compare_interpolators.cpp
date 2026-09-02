// Compare treeweave against the interpolators a C++ user reaches for.
//
// Same protocol as benchmarks/compare_interpolators.py and .jl: for each target
// and each requested accuracy, every method is grown until it MEETS that
// accuracy on a dense test grid, and only then measured. The table compares like
// with like: same achieved error, different cost.
//
// Reported per method:
//   f-evals   calls to the target needed to build the approximation
//   memory    heap bytes the approximation holds after construction
//   Meval/s   evaluation throughput on a shuffled batch of 1e6 points
//   max err   achieved max error relative to max|f| on the test grid
//
// The comparison set:
//   boost cardinal cubic     cardinal_cubic_b_spline, uniform grid: the default reach
//   boost cardinal quintic   cardinal_quintic_b_spline, uniform grid
//
// Boost.Math's barycentric_rational is deliberately not in the field: it costs
// O(n) per evaluation, so growing it to 1e-10 costs more throughput than the
// table can report. GSL's gsl_spline cubic is the same algorithm as Boost's
// cardinal cubic on a uniform grid, so it would add a row and no information.
//
// Run:  compare_interpolators_cpp [--self-test] [--rst] [--check-docs]

#include <treeweave/treeweave.hpp>

#include <boost/math/interpolators/cardinal_cubic_b_spline.hpp>
#include <boost/math/interpolators/cardinal_quintic_b_spline.hpp>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include <cstdlib>
#include <new>

namespace {

// --- the memory column -------------------------------------------------------
//
// Live heap bytes, counted by replacing the global allocation functions. The
// interpolants keep their coefficients in heap vectors, so the delta across a
// construction is what the approximation costs. Counting requested bytes rather
// than sampling the allocator (mallinfo2) keeps the column deterministic: it
// cannot move with free-list state or fragmentation.
//
// Every block carries a header holding its size and whether it was counted, so
// a block allocated before counting started can be freed after without
// underflowing the total.

std::size_t           g_live     = 0;
bool                  g_counting = false;
constexpr std::size_t HEADER     = 32; // >= 16 bytes of header, keeps 16-byte alignment

struct block_header {
    std::size_t size;
    std::size_t counted;
};

auto tagged_alloc(std::size_t n, std::size_t align) -> void * {
    const std::size_t prefix = std::max(align, HEADER);
    // aligned_alloc needs a size that is a multiple of the alignment.
    const std::size_t total = ((n + prefix + align - 1) / align) * align;
    void             *base  = std::aligned_alloc(align, total);
    if (base == nullptr)
        throw std::bad_alloc();
    auto *head    = static_cast<block_header *>(base);
    head->size    = n;
    head->counted = g_counting ? 1 : 0;
    if (g_counting)
        g_live += n;
    return static_cast<char *>(base) + prefix;
}

auto tagged_free(void *p, std::size_t align) noexcept -> void {
    if (p == nullptr)
        return;
    const std::size_t prefix = std::max(align, HEADER);
    auto             *base   = static_cast<char *>(p) - prefix;
    const auto       *head   = reinterpret_cast<block_header *>(base);
    if (head->counted != 0)
        g_live -= head->size;
    std::free(base);
}

// Live counted bytes. Sampled around a construction, the delta is the object.
auto counted_bytes() -> std::size_t { return g_live; }

} // namespace

void *operator new(std::size_t n) { return tagged_alloc(n, HEADER); }
void *operator new[](std::size_t n) { return tagged_alloc(n, HEADER); }
void *operator new(std::size_t n, std::align_val_t a) { return tagged_alloc(n, static_cast<std::size_t>(a)); }
void *operator new[](std::size_t n, std::align_val_t a) { return tagged_alloc(n, static_cast<std::size_t>(a)); }
void  operator delete(void *p) noexcept { tagged_free(p, HEADER); }
void  operator delete[](void *p) noexcept { tagged_free(p, HEADER); }
void  operator delete(void *p, std::size_t) noexcept { tagged_free(p, HEADER); }
void  operator delete[](void *p, std::size_t) noexcept { tagged_free(p, HEADER); }
void  operator delete(void *p, std::align_val_t a) noexcept { tagged_free(p, static_cast<std::size_t>(a)); }
void  operator delete[](void *p, std::align_val_t a) noexcept { tagged_free(p, static_cast<std::size_t>(a)); }
void operator delete(void *p, std::size_t, std::align_val_t a) noexcept { tagged_free(p, static_cast<std::size_t>(a)); }
void operator delete[](void *p, std::size_t, std::align_val_t a) noexcept {
    tagged_free(p, static_cast<std::size_t>(a));
}

namespace {

constexpr std::size_t N_TEST       = 200'001;
constexpr std::size_t N_BENCH      = 1'000'000;
constexpr int         REPEATS      = 5;
constexpr std::size_t MAX_SIZE     = 1u << 20;
constexpr double      TOLERANCES[] = {1e-6, 1e-10};

const std::string TREEWEAVE  = "treeweave";
const std::string DOCS_TABLE = "docs/guides/performance.rst";
// The per-language subsections of "Against the alternatives". Every one of them
// carries the same target labels and a treeweave row, so the parser is scoped.
const std::vector<std::string> SECTIONS = {"In Python", "In Julia", "In C++", "In Octave"};
const std::string              SECTION  = "In C++";

struct target {
    std::string                   name;
    std::function<double(double)> f;
    double                        a, b;
};

struct ctx {
    const target       &t;
    std::vector<double> xs, x_test, y_test;
    double              scale;

    ctx(const target &tt, std::mt19937_64 &rng) : t(tt) {
        std::uniform_real_distribution<double> u(tt.a, tt.b);
        xs.resize(N_BENCH);
        for (auto &x : xs)
            x = u(rng);
        x_test.resize(N_TEST);
        y_test.resize(N_TEST);
        for (std::size_t i = 0; i < N_TEST; ++i) {
            x_test[i] = tt.a + (tt.b - tt.a) * static_cast<double>(i) / static_cast<double>(N_TEST - 1);
            y_test[i] = tt.f(x_test[i]);
        }
        scale = 0.0;
        for (double y : y_test)
            scale = std::max(scale, std::abs(y));
    }
};

struct result {
    std::size_t evals  = 0;
    std::size_t memory = 0; // 0 marks "never reached the tolerance"
    double      rate   = std::numeric_limits<double>::quiet_NaN();
    double      err    = std::numeric_limits<double>::infinity();
};

template <class Eval>
auto max_error(const ctx &c, Eval &&ev) -> double {
    double worst = 0.0;
    for (std::size_t i = 0; i < N_TEST; ++i)
        worst = std::max(worst, std::abs(ev(c.x_test[i]) - c.y_test[i]));
    return worst / c.scale;
}

// Mevals/s, minimum over repeats (the least contaminated run).
template <class Eval>
auto throughput(const ctx &c, Eval &&ev) -> double {
    ev(c.xs.front()); // warm up: the other three arms do, so this one must too
    double best = std::numeric_limits<double>::infinity();
    for (int r = 0; r < REPEATS; ++r) {
        const auto t0   = std::chrono::steady_clock::now();
        double     sink = 0.0;
        for (double x : c.xs)
            sink += ev(x);
        const auto dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        best          = std::min(best, dt);
        // Keep the loop from being optimized away without timing the check.
        if (std::isnan(sink))
            std::abort();
    }
    return static_cast<double>(c.xs.size()) / best / 1e6;
}

// Double the size until the approximation meets tol, then measure it.
// build(n) returns the interpolant; it must free every temporary before it
// returns, so the heap delta is the interpolant alone.
template <class Build>
auto grow(const ctx &c, double tol, Build &&build, std::size_t start = 16) -> result {
    result r;
    for (std::size_t n = start; n <= MAX_SIZE; n *= 2) {
        const auto before = counted_bytes();
        auto       obj    = build(n);
        const auto after  = counted_bytes();
        auto       ev     = [&obj](double x) { return obj(x); };
        r.evals           = n;
        r.err             = max_error(c, ev);
        if (r.err <= tol) {
            r.memory = after - before;
            r.rate   = throughput(c, ev);
            return r;
        }
    }
    r.memory = 0;
    return r;
}

// --- the methods -------------------------------------------------------------

auto m_treeweave(const ctx &c, double tol) -> result {
    result r;
    double requested = tol;
    for (int attempt = 0; attempt < 6; ++attempt) {
        std::size_t calls   = 0;
        auto        counted = [&](double x) {
            ++calls;
            return c.t.f(x);
        };
        const auto before = counted_bytes();
        auto       fn     = treeweave::fit(counted, c.t.a, c.t.b, requested);
        const auto after  = counted_bytes();
        auto       ev     = [&fn](double x) { return fn(x); };
        r.evals           = calls;
        r.err             = max_error(c, ev);
        if (r.err <= tol) {
            r.memory = after - before;
            r.rate   = throughput(c, ev);
            return r;
        }
        requested /= 100;
    }
    r.evals  = 0;
    r.memory = 0;
    return r;
}

auto m_boost_cubic(const ctx &c, double tol) -> result {
    return grow(c, tol, [&c](std::size_t n) {
        const double        h = (c.t.b - c.t.a) / static_cast<double>(n - 1);
        std::vector<double> y(n);
        for (std::size_t i = 0; i < n; ++i)
            y[i] = c.t.f(c.t.a + h * static_cast<double>(i));
        return boost::math::interpolators::cardinal_cubic_b_spline<double>(y.data(), n, c.t.a, h);
    });
}

auto m_boost_quintic(const ctx &c, double tol) -> result {
    return grow(c, tol, [&c](std::size_t n) {
        const double        h = (c.t.b - c.t.a) / static_cast<double>(n - 1);
        std::vector<double> y(n);
        for (std::size_t i = 0; i < n; ++i)
            y[i] = c.t.f(c.t.a + h * static_cast<double>(i));
        return boost::math::interpolators::cardinal_quintic_b_spline<double>(y.data(), n, c.t.a, h);
    });
}

using method_fn = result (*)(const ctx &, double);
// Order here is the order of the rows in the printed table and in the docs.
const std::vector<std::pair<std::string, method_fn>> METHODS = {
    {TREEWEAVE, m_treeweave},
    {"boost cardinal cubic", m_boost_cubic},
    {"boost cardinal quintic", m_boost_quintic},
};

// The methods the near-pole gate holds treeweave against: splines on a uniform
// knot grid, which is what refinement has to beat.
const std::vector<std::string> UNIFORM_SPLINES = {"boost cardinal cubic", "boost cardinal quintic"};

auto zeta_n(double s) -> double {
    double a = 0.0;
    for (int k = 1; k <= 1000; ++k)
        a += std::pow(static_cast<double>(k), -s);
    return a;
}

const std::string POLE = "1/(x - 1.05) on [-1, 1]";

const std::vector<target> TARGETS = {
    // An expensive smooth function: the case treeweave is built for.
    {"zeta(s), 1000 terms, on [2, 10]", zeta_n, 2.0, 10.0},
    // A pole just outside the domain: the case adaptivity is built for.
    {POLE, [](double x) { return 1.0 / (x - 1.05); }, -1.0, 1.0},
    // Oscillation: nobody's favourite, included so the table is not cherry-picked.
    {"sin(30 x) on [0, 1]", [](double x) { return std::sin(30.0 * x); }, 0.0, 1.0},
};

// How each target is written in docs/guides/performance.rst.
const std::map<std::string, std::string> TITLES = {
    {"zeta(s), 1000 terms, on [2, 10]", "``zeta(s)``, 1000 terms, on [2, 10]"},
    {POLE, "``1/(x - 1.05)`` on [-1, 1]"},
    {"sin(30 x) on [0, 1]", "``sin(30 x)`` on [0, 1]"},
};

// One helper per format: a non-literal format string trips -Wformat-nonliteral.
auto fmt_err(double v) -> std::string {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.1e", v);
    return buf;
}

auto fmt_rate(double v, bool decimal) -> std::string {
    char buf[64];
    std::snprintf(buf, sizeof buf, decimal ? "%.1f" : "%.0f", v);
    return buf;
}

auto fmt_memory(std::size_t memory) -> std::string {
    if (memory == 0)
        return "n/a";
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.1f KiB", static_cast<double>(memory) / 1024.0);
    return buf;
}

auto fmt_tol(double tol) -> std::string {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.0e", tol);
    std::string s  = buf;
    const auto  at = s.find("e-0");
    if (at != std::string::npos)
        s.erase(at + 2, 1);
    return s;
}

using key   = std::tuple<std::string, double, std::string>;
using table = std::map<key, result>;

} // namespace

namespace {

// Every published claim this benchmark can settle.
//
// Every treeweave row must have reached the accuracy it was asked for, and near
// the pole refinement must beat a spline on a uniform knot grid on both f-evals
// and memory. The pole comparison alone cannot see a fit that never converged:
// that one reports zero memory, which beats any competitor.
auto check(const table &rows) -> int {
    std::vector<std::string> failures;
    for (const auto &[k, r] : rows) {
        const auto &[title, tol, name] = k;
        if (name != TREEWEAVE)
            continue;
        if (r.err > 10 * tol)
            failures.push_back(title + " @ " + fmt_tol(tol) + ": treeweave err " + fmt_err(r.err) + " > 10x tol");
        if (r.memory == 0)
            failures.push_back(title + " @ " + fmt_tol(tol) + ": treeweave never reached the tolerance");
    }
    for (double tol : TOLERANCES) {
        const result &tw = rows.at({POLE, tol, TREEWEAVE});
        for (const auto &name : UNIFORM_SPLINES) {
            const auto it = rows.find({POLE, tol, name});
            if (it == rows.end() || it->second.memory == 0)
                continue;
            const result                                                      &other     = it->second;
            const std::pair<const char *, std::pair<std::size_t, std::size_t>> columns[] = {
                {"f-evals", {tw.evals, other.evals}},
                {"memory", {tw.memory, other.memory}},
            };
            for (const auto &[column, pair] : columns)
                if (pair.first >= pair.second)
                    failures.push_back("near the pole at " + fmt_tol(tol) + ": treeweave " + column + " " +
                                       std::to_string(pair.first) + " is not below " + name + "'s " +
                                       std::to_string(pair.second));
        }
    }
    for (const auto &f : failures)
        std::cout << "FAIL: " << f << "\n";
    std::cout << (failures.empty() ? "every claim holds" : std::to_string(failures.size()) + " claim(s) failed")
              << "\n";
    return failures.empty() ? 0 : 1;
}

// Emit the docs table for the C++ field. Paste over the table in performance.rst.
auto as_rst(const table &rows, const std::vector<std::string> &order) -> std::string {
    std::ostringstream out;
    for (const auto &t : TARGETS) {
        const std::string &label = TITLES.at(t.name);
        out << label << "\n"
            << std::string(label.size(), '^') << "\n\n"
            << ".. list-table::\n   :header-rows: 1\n   :widths: 7 26 9 11 8 9\n\n"
            << "   * - tol\n     - method\n     - f-evals\n     - memory\n     - Meval/s\n"
            << "     - max err\n";
        for (double tol : TOLERANCES)
            for (const auto &name : order) {
                const auto it = rows.find({t.name, tol, name});
                if (it == rows.end())
                    continue;
                const result &r = it->second;
                out << "   * - " << fmt_tol(tol) << "\n     - " << name << "\n     - " << r.evals << "\n     - "
                    << fmt_memory(r.memory) << "\n     - " << (std::isnan(r.rate) ? "n/a" : fmt_rate(r.rate, false))
                    << "\n     - " << fmt_err(r.err) << "\n";
            }
        out << "\n";
    }
    return out.str();
}

auto trim(const std::string &s) -> std::string {
    const auto b = s.find_first_not_of(" \t\r");
    if (b == std::string::npos)
        return "";
    return s.substr(b, s.find_last_not_of(" \t\r") - b + 1);
}

using published = std::map<key, std::pair<std::string, std::string>>;

// Read the published table back as (title, tol, method) -> (evals, memory).
//
// Each target's table is introduced by a line holding exactly that target's
// label, so the parser keys rows on the most recent such line. Only rows under
// `section` are read, and only for methods in `order`: the other language
// sections repeat both the labels and the treeweave row under their headings.
auto parse_docs_table(const std::string &text, const std::vector<std::string> &order, const std::string &section)
    -> published {
    std::map<std::string, std::string> labels;
    for (const auto &[name, label] : TITLES)
        labels[label] = name;

    published                out;
    std::string              title;
    std::vector<std::string> cells;
    // A document with no section heading at all is one table (the self-test).
    bool inside = true;

    auto flush = [&] {
        if (!title.empty() && cells.size() == 6 && std::find(order.begin(), order.end(), cells[1]) != order.end())
            out[{title, std::stod(cells[0]), cells[1]}] = {cells[2], cells[3]};
    };

    std::istringstream in(text);
    for (std::string line; std::getline(in, line);) {
        const std::string s = trim(line);
        if (std::find(SECTIONS.begin(), SECTIONS.end(), s) != SECTIONS.end()) {
            flush();
            inside = s == section;
            title.clear();
            cells.clear();
        } else if (!inside) {
            continue;
        } else if (labels.count(s)) {
            flush();
            title = labels[s];
            cells.clear();
        } else if (s.rfind("* -", 0) == 0) {
            flush();
            cells = {trim(s.substr(3))};
        } else if (s.rfind("- ", 0) == 0 && !cells.empty()) {
            cells.push_back(trim(s.substr(2)));
        } else if (s.empty()) {
            flush();
            cells.clear();
        }
    }
    flush();
    return out;
}

// The published table must be the one this program measures.
//
// Only the deterministic columns are compared: f-evals and memory are set by the
// algorithms, not by the machine. Regenerate with --rst after a change that
// moves them.
auto check_docs(const table &rows, const std::vector<std::string> &order, const std::string &path = DOCS_TABLE,
                const std::string &section = SECTION) -> int {
    std::ifstream file(path);
    if (!file) {
        std::cout << "FAIL: cannot read " << path << "\n";
        return 1;
    }
    std::string text, line;
    while (std::getline(file, line))
        text += line + "\n";
    const published was      = parse_docs_table(text, order, section);
    int             failures = 0;
    for (const auto &[k, r] : rows) {
        const auto &[title, tol, name] = k;
        if (std::find(order.begin(), order.end(), name) == order.end())
            continue;
        const auto it = was.find(k);
        if (it == was.end()) {
            std::cout << "FAIL: " << path << " has no row for " << TITLES.at(title) << " @ " << fmt_tol(tol) << " / "
                      << name << "\n";
            ++failures;
            continue;
        }
        const std::pair<const char *, std::pair<std::string, std::string>> columns[] = {
            {"f-evals", {std::to_string(r.evals), it->second.first}},
            {"memory", {fmt_memory(r.memory), it->second.second}},
        };
        for (const auto &[column, pair] : columns)
            if (pair.first != pair.second) {
                std::cout << "FAIL: " << TITLES.at(title) << " @ " << fmt_tol(tol) << " / " << name << ": " << column
                          << " is " << pair.first << ", docs say " << pair.second << "\n";
                ++failures;
            }
    }
    for (const auto &[k, v] : was)
        if (!rows.count(k)) {
            std::cout << "FAIL: " << path << " has a row this run did not produce: " << TITLES.at(std::get<0>(k))
                      << " @ " << fmt_tol(std::get<1>(k)) << " / " << std::get<2>(k) << "\n";
            ++failures;
        }
    std::cout << (failures == 0 ? "docs table matches" : std::to_string(failures) + " docs-table mismatch(es)") << "\n";
    return failures == 0 ? 0 : 1;
}

} // namespace

namespace {

auto row(std::size_t evals, std::size_t memory, double err) -> result { return result{evals, memory, 200.0, err}; }

auto write_file(const std::filesystem::path &path, const std::string &text) -> void { std::ofstream(path) << text; }

auto replace_all(std::string text, const std::string &from, const std::string &to) -> std::string {
    for (auto at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size()))
        text.replace(at, from.size(), to);
    return text;
}

// Two positive controls for the memory column.
//
// treeweave accounts for itself, so its memory_usage() and the counted bytes
// must agree to within a factor of two: the gap is vector slack, and a wider one
// would mean the counter is measuring something other than the approximation.
// Boost's cardinal cubic holds one coefficient array, so its counted bytes must
// land just above n * sizeof(double).
auto check_memory_accounting() -> int {
    int failures = 0;
    {
        const auto   before  = counted_bytes();
        auto         fn      = treeweave::fit(zeta_n, 2.0, 10.0, 1e-10);
        const auto   counted = counted_bytes() - before;
        const auto   own     = fn.memory_usage();
        const double ratio = static_cast<double>(std::max(counted, own)) / static_cast<double>(std::min(counted, own));
        std::printf("memory accounting: treeweave memory_usage() %zu B, counted %zu B, ratio %.2f\n", own, counted,
                    ratio);
        if (ratio > 2.0) {
            std::cout << "FAIL: memory accounting: memory_usage() and the counted bytes disagree\n";
            ++failures;
        }
    }
    {
        constexpr std::size_t n = 1024;
        const double          h = 8.0 / static_cast<double>(n - 1);
        std::vector<double>   y(n);
        for (std::size_t i = 0; i < n; ++i)
            y[i] = zeta_n(2.0 + h * static_cast<double>(i));
        const auto before  = counted_bytes();
        auto       spl     = boost::math::interpolators::cardinal_cubic_b_spline<double>(y.data(), n, 2.0, h);
        const auto counted = counted_bytes() - before;
        const auto floor   = n * sizeof(double);
        std::printf("memory accounting: boost cubic at n=%zu counted %zu B, coefficients %zu B\n", n, counted, floor);
        if (counted < floor || counted > floor + 1024) {
            std::cout << "FAIL: memory accounting: the counted bytes are not the coefficient array\n";
            ++failures;
        }
        if (std::isnan(spl(2.0)))
            std::abort();
    }
    return failures == 0 ? 0 : 1;
}

// Positive controls: every gate above must fail on a table it should reject.
auto self_test() -> int {
    std::vector<std::string>       failures;
    const std::vector<std::string> order = {TREEWEAVE, "boost cardinal cubic", "boost cardinal quintic"};

    table good;
    for (double tol : TOLERANCES) {
        good[{POLE, tol, TREEWEAVE}]                = row(100, 1024, tol / 2);
        good[{POLE, tol, "boost cardinal cubic"}]   = row(1000, 8192, tol / 2);
        good[{POLE, tol, "boost cardinal quintic"}] = row(900, 4096, tol / 2);
    }
    if (check(good) != 0)
        failures.emplace_back("a winning table was reported as failing");

    for (const auto &[what, mutated] : std::vector<std::pair<std::string, result>>{
             {"more f-evals than the cubic spline", row(5000, 1024, 1e-11)},
             {"more memory than the cubic spline", row(100, 1u << 20, 1e-11)},
             // Zero memory beats every competitor: only the per-row gate sees it.
             {"treeweave never converged", row(100, 0, 1e-11)},
             {"an achieved error above 10x the tolerance", row(100, 1024, 1e-8)}}) {
        table bad                     = good;
        bad[{POLE, 1e-10, TREEWEAVE}] = mutated;
        if (check(bad) == 0)
            failures.push_back(what + ": not detected");
    }

    const std::string page = as_rst(good, order);
    const auto dir = std::filesystem::temp_directory_path() / ("tw-cmp-" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(dir);
    const auto path = dir / "performance.rst";

    write_file(path, page);
    if (check_docs(good, order, path.string()) != 0)
        failures.emplace_back("the table as emitted did not match itself");

    write_file(path, replace_all(page, "     - 100\n", "     - 4096\n"));
    if (check_docs(good, order, path.string()) == 0)
        failures.emplace_back("an f-eval drift against the docs was not detected");

    write_file(path, replace_all(page, "1.0 KiB", "2.0 KiB"));
    if (check_docs(good, order, path.string()) == 0)
        failures.emplace_back("a memory drift against the docs was not detected");

    write_file(path, "");
    if (check_docs(good, order, path.string()) == 0)
        failures.emplace_back("an empty docs table was accepted");

    // The other sections repeat the labels and the treeweave rows. Their numbers
    // must not be read as this section's.
    const std::string drift = replace_all(page, "     - 100\n", "     - 4096\n");
    write_file(path, SECTIONS[0] + "\n\n" + drift + "\n" + SECTION + "\n\n" + page);
    if (check_docs(good, order, path.string()) != 0)
        failures.emplace_back("the other section's rows were read as this section's");

    write_file(path, SECTIONS[0] + "\n\n" + page + "\n" + SECTION + "\n\n" + drift);
    if (check_docs(good, order, path.string()) == 0)
        failures.emplace_back("a drift in this section's rows was not detected");

    std::filesystem::remove_all(dir);

    if (check_memory_accounting() != 0)
        failures.emplace_back("the memory column is not the approximation's coefficients");

    for (const auto &f : failures)
        std::cout << "FAIL: " << f << "\n";
    std::cout << (failures.empty() ? "self-test passed" : std::to_string(failures.size()) + " self-test case(s) failed")
              << "\n";
    return failures.empty() ? 0 : 1;
}

} // namespace

auto main(int argc, char **argv) -> int {
    // From here on every allocation is counted. Blocks made during static
    // initialization carry counted = 0, so freeing them cannot underflow.
    g_counting = true;
    const std::vector<std::string> args(argv + 1, argv + argc);
    const auto has = [&args](const char *flag) { return std::find(args.begin(), args.end(), flag) != args.end(); };
    if (has("--self-test"))
        return self_test();

    std::mt19937_64          rng(0);
    table                    rows;
    std::vector<std::string> order;
    for (const auto &[name, _] : METHODS)
        order.push_back(name);

    for (const auto &t : TARGETS) {
        const ctx c(t, rng);
        std::printf("\n%s\n  %6s  %-24s %10s %11s %9s %9s\n", t.name.c_str(), "tol", "method", "f-evals", "memory",
                    "Meval/s", "max err");
        for (double tol : TOLERANCES)
            for (const auto &[name, measure] : METHODS) {
                const result r            = measure(c, tol);
                rows[{t.name, tol, name}] = r;
                std::printf("  %6s  %-24s %10zu %11s %9s %9.1e\n", fmt_tol(tol).c_str(), name.c_str(), r.evals,
                            fmt_memory(r.memory).c_str(), std::isnan(r.rate) ? "n/a" : fmt_rate(r.rate, true).c_str(),
                            r.err);
                std::fflush(stdout);
            }
    }
    if (has("--rst"))
        std::cout << "\n" << as_rst(rows, order);
    const int status = std::max(check(rows), check_memory_accounting());
    return has("--check-docs") ? std::max(status, check_docs(rows, order)) : status;
}
