// Guru-interface tests: counting-sort blocks (histogram / exclusive_scan /
// scatter / counting_sort / gather), eval_leaf_aos/soa, fill_out_of_domain,
// for_each_run / for_each_sorted_run. Classification ids come straight from
// the public Function members (leaf_ids / leaf_id); the parity targets are
// the public Function paths (batch operator(), sorted).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <random>
#include <span>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <csetjmp>
#include <csignal>
#include <sys/mman.h>
#include <unistd.h>
#endif

#include <catch2/catch_test_macros.hpp>

#include <treeweave/guru.hpp>

namespace {

#if defined(__unix__) || defined(__APPLE__)
// SIGSEGV guard state for the lazy-NaN-scan regression test. File scope:
// a signal handler reads only these. Nan read = the guard page got touched.
volatile sig_atomic_t nan_guard_touched = 0;
sigjmp_buf            nan_guard_env;
// Run records from the callback. Plain globals: a siglongjmp on the fault
// path must not bypass a nontrivial destructor, so the callback writes here
// instead of into a capturing lambda or a vector.
std::pair<std::size_t, std::size_t> nan_guard_runs[4];
std::size_t                         nan_guard_calls = 0;

// Scope-bound cleanup for the mapping and the installed SIGSEGV handler.
// Destructors run on every exit path (return, REQUIRE throw), so the handler
// goes back and the mapping is released even when the test fails. The handler
// is restored only when arm() succeeded; the mapping is released always.
// siglongjmp bypasses them only on the fault path; the check after
// sigsetjmp still runs, and a fault means the test fails anyway.
struct segv_guard {
    double          *map    = nullptr;
    std::size_t      length = 0;
    struct sigaction old{};
    bool             armed = false;
    segv_guard(double *m, std::size_t len) : map(m), length(len) {}
    ~segv_guard() {
        if (armed)
            ::sigaction(SIGSEGV, &old, nullptr);
        ::munmap(map, length);
    }
    bool arm(const struct sigaction &sa) {
        armed = ::sigaction(SIGSEGV, &sa, &old) == 0;
        return armed;
    }
};
#endif

// Easy fit: the root panel converges on its own, min_uniform_depth force-refines
// inside the single subtree -> live leaf table, the fast quantize path is live.
auto make_table_f64() {
    treeweave::options opts;
    opts.max_memory_mib    = 0;
    opts.min_uniform_depth = 6; // 64 uniform leaves, live table, single subtree
    return treeweave::fit<7>([](double x) { return std::exp(x); }, 0.0, 1.0, 1e-12, opts);
}

// Deep fit: a near-singular feature pushes the paneler past the 16-bit leaf-
// table depth cap (probed: single subtree, depth 24, no table), so the
// descent path runs.
auto make_descent_f64() {
    treeweave::options opts;
    opts.max_depth      = 50;
    opts.max_memory_mib = 256;
    return treeweave::fit<8>([](double x) { return 1.0 / (x * x + 1e-13); }, -1.0, 1.0, 1e-6, opts);
}

// 1D in, 4-out: exercises the SoA/LeafND route.
auto make_out4_f64() {
    treeweave::options opts;
    opts.max_memory_mib    = 0;
    opts.min_uniform_depth = 4; // 16 uniform leaves, live table, single subtree
    return treeweave::fit<7>(
        [](std::array<double, 1> x) -> std::array<double, 4> {
            return {std::cos(x[0]), std::sin(x[0]), std::cos(3.0 * x[0]), std::sin(3.0 * x[0])};
        },
        std::array<double, 1>{0.0}, std::array<double, 1>{2.0}, 1e-11, opts);
}

} // namespace

TEST_CASE("1D leaf_id invariants pin the OOD gate (table and descent)", "[guru][classify]") {
    auto table = make_table_f64();
    auto desc  = make_descent_f64();
    REQUIRE(table.has_fast_quantize());
    REQUIRE(!desc.has_fast_quantize());
    REQUIRE(table.out_of_domain_id() == table.num_leaves());

    // Regression pins for the positive-logic gate fix in leaf_id_of: finite
    // OOD, ±Inf and NaN all map to the out_of_domain_id() sentinel in BOTH
    // modes (descent previously let NaN through to a UB linear-bin index).
    const double nanx = std::numeric_limits<double>::quiet_NaN();
    const double inf  = std::numeric_limits<double>::infinity();
    REQUIRE(table.leaf_id(nanx) == table.out_of_domain_id());
    REQUIRE(table.leaf_id(inf) == table.out_of_domain_id());
    REQUIRE(table.leaf_id(-inf) == table.out_of_domain_id());
    REQUIRE(desc.leaf_id(nanx) == desc.out_of_domain_id());
    REQUIRE(desc.leaf_id(inf) == desc.out_of_domain_id());
    REQUIRE(desc.leaf_id(-inf) == desc.out_of_domain_id());
    REQUIRE(desc.leaf_id(-1.001) == desc.out_of_domain_id());
    REQUIRE(desc.leaf_id(1.001) == desc.out_of_domain_id());
    REQUIRE(desc.leaf_id(0.37) < desc.out_of_domain_id()); // in-domain -> real leaf

    // Batch vs scalar parity on the table mode (different code paths).
    std::mt19937                   gen(3);
    std::uniform_real_distribution d(0.0, 1.0);
    std::vector<double>            xs(2048);
    for (auto &x : xs)
        x = d(gen);
    xs.push_back(nanx);
    xs.push_back(2.0);
    xs.push_back(-1.0);
    std::vector<std::uint32_t> ids(xs.size());
    table.leaf_ids(xs.data(), ids.data(), xs.size());
    for (std::size_t i = 0; i < xs.size(); ++i)
        REQUIRE(ids[i] == table.leaf_id(xs[i]));
}

TEST_CASE("guru counting-sort blocks: stable, consistent, invertible", "[guru][sort]") {
    // Hand-checked small case: 8 points, 3 bins.
    const std::vector<std::uint32_t> keys = {2, 0, 2, 1, 2, 0, 1, 2};
    const std::vector<double>        in   = {10, 20, 30, 40, 50, 60, 70, 80};
    std::vector<double>              packed(8);
    std::vector<std::uint32_t>       rank(8), counts(3);

    treeweave::guru::counting_sort(keys, in, packed, counts, rank);

    // Endings: bin0={1,5}, bin1={3,6}, bin2={0,2,4,7}.
    REQUIRE(counts == std::vector<std::uint32_t>{2, 4, 8});
    // Stable within bins: input order preserved.
    REQUIRE(packed == std::vector<double>{20, 60, 40, 70, 10, 30, 50, 80});
    for (std::size_t i = 0; i < 8; ++i)
        REQUIRE(packed[rank[i]] == in[i]);

    // gather inverts the scatter: out == in, bitwise.
    std::vector<double> out(8);
    treeweave::guru::gather(rank, packed, out);
    REQUIRE(out == in);

    // for_each_run reconstructs the packed slices as (id, begin, count).
    std::vector<std::array<std::size_t, 3>> runs;
    treeweave::guru::for_each_run(counts, [&](std::uint32_t b, std::size_t begin, std::size_t count) {
        runs.push_back({b, begin, count});
    });
    REQUIRE(runs.size() == 3);
    REQUIRE(runs[0] == std::array<std::size_t, 3>{0, 0, 2});
    REQUIRE(runs[1] == std::array<std::size_t, 3>{1, 2, 2});
    REQUIRE(runs[2] == std::array<std::size_t, 3>{2, 4, 4});

    // One empty bin: never reported.
    const std::vector<std::uint32_t> ends_with_gap = {2, 2, 5};
    runs.clear();
    treeweave::guru::for_each_run(ends_with_gap, [&](std::uint32_t b, std::size_t begin, std::size_t count) {
        runs.push_back({b, begin, count});
    });
    REQUIRE(runs.size() == 2);
    REQUIRE(runs[0] == std::array<std::size_t, 3>{0, 0, 2});
    REQUIRE(runs[1] == std::array<std::size_t, 3>{2, 2, 3});

    // The split blocks: histogram, then exclusive_scan + scatter explicitly.
    // Same result as counting_sort.
    std::vector<double>        packed2(8);
    std::vector<std::uint32_t> rank2(8), counts2(3, 99);
    treeweave::guru::histogram(keys, counts2); // zeroes the dirty buffer first
    REQUIRE(counts2 == std::vector<std::uint32_t>{2, 2, 4});
    treeweave::guru::exclusive_scan(counts2);
    REQUIRE(counts2 == std::vector<std::uint32_t>{0, 2, 4});
    treeweave::guru::scatter(keys, in, packed2, counts2, rank2);
    REQUIRE(counts2 == counts);
    REQUIRE(packed2 == packed);
    REQUIRE(rank2 == rank);

    // histogram vs a hand loop on a random key set.
    std::mt19937                            gen(41);
    std::uniform_int_distribution<std::uint32_t> dk(0, 16);
    std::vector<std::uint32_t>              rkeys(4096);
    for (auto &k : rkeys)
        k = dk(gen);
    std::vector<std::uint32_t> hist(17), hand(17, 0);
    treeweave::guru::histogram(rkeys, hist);
    for (const std::uint32_t k : rkeys)
        ++hand[k];
    REQUIRE(hist == hand);

    // gather vs a plain loop on the same random set (crosses the prefetch
    // lookahead boundary), plus the empty case.
    std::vector<std::uint32_t> rrank(rkeys.size());
    std::vector<double>        rvals(rkeys.size()), rpacked(rkeys.size()), rout(rkeys.size()), rref(rkeys.size());
    for (std::size_t i = 0; i < rvals.size(); ++i)
        rvals[i] = static_cast<double>(i);
    treeweave::guru::counting_sort(rkeys, rvals, rpacked, hist, rrank);
    treeweave::guru::gather(rrank, rpacked, rout);
    for (std::size_t i = 0; i < rref.size(); ++i)
        rref[i] = rpacked[rrank[i]];
    REQUIRE(rout == rref);
    REQUIRE(rout == rvals); // gather inverts the sort's placement
    treeweave::guru::gather(std::span<const std::uint32_t>{}, std::span<const double>{}, std::span<double>{});

    // n == 0: every block is a no-op on the input, and counts still zero.
    std::vector<std::uint32_t> c0 = {7, 7, 7};
    treeweave::guru::counting_sort(std::span<const std::uint32_t>{}, std::span<const double>{}, std::span<double>{},
                                   c0, std::span<std::uint32_t>{});
    REQUIRE(c0 == std::vector<std::uint32_t>{0, 0, 0});
    treeweave::guru::for_each_run(c0, [](std::uint32_t, std::size_t, std::size_t) { FAIL(); });
}

TEST_CASE("guru fill_out_of_domain writes the sentinel NaN pattern", "[guru][ood]") {
    auto f1 = make_table_f64(); // 1-out
    auto f4 = make_out4_f64();  // 4-out

    // AoS, scalar output: n NaNs, the guard element untouched.
    std::vector<double> a(5, 1.5);
    treeweave::guru::fill_out_of_domain(f1, a.data(), 4);
    for (std::size_t i = 0; i < 4; ++i)
        REQUIRE(std::isnan(a[i]));
    REQUIRE(a[4] == 1.5);

    // AoS, 4-out: n * output_dim NaNs.
    std::vector<double> b(3 * 4 + 1, 2.5);
    treeweave::guru::fill_out_of_domain(f4, b.data(), 3);
    for (std::size_t i = 0; i < 12; ++i)
        REQUIRE(std::isnan(b[i]));
    REQUIRE(b[12] == 2.5);

    // SoA, 4-out: n NaNs per component buffer, guards untouched.
    std::vector<double>     s(4 * 3, 3.5);
    std::array<double *, 4> soa{s.data() + 0, s.data() + 3, s.data() + 6, s.data() + 9};
    treeweave::guru::fill_out_of_domain(f4, soa, 2);
    for (std::size_t d = 0; d < 4; ++d) {
        REQUIRE(std::isnan(soa[d][0]));
        REQUIRE(std::isnan(soa[d][1]));
        REQUIRE(soa[d][2] == 3.5);
    }
}

TEST_CASE("guru eval_leaf parity with the public unsorted batch", "[guru][eval]") {
    auto                f     = make_table_f64();
    const std::uint32_t nbins = f.out_of_domain_id() + 1;

    std::mt19937                   gen(7);
    std::uniform_real_distribution d(0.0, 1.0);
    std::vector<double>            xs(20000);
    for (auto &x : xs)
        x = d(gen);
    // OOD points: they must NaN-fill through rank like the public batch does.
    xs.push_back(-0.5);
    xs.push_back(1.5);
    xs.push_back(std::numeric_limits<double>::quiet_NaN());
    xs.push_back(std::numeric_limits<double>::infinity());

    // The guru recipe: classify -> sort -> per-run eval_leaf_aos -> gather.
    const std::size_t          n = xs.size();
    std::vector<std::uint32_t> keys(n), rank(n), counts(nbins);
    std::vector<double>        packed(n), out_packed(n), out_guru(n);
    f.leaf_ids(xs.data(), keys.data(), n);
    treeweave::guru::counting_sort(keys, xs, packed, counts, rank);
    treeweave::guru::for_each_run(counts, [&](std::uint32_t b, std::size_t begin, std::size_t count) {
        if (b == f.out_of_domain_id()) { // no coefficients; mirror the public NaN fill.
            treeweave::guru::fill_out_of_domain(f, out_packed.data() + begin, count);
            return;
        }
        treeweave::guru::eval_leaf_aos(f, b, packed.data() + begin, out_packed.data() + begin, count);
    });
    treeweave::guru::gather(rank, out_packed, out_guru);

    // Reference: the public unsorted batch. Same kernels => bitwise equal.
    std::vector<double> out_ref(n);
    f(xs.data(), out_ref.data(), n);
    for (std::size_t i = 0; i < n; ++i) {
        if (std::isnan(out_ref[i]))
            REQUIRE(std::isnan(out_guru[i]));
        else
            REQUIRE(out_guru[i] == out_ref[i]);
    }

    // Positive control: evaluating the wrong leaf on one run must break parity,
    // proving the check above can actually fail.
    std::vector<double> out_bad(n);
    treeweave::guru::for_each_run(counts, [&](std::uint32_t b, std::size_t begin, std::size_t count) {
        if (b == f.out_of_domain_id())
            return; // sentinel: keep NaN; no coefficients to misuse
        const std::uint32_t leaf =
            (begin == 0) ? static_cast<std::uint32_t>((b + 1) % f.num_leaves()) : b; // break only the first run
        treeweave::guru::eval_leaf_aos(f, leaf, packed.data() + begin, out_bad.data() + begin, count);
    });
    // OOD rows would count unconditionally (0.0 in out_bad vs NaN in out_ref);
    // only in-domain rows prove the wrong-leaf substitution itself fires.
    std::size_t n_diff = 0;
    for (std::size_t i = 0; i < n; ++i)
        if (!std::isnan(out_ref[i]))
            n_diff += out_bad[rank[i]] != out_ref[i];
    REQUIRE(n_diff > 0);
}

TEST_CASE("guru eval_leaf_soa parity on a 1D->4D fit", "[guru][soa]") {
    auto                f     = make_out4_f64();
    const std::uint32_t nbins = f.out_of_domain_id() + 1;

    std::mt19937                   gen(11);
    std::uniform_real_distribution d(0.0, 2.0);
    std::vector<double>            xs(4000);
    for (auto &x : xs)
        x = d(gen);
    xs.push_back(2.5); // OOD: the sentinel branch below must execute, not just exist
    xs.push_back(std::numeric_limits<double>::quiet_NaN());
    const std::size_t n = xs.size();

    std::vector<std::uint32_t> keys(n), rank(n), counts(nbins);
    f.leaf_ids(xs.data(), keys.data(), n);
    std::vector<double> packed(n);
    std::vector<double> out_packed(4 * n); // four packed component spans of length n
    std::vector<double> out_ref(4 * n);
    treeweave::guru::counting_sort(keys, xs, packed, counts, rank);
    std::array<double *, 4> out_soa{out_packed.data() + 0 * n, out_packed.data() + 1 * n, out_packed.data() + 2 * n,
                                    out_packed.data() + 3 * n};
    std::array<double *, 4> ref_soa{out_ref.data() + 0 * n, out_ref.data() + 1 * n, out_ref.data() + 2 * n,
                                    out_ref.data() + 3 * n};
    treeweave::guru::for_each_run(counts, [&](std::uint32_t b, std::size_t begin, std::size_t count) {
        std::array<double *, 4> sp{};
        for (std::size_t d_c = 0; d_c < 4; ++d_c)
            sp[d_c] = out_soa[d_c] + begin;
        if (b == f.out_of_domain_id()) {
            treeweave::guru::fill_out_of_domain(f, sp, count);
            return;
        }
        treeweave::guru::eval_leaf_soa(f, b, packed.data() + begin, sp, count);
    });

    f(xs.data(), ref_soa, n); // public SoA batch
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t d_c = 0; d_c < 4; ++d_c) {
            if (std::isnan(ref_soa[d_c][i]))
                REQUIRE(std::isnan(out_soa[d_c][rank[i]]));
            else
                REQUIRE(out_soa[d_c][rank[i]] == ref_soa[d_c][i]);
        }
}

TEST_CASE("guru for_each_sorted_run parity with Function::sorted", "[guru][sorted]") {
    auto f  = make_descent_f64();
    auto ft = make_table_f64();

    const auto fill_or_eval_aos = [](const auto &ff, std::uint32_t id, const double *xref, std::size_t begin,
                                     std::size_t count, double *dst) {
        if (id == ff.out_of_domain_id()) {
            treeweave::guru::fill_out_of_domain(ff, dst + begin, count);
            return;
        }
        treeweave::guru::eval_leaf_aos(ff, id, xref + begin, dst + begin, count);
    };

    {
        std::mt19937                   gen(13);
        std::uniform_real_distribution d(-1.0, 1.0);
        std::vector<double>            xs(9000);
        for (auto &x : xs)
            x = d(gen);
        // Adversarial: OOD prefix, OOD suffix.
        xs.insert(xs.begin(), {-2.5, -1.0 - 1e-9});
        xs.push_back(1.5);
        std::sort(xs.begin(), xs.end());
        const std::size_t n = xs.size();

        std::vector<double> out_guru(n), out_ref(n);
        treeweave::guru::for_each_sorted_run(f, xs.data(), n,
                                             [&](std::uint32_t id, std::size_t begin, std::size_t count) {
                                                 fill_or_eval_aos(f, id, xs.data(), begin, count, out_guru.data());
                                             });
        f.sorted(xs.data(), out_ref.data(), n);
        for (std::size_t i = 0; i < n; ++i) {
            if (std::isnan(out_ref[i]))
                REQUIRE(std::isnan(out_guru[i]));
            else
                REQUIRE(out_guru[i] == out_ref[i]);
        }
    }

    {
        // Table-mode sorted walk on the easy fit.
        std::mt19937                   gen(13);
        std::uniform_real_distribution dt(0.0, 1.0);
        std::vector<double>            xt(3000);
        for (auto &x : xt)
            x = dt(gen);
        std::sort(xt.begin(), xt.end());
        xt.insert(xt.begin(), -0.25); // OOD prefix
        xt.push_back(1.25);           // OOD suffix
        std::vector<double> og(xt.size()), oref(xt.size());
        treeweave::guru::for_each_sorted_run(ft, xt.data(), xt.size(),
                                             [&](std::uint32_t id, std::size_t begin, std::size_t count) {
                                                 fill_or_eval_aos(ft, id, xt.data(), begin, count, og.data());
                                             });
        ft.sorted(xt.data(), oref.data(), xt.size());
        for (std::size_t i = 0; i < xt.size(); ++i) {
            if (std::isnan(oref[i]))
                REQUIRE(std::isnan(og[i]));
            else
                REQUIRE(og[i] == oref[i]);
        }
    }
}

TEST_CASE("guru for_each_sorted_run: interior NaN and run reaching n", "[guru][sorted][edge]") {
    auto f  = make_descent_f64();
    auto ft = make_table_f64();

    auto fill_or_eval_aos = [](const auto &ff, std::uint32_t id, const double *xref, std::size_t begin,
                               std::size_t count, double *dst) {
        if (id == ff.out_of_domain_id()) {
            treeweave::guru::fill_out_of_domain(ff, dst + begin, count);
            return;
        }
        treeweave::guru::eval_leaf_aos(ff, id, xref + begin, dst + begin, count);
    };

    // No OOD suffix: the last in-domain run ends exactly at n; the gallop
    // probes k over the array end and must clamp at n. An interior NaN breaks
    // a run mid-array.
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    auto       run = [&](const auto &ff) {
        // Sort only the finite values; sorting NaN is undefined behaviour
        // (NaN breaks the strict weak ordering std::sort requires). Insert
        // the NaN at its interior slot afterwards.
        std::vector<double> xs = {-0.9, -0.7, -0.5, -0.3, 0.4, 0.6, 0.8, 0.95};
        // Extend the last run with 15 exact duplicates of 0.95 so the tail
        // is one leaf run of length 16; a galloping probe overshoots past n
        // and a bad clamp truncates that tail.
        for (int r = 1; r < 16; ++r)
            xs.push_back(0.95);
        std::sort(xs.begin(), xs.end());
        // Insert the NaN at slot 4 without vector::insert: gcc 13
        // -Wnull-dereference misfires on the insert relocation path.
        xs.push_back(0.95);
        std::copy_backward(xs.begin() + 4, xs.end() - 1, xs.end());
        xs[4] = nan;
        std::vector<double> out_guru(xs.size()), out_ref(xs.size());
        std::uint32_t       prev_id   = ff.out_of_domain_id();
        std::size_t         prev_last = 0;
        std::size_t         n_runs    = 0;
        treeweave::guru::for_each_sorted_run(ff, xs.data(), xs.size(),
                                             [&](std::uint32_t id, std::size_t begin, std::size_t count) {
                                                 // Runs are disjoint, ascending, and merged: no
                                                 // two adjacent runs share one real leaf id
                                                 // (ood_id CAN repeat: the OOD-prefix run can
                                                 // abut an interior-NaN sentinel run).
                                                 REQUIRE(begin == prev_last);
                                                 if (n_runs > 0 && id != ff.out_of_domain_id())
                                                     REQUIRE(id != prev_id);
                                                 prev_id   = id;
                                                 prev_last = begin + count;
                                                 ++n_runs;
                                                 fill_or_eval_aos(ff, id, xs.data(), begin, count, out_guru.data());
                                             });
        REQUIRE(prev_last == xs.size());
        ff.sorted(xs.data(), out_ref.data(), xs.size());
        for (std::size_t i = 0; i < xs.size(); ++i) {
            if (std::isnan(out_ref[i]))
                REQUIRE(std::isnan(out_guru[i]));
            else
                REQUIRE(out_guru[i] == out_ref[i]);
        }
    };
    run(f);
    run(ft);

    // Cutoff boundary: runs of length exactly K and K+1 (K = small_run_cutoff)
    // must come out whole, on both sides of the scan-to-gallop switch.
    auto run_len = [&](const auto &ff, std::size_t len) {
        std::vector<double> xs(len, 0.5); // one leaf run: exact duplicates
        std::size_t         n_runs = 0, first_begin = 0, first_count = 0;
        treeweave::guru::for_each_sorted_run(ff, xs.data(), xs.size(),
                                             [&](std::uint32_t, std::size_t begin, std::size_t count) {
                                                 if (n_runs == 0) {
                                                     first_begin = begin;
                                                     first_count = count;
                                                 }
                                                 ++n_runs;
                                             });
        REQUIRE(n_runs == 1);
        REQUIRE(first_begin == 0);
        REQUIRE(first_count == len);
    };
    constexpr std::size_t K = treeweave::detail::small_run_cutoff;
    for (std::size_t len : {K, K + 1}) {
        run_len(f, len);
        run_len(ft, len);
    }

    // Same leaf on both sides of an interior NaN: L equal points, a NaN, one
    // more equal point. The run must break at the NaN for every L, so a
    // gallop probe that jumps over the NaN at any distance is caught.
    auto nan_between = [&](const auto &ff) {
        for (std::size_t len = 1; len <= 40; ++len) {
            std::vector<double> xs(len, 0.9);
            xs.push_back(nan);
            xs.push_back(0.9);
            std::vector<std::size_t> counts;
            treeweave::guru::for_each_sorted_run(
                ff, xs.data(), xs.size(),
                [&](std::uint32_t, std::size_t, std::size_t count) { counts.push_back(count); });
            REQUIRE(counts == std::vector<std::size_t>{len, 1, 1});
        }
    };
    nan_between(f);
    nan_between(ft);

    // Exercise the gallop body: one NaN-free batch, a run of `len` equal
    // points of one leaf followed by 10 points of another leaf. Sweeping len
    // (past small_run_cutoff, up to 1000) drives many gallop/bisection paths,
    // so an off-by-one in the bracket shows up for some len.
    // Reference: a hand-written linear walk over the public
    // sorted_leaf_id_at, not Function::sorted, which shares this walker.
    auto gallop_body = [&](const auto &ff, std::size_t len) {
        std::vector<double> xs(len, 0.25);
        for (int r = 0; r < 10; ++r)
            xs.push_back(0.75);
        const auto [lower, upper] = ff.get_bounds();
        const auto lo             = lower[0];
        const auto hi             = upper[0];
        const auto ood_id         = ff.out_of_domain_id();
        const auto &subs          = ff.get_subtrees();
        const bool fast           = subs.size() == 1 && subs.front().has_leaf_table();
        using Run = std::tuple<std::uint32_t, std::size_t, std::size_t>;
        std::vector<Run> want;
        std::size_t      w = 0;
        while (w < xs.size() && xs[w] < lo)
            ++w;
        if (w > 0)
            want.emplace_back(ood_id, 0, w);
        while (w < xs.size()) {
            if (xs[w] > hi) {
                want.emplace_back(ood_id, w, xs.size() - w);
                break;
            }
            const std::uint32_t wid = ff.sorted_leaf_id_at(xs.data(), w, ood_id, fast);
            if (wid == ood_id) {
                want.emplace_back(ood_id, w, 1);
                ++w;
                continue;
            }
            std::size_t v = w + 1;
            while (v < xs.size() && ff.sorted_leaf_id_at(xs.data(), v, ood_id, fast) == wid)
                ++v;
            want.emplace_back(wid, w, v - w);
            w = v;
        }
        std::vector<Run> got;
        treeweave::guru::for_each_sorted_run(ff, xs.data(), xs.size(),
                                             [&](std::uint32_t id, std::size_t begin, std::size_t count) {
                                                 got.emplace_back(id, begin, count);
                                             });
        REQUIRE(got == want);
        // Hand-check the shape: exactly two runs, len + 10.
        REQUIRE(got.size() == 2);
        REQUIRE(std::get<1>(got[0]) == 0);
        REQUIRE(std::get<2>(got[0]) == len);
        REQUIRE(std::get<1>(got[1]) == len);
        REQUIRE(std::get<2>(got[1]) == 10);
    };
    for (std::size_t len = K + 1; len <= 70; ++len) {
        gallop_body(f, len);
        gallop_body(ft, len);
    }
    gallop_body(f, 1000);
    gallop_body(ft, 1000);

    // Lazy NaN scan: a long run, then an out-of-domain suffix that ends in a
    // NaN. The NaN sits after the suffix start, so main's walk gives the run
    // and one suffix run covering the NaN. A huge all-out-of-domain batch
    // gives one callback.
    auto nan_after_suffix = [&](const auto &ff) {
        std::vector<double> xs(40, 0.25);
        xs.insert(xs.end(), 20, 2.0);
        xs.push_back(nan);
        std::vector<std::pair<std::size_t, std::size_t>> runs;
        treeweave::guru::for_each_sorted_run(
            ff, xs.data(), xs.size(),
            [&](std::uint32_t, std::size_t begin, std::size_t count) { runs.emplace_back(begin, count); });
        REQUIRE(runs == std::vector<std::pair<std::size_t, std::size_t>>{{0, 40}, {40, 21}});

        std::vector<double> big(1000000, 2.0);
        std::size_t         calls = 0;
        treeweave::guru::for_each_sorted_run(ff, big.data(), big.size(),
                                             [&](std::uint32_t, std::size_t, std::size_t) { ++calls; });
        REQUIRE(calls == 1);
    };
    nan_after_suffix(f);
    nan_after_suffix(ft);

#if defined(__unix__) || defined(__APPLE__)
    // Inspected-points regression: the lazy NaN scan must stop at the first
    // xs[p] > hi. 3 equal in-domain points, then 10^6 points above hi, all
    // in one page-aligned mapping. Pages after page 0 (indices >= 512) get
    // PROT_NONE: the fixed walk reads indices 0..3 only, so it completes;
    // a scan that reads the suffix faults, the handler longjmps out, and the
    // test fails. A leaf-id call counter cannot see the raw xs reads, and
    // there is no read-count hook, so this SIGSEGV guard is the proxy.
    SECTION("lazy NaN scan stops at the out-of-domain suffix start") {
        auto guard_body = [&](const auto &ff) {
            constexpr std::size_t n     = 1000000;
            const std::size_t     psz   = static_cast<std::size_t>(::sysconf(_SC_PAGESIZE));
            const std::size_t     bytes = n * sizeof(double);
            const std::size_t     mlen  = (bytes + psz - 1) / psz * psz;
            double               *xs    = static_cast<double *>(
                ::mmap(nullptr, mlen, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
            REQUIRE(xs != MAP_FAILED);
            // Owns the mapping from here on; restores the handler when armed.
            segv_guard cleanup(xs, mlen);
            xs[0] = xs[1] = xs[2] = 0.5;
            std::fill_n(xs + 3, n - 3, 2.0); // > hi for both fixtures
            REQUIRE(::mprotect(reinterpret_cast<char *>(xs) + psz, mlen - psz, PROT_NONE) == 0);

            struct sigaction sa{};
            sa.sa_handler = [](int) {
                nan_guard_touched = 1;
                ::siglongjmp(nan_guard_env, 1);
            };
            sigemptyset(&sa.sa_mask); // Apple SDK macro; no :: prefix
            sa.sa_flags = SA_NODEFER;
            REQUIRE(cleanup.arm(sa));

            nan_guard_calls = 0;
            if (::sigsetjmp(nan_guard_env, 1) == 0) {
                treeweave::guru::for_each_sorted_run(ff, xs, n,
                                                     [](std::uint32_t, std::size_t begin, std::size_t count) {
                                                         if (nan_guard_calls < 4)
                                                             nan_guard_runs[nan_guard_calls] = {begin, count};
                                                         ++nan_guard_calls;
                                                     });
            }
            REQUIRE(nan_guard_touched == 0);
            REQUIRE(nan_guard_calls == 2);
            REQUIRE(nan_guard_runs[0] == std::pair<std::size_t, std::size_t>{0, 3});
            REQUIRE(nan_guard_runs[1] == std::pair<std::size_t, std::size_t>{3, n - 3});
        };
        nan_guard_touched = 0;
        guard_body(f);
        guard_body(ft);
    }
#endif
}

TEST_CASE("guru for_each_sorted_run on a tuple-input fit", "[guru][sorted][soa]") {
    auto f4 = make_out4_f64();

    std::mt19937                   gen(29);
    std::uniform_real_distribution d(0.0, 2.0);
    const std::size_t              n = 3000;
    std::vector<double>            xs(n);
    for (auto &x : xs)
        x = d(gen);
    std::sort(xs.begin(), xs.end());
    xs.push_back(2.25); // OOD suffix

    std::vector<double>     os(4 * xs.size()), rs(4 * xs.size());
    std::array<double *, 4> out_soa{os.data() + 0 * xs.size(), os.data() + 1 * xs.size(), os.data() + 2 * xs.size(),
                                    os.data() + 3 * xs.size()};
    treeweave::guru::for_each_sorted_run(
        f4, xs.data(), xs.size(), [&](std::uint32_t id, std::size_t begin, std::size_t count) {
            std::array<double *, 4> sp{out_soa[0] + begin, out_soa[1] + begin, out_soa[2] + begin, out_soa[3] + begin};
            if (id == f4.out_of_domain_id()) {
                treeweave::guru::fill_out_of_domain(f4, sp, count);
                return;
            }
            treeweave::guru::eval_leaf_soa(f4, id, xs.data() + begin, sp, count);
        });
    std::array<double *, 4> ref_soa{rs.data() + 0 * xs.size(), rs.data() + 1 * xs.size(), rs.data() + 2 * xs.size(),
                                    rs.data() + 3 * xs.size()};
    f4.sorted(xs.data(), ref_soa, xs.size());
    for (std::size_t i = 0; i < xs.size(); ++i)
        for (std::size_t dq = 0; dq < 4; ++dq) {
            if (std::isnan(ref_soa[dq][i]))
                REQUIRE(std::isnan(out_soa[dq][i]));
            else
                REQUIRE(out_soa[dq][i] == ref_soa[dq][i]);
        }
}

TEST_CASE("guru blocks at tile-crossing sizes, f32 classify batch", "[guru][scale]") {
    auto f = make_table_f64();

    std::mt19937                   gen(17);
    std::uniform_real_distribution d(0.0, 1.0);
    std::vector<double> xs(decltype(f)::kDefaultTileK + 913); // crosses the public tile cap
    for (auto &x : xs)
        x = d(gen);
    xs.push_back(-0.5); // OOD: the sentinel branch below must execute, not just exist
    xs.push_back(std::numeric_limits<double>::quiet_NaN());
    const std::size_t n = xs.size();

    std::vector<std::uint32_t> keys(n), rank(n), counts(f.out_of_domain_id() + 1);
    std::vector<double>        packed(n), out_packed(n), out_guru(n), out_ref(n);
    f.leaf_ids(xs.data(), keys.data(), n);
    treeweave::guru::counting_sort(keys, xs, packed, counts, rank);
    treeweave::guru::for_each_run(counts, [&](std::uint32_t b, std::size_t begin, std::size_t count) {
        if (b == f.out_of_domain_id()) {
            treeweave::guru::fill_out_of_domain(f, out_packed.data() + begin, count);
            return;
        }
        treeweave::guru::eval_leaf_aos(f, b, packed.data() + begin, out_packed.data() + begin, count);
    });
    treeweave::guru::gather(rank, out_packed, out_guru);
    f(xs.data(), out_ref.data(), n);
    for (std::size_t i = 0; i < n; ++i) {
        if (std::isnan(out_ref[i]))
            REQUIRE(std::isnan(out_guru[i]));
        else
            REQUIRE(out_guru[i] == out_ref[i]);
    }

    // f32: classify batch parity + a runnable eval_leaf_aos parity.
    treeweave::options opts;
    opts.max_memory_mib    = 0;
    opts.min_uniform_depth = 6;
    auto f32 = treeweave::fit<7>([](float x) { return std::exp(x); }, 0.0f, 1.0f, /*tol=*/1e-5, opts);
    REQUIRE(f32.has_fast_quantize());
    std::mt19937                          gen32(19);
    std::uniform_real_distribution<float> d32(0.0f, 1.0f);
    std::vector<float>                    xf(5000);
    for (auto &x : xf)
        x = d32(gen32);
    xf.push_back(std::numeric_limits<float>::quiet_NaN());
    xf.push_back(2.0f);
    std::vector<std::uint32_t> k32(xf.size());
    f32.leaf_ids(xf.data(), k32.data(), xf.size());
    for (std::size_t i = 0; i < xf.size(); ++i)
        REQUIRE(k32[i] == f32.leaf_id(xf[i]));

    {
        std::mt19937                          genf(31);
        std::uniform_real_distribution<float> df(0.0f, 1.0f);
        std::vector<float>                    xsf(2000);
        for (auto &x : xsf)
            x = df(genf);
        xsf.push_back(2.0f); // OOD: exercises the sentinel branch below
        const std::size_t nf = xsf.size();
        std::vector<std::uint32_t> kf(nf), rankf(nf), cf(f32.out_of_domain_id() + 1);
        std::vector<float>         pkf(nf), ofld(nf), outf(nf), reff(nf);
        f32.leaf_ids(xsf.data(), kf.data(), nf);
        treeweave::guru::counting_sort(kf, xsf, pkf, cf, rankf);
        treeweave::guru::for_each_run(cf, [&](std::uint32_t b, std::size_t begin, std::size_t count) {
            if (b == f32.out_of_domain_id()) {
                treeweave::guru::fill_out_of_domain(f32, ofld.data() + begin, count);
                return;
            }
            treeweave::guru::eval_leaf_aos(f32, b, pkf.data() + begin, ofld.data() + begin, count);
        });
        treeweave::guru::gather(rankf, ofld, outf);
        f32(xsf.data(), reff.data(), nf);
        for (std::size_t q = 0; q < nf; ++q) {
            if (std::isnan(reff[q]))
                REQUIRE(std::isnan(outf[q]));
            else
                REQUIRE(outf[q] == reff[q]);
        }
    }
}

// Combined-key counting sort across TWO fits — the multi-regime composition the
// guru interface exists for: one classify sweep, one sort, fused per-run work,
// one gather back.
TEST_CASE("guru two-fit combined key matches per-fit reference", "[guru][combined]") {
    auto f1 = make_table_f64(); // exp(x) on [0,1]
    auto f2 = make_out4_f64();  // cos/sin on [0,2], 4 outputs
    const auto          L1    = static_cast<std::uint32_t>(f1.num_leaves());
    const auto          L2    = static_cast<std::uint32_t>(f2.num_leaves());
    const std::uint32_t base2 = L1 + 1;
    const std::uint32_t ood   = base2 + L2; // shared OOD bucket
    const std::uint32_t nbins = ood + 1;    // f1 ids | f2 ids | OOD

    std::mt19937                   gen(23);
    std::uniform_real_distribution d(0.0, 1.0);
    std::vector<double>            xs(5000);
    for (auto &x : xs)
        x = d(gen);
    // OOD injections: even-indexed points route to f1, so -0.5 tests f1's fold;
    // odd-indexed route to f2, so 2.5 tests f2's. Exercises the shared bucket.
    xs[0] = -0.5;
    xs[1] = 2.5;
    xs[2] = std::numeric_limits<double>::quiet_NaN();
    xs[3] = std::numeric_limits<double>::infinity();
    const std::size_t n = xs.size();

    // Route by index parity. Per the documented fold rule, each fit's OOD
    // sentinel is compared against that fit's own out_of_domain_id() BEFORE
    // the regime offset — never against an offset number.
    std::vector<std::uint32_t> keys(n), rank(n), counts(nbins);
    for (std::size_t i = 0; i < n; ++i) {
        if (i % 2) {
            const std::uint32_t id = f2.leaf_id(std::array<double, 1>{xs[i]});
            keys[i]                = (id == f2.out_of_domain_id()) ? ood : base2 + id;
        } else {
            const std::uint32_t id = f1.leaf_id(xs[i]);
            keys[i]                = (id == f1.out_of_domain_id()) ? ood : id;
        }
    }
    std::vector<double> packed(n);
    treeweave::guru::counting_sort(keys, xs, packed, counts, rank);
    // Post-scatter, counts[b] is bucket b's one-past-end (Reinecke), so the bucket
    // count is the difference — and ood is the last bucket (counts[ood] == n).
    REQUIRE(counts[ood] - counts[ood - 1] == 4); // the four injected OOD points

    std::vector<double> out_packed(n);
    treeweave::guru::for_each_run(counts, [&](std::uint32_t b, std::size_t begin, std::size_t count) {
        if (b == ood) {
            treeweave::guru::fill_out_of_domain(f1, out_packed.data() + begin, count);
        } else if (b < base2) {
            treeweave::guru::eval_leaf_aos(f1, b, packed.data() + begin, out_packed.data() + begin, count);
        } else {
            // Component 0 (cos) of the 4-out fit, fused from its AoS run.
            std::vector<double> tmp(4 * count);
            treeweave::guru::eval_leaf_aos(f2, b - base2, packed.data() + begin, tmp.data(), count);
            for (std::size_t q = 0; q < count; ++q)
                out_packed[begin + q] = tmp[4 * q];
        }
    });

    std::vector<double> out(n);
    treeweave::guru::gather(rank, out_packed, out);
    for (std::size_t i = 0; i < n; ++i) {
        if (keys[i] == ood) {
            REQUIRE(std::isnan(out[i]));
            continue;
        }
        // f2's contract tol was 1e-11: the analytic check must not be tighter.
        const double want = (i % 2) ? std::cos(xs[i]) : std::exp(xs[i]);
        REQUIRE(std::abs(out[i] - want) < ((i % 2) ? 1e-11 : 1e-12));
    }
}

namespace {

// Lane-for-lane parity of LaneQuantizer with the scalar leaf_id on a whole
// batch: fold the in-domain mask onto the ids exactly as a caller would.
template <class F>
auto check_lane_parity(const F &f, std::vector<typename F::value_type> xs) -> void {
    using T    = typename F::value_type;
    using Q    = treeweave::guru::LaneQuantizer<T>;
    using B    = typename Q::batch_type;
    using I    = typename Q::id_batch_type;
    using id_t = typename Q::id_type;
    REQUIRE(Q::supports(f));
    const Q        q(f);
    const auto     ood = static_cast<id_t>(f.out_of_domain_id());
    constexpr auto W   = B::size;
    xs.resize((xs.size() + W - 1) / W * W, T{0.5}); // pad to whole batches
    for (std::size_t i = 0; i < xs.size(); i += W) {
        const B x   = B::load_unaligned(xs.data() + i);
        const I ids = xsimd::select(xsimd::batch_bool_cast<id_t>(q.in_domain(x)), q.ids(x), I(ood));
        for (std::size_t l = 0; l < W; ++l) {
            const auto want = f.leaf_id(typename F::input_type{xs[i + l]});
            REQUIRE(ids.get(l) == static_cast<id_t>(want));
        }
    }
}

template <class T>
auto lane_probe_points(std::mt19937 &gen, T lo, T hi) -> std::vector<T> {
    std::uniform_real_distribution<T> d(lo - (hi - lo) / 4, hi + (hi - lo) / 4); // 1/4 of the mass lands OOD
    std::vector<T>                    xs(4096);
    for (auto &x : xs)
        x = d(gen);
    const T adversarial[] = {lo,
                             hi,
                             std::nextafter(lo, hi),
                             std::nextafter(hi, lo),
                             std::nextafter(lo, lo - 1),
                             std::nextafter(hi, hi + 1),
                             T{0},
                             -T{0},
                             std::numeric_limits<T>::quiet_NaN(),
                             std::numeric_limits<T>::infinity(),
                             -std::numeric_limits<T>::infinity(),
                             std::numeric_limits<T>::max(),
                             std::numeric_limits<T>::denorm_min()};
    for (T v : adversarial)
        xs.push_back(v); // not insert(): gcc 11 -Wstringop-overflow false positive
    return xs;
}

} // namespace

TEST_CASE("guru LaneQuantizer lanes match leaf_id (f64 scalar, f64 1D->4D, f32)", "[guru][classify][lanes]") {
    std::mt19937 gen(29);
    {
        auto f = make_table_f64();
        check_lane_parity(f, lane_probe_points<double>(gen, 0.0, 1.0));
    }
    {
        auto f = make_out4_f64();
        check_lane_parity(f, lane_probe_points<double>(gen, 0.0, 2.0));
    }
    {
        treeweave::options opts;
        opts.max_memory_mib    = 0;
        opts.min_uniform_depth = 5;
        auto f                 = treeweave::fit<6>([](float x) { return std::exp(x); }, -1.0f, 1.0f, 1e-5, opts);
        check_lane_parity(f, lane_probe_points<float>(gen, -1.0f, 1.0f));
    }
    // The descent-mode fit has no leaf table: the precondition must say so.
    REQUIRE(!treeweave::guru::LaneQuantizer<double>::supports(make_descent_f64()));
}
