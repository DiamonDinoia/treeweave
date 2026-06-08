// Leaf-id quantize parity tests.
//
// The batch evaluator bins each point to a leaf id via a vectorized quantize
// (`PolyTree::for_each_leaf_id_batch`), exposed here as `Function::leaf_ids`.
// Two invariants must hold for every input, and are the gate for the Phase 1
// int32 quantize rework (`bench/binsort_phase0.md`):
//
//   1. PARITY (bit-exact, all inputs): the vectorized `leaf_ids` stream equals
//      the scalar `leaf_id` oracle point-for-point — in-domain, on exact cell
//      boundaries, out-of-domain on both sides, and for NaN / +-Inf. This is
//      what guards the SIMD truncate (int64 today, int32 for f32 after Phase 1)
//      against the scalar quantize: same formula, same OOD-wrap classification.
//
//   2. DESCENT (at cell centers): the quantize-then-table-lookup id equals the
//      tree-descent id (`find_node(x).poly_eval_id()`). Cell centers are the
//      points the leaf table was built from, so this verifies the quantize
//      addresses the table correctly. (Bit-exact descent parity is NOT claimed
//      at exact cell boundaries — `PolyTree::get_node_index` recomputes mids by
//      a different chain than the table build, see polytree.hpp:388.)
//
// Covered for double and float, 1D and 2D.

#include <cstddef>
#include <treeweave/treeweave.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <random>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

template <class T>
void check_1d() {
    constexpr int      depth = 8; // -> 256 uniform leaves, leaf table live
    treeweave::options opts;
    opts.max_memory_mib    = 0;
    opts.min_uniform_depth = depth;
    auto fn = treeweave::fit<3>([](T x) { return T{1} / (T{1} + T{25} * x * x); }, T{-1}, T{1}, /*tol=*/1e-4, opts);
    REQUIRE(fn.has_fast_quantize());
    const auto n_leaves = static_cast<std::uint32_t>(fn.num_leaves());
    const T    lo       = T{-1};
    const T    hi       = T{1};
    const T    span     = hi - lo;
    const T    cell     = span / static_cast<T>(std::size_t{1} << depth);

    std::vector<T> xs;
    // Random interior points.
    std::mt19937                           gen(20240603);
    std::uniform_real_distribution<double> d(-1.0 + 1e-6, 1.0 - 1e-6);
    xs.reserve(20000);
    for (int i = 0; i < 20000; ++i)
        xs.push_back(static_cast<T>(d(gen)));
    // Exact cell boundaries (every 7th cell to keep it quick) and cell centers.
    for (std::size_t k = 0; k <= (std::size_t{1} << depth); k += 7)
        xs.push_back(lo + static_cast<T>(k) * cell);
    for (std::size_t k = 0; k < (std::size_t{1} << depth); ++k)
        xs.push_back(lo + (static_cast<T>(k) + T{0.5}) * cell);
    // Domain edges and out-of-domain on both sides.
    xs.push_back(lo);
    xs.push_back(hi);                     // closed upper endpoint: clamps to the last leaf
    xs.push_back(std::nextafter(hi, lo)); // just inside the top
    xs.push_back(lo - T{0.5});
    xs.push_back(hi + T{0.5}); // finite high-OOD: clamps to the last leaf too
    xs.push_back(lo - static_cast<T>(1e30));
    xs.push_back(hi + static_cast<T>(1e30));
    // Non-finite.
    xs.push_back(std::numeric_limits<T>::quiet_NaN());
    xs.push_back(std::numeric_limits<T>::infinity());
    xs.push_back(-std::numeric_limits<T>::infinity());

    const std::size_t          n = xs.size();
    std::vector<std::uint32_t> vec(n);
    fn.leaf_ids(xs.data(), vec.data(), n);

    for (std::size_t i = 0; i < n; ++i) {
        // (1) Vectorized batch id == scalar oracle id, bit-exact.
        const std::uint32_t scalar = fn.leaf_id(xs[i]);
        REQUIRE(vec[i] == scalar);
        // All ids are a valid leaf or the OOD sentinel.
        REQUIRE(vec[i] <= n_leaves);
    }

    // (2) Cell centers: quantize id == descent id, by construction.
    for (std::size_t k = 0; k < (std::size_t{1} << depth); ++k) {
        const T xc = lo + (static_cast<T>(k) + T{0.5}) * cell;
        REQUIRE(fn.leaf_id(xc) == fn.find_node(xc).poly_eval_id());
    }
}

template <class T>
void check_2d() {
    using P = std::array<T, 2>;
    treeweave::options opts;
    opts.max_memory_mib    = 0;
    opts.min_uniform_depth = 4; // 2D depth 4 -> 8 bits -> 256-entry table
    auto fn                = treeweave::fit<3>(
        [](P x) -> std::array<T, 1> {
            return {std::exp(-T{4} * ((x[0] - T{0.5}) * (x[0] - T{0.5}) + (x[1] - T{0.5}) * (x[1] - T{0.5})))};
        },
        P{T{0}, T{0}}, P{T{1}, T{1}}, /*tol=*/1e-3, opts);

    const auto                             n_leaves = static_cast<std::uint32_t>(fn.num_leaves());
    std::mt19937                           gen(99);
    std::uniform_real_distribution<double> d(1e-4, 1.0 - 1e-4);

    std::vector<T> flat; // AoS: x0,y0,x1,y1,...
    std::vector<P> pts;
    for (int i = 0; i < 8000; ++i) {
        P p{static_cast<T>(d(gen)), static_cast<T>(d(gen))};
        pts.push_back(p);
        flat.push_back(p[0]);
        flat.push_back(p[1]);
    }
    // Some OOD / non-finite corners.
    for (P p :
         std::vector<P>{{T{-1}, T{0.5}}, {T{0.5}, T{2}}, {T{1}, T{1}}, {std::numeric_limits<T>::quiet_NaN(), T{0.5}}}) {
        pts.push_back(p);
        flat.push_back(p[0]);
        flat.push_back(p[1]);
    }

    const std::size_t          n = pts.size();
    std::vector<std::uint32_t> vec(n);
    fn.leaf_ids(flat.data(), vec.data(), n);
    for (std::size_t i = 0; i < n; ++i) {
        REQUIRE(vec[i] == fn.leaf_id(pts[i]));
        REQUIRE(vec[i] <= n_leaves);
    }
}

// Large-n_leaves counting-sort path. With many leaves (here 2^13 = 8192) the
// flat counting sort's counts[] histogram and the random xp_packed scatter
// stress the caches; this guards the sort's distinct failure modes — wrong
// counts[] boundaries, a bad perm_inv, or a dropped/duplicated point — each of
// which routes a point's slot to a *different* point's value (a large, O(1)
// error) or to garbage/NaN. The quantize itself is already pinned by the parity
// tests above.
//
// The batch quantize and the scalar `operator()` classify identically over the
// region this draws from — in-domain, OOD-low, and NaN/-Inf — so the check is
// direct: scalar `single = fn(xs[i])` is the oracle and we assert
//   * isnan(batch[i]) == isnan(single)            (same OOD/NaN classification)
//   * finite: |single - batch[i]| <= tol          (same leaf, Horner rounding)
// The draw is deliberately capped at the upper bound `hi` (no *finite* high-OOD):
// with the closed upper endpoint the batch quantize clamps finite x > hi to the
// last leaf (returns a value) while scalar `operator()` keeps its inclusive
// `<=` guard (returns NaN), so the two paths diverge there by design. That
// finite-high-OOD clamp parity is pinned by the quantize-parity test
// (`check_1d`, which compares the two quantize paths, not operator()); here we
// keep the elegant operator() oracle by staying out of that one region. NaN and
// -Inf still agree (both classify OOD -> NaN); +Inf is not injected because its
// float->int convert is arch-dependent under the clamp (see the body comment).
template <class T>
void check_large_leaves() {
    constexpr int      depth = 13; // 2^13 = 8192 leaves
    treeweave::options opts;
    opts.max_memory_mib    = 0;
    opts.min_uniform_depth = depth;
    auto fn = treeweave::fit<3>([](T x) { return T{1} / (T{1} + T{25} * x * x); }, T{-1}, T{1}, /*tol=*/1e-4, opts);
    REQUIRE(fn.has_fast_quantize());
    REQUIRE(fn.num_leaves() > 4096); // large leaf count

    constexpr std::size_t                  N = 200000;
    std::mt19937                           gen(424242);
    std::uniform_real_distribution<double> d(-1.3, 1.0); // ~13% land OOD-low
    std::vector<T>                         xs(N);
    for (auto &x : xs)
        x = static_cast<T>(d(gen));
    // NaN and -Inf classify OOD -> NaN on every target (NaN propagates through
    // the leaf polynomial; -Inf's float->int convert lands at INT_MIN, below the
    // clamp, so it still wraps). +Inf is deliberately NOT injected: its
    // float->int convert is arch-dependent (x86 -> INT_MIN -> OOD/NaN; ARM
    // saturates to INT_MAX, which the closed-endpoint clamp routes to the last
    // leaf -> a finite extrapolation), an accepted high-side divergence that the
    // scalar operator() oracle does not share.
    xs[7]     = std::numeric_limits<T>::quiet_NaN();
    xs[N - 3] = -std::numeric_limits<T>::infinity();

    std::vector<T> batch(N);
    fn(xs.data(), batch.data(), N);

    const T     tol   = std::is_same_v<T, double> ? T(1e-9) : T(2e-4);
    std::size_t n_ood = 0, n_in = 0;
    for (std::size_t i = 0; i < N; ++i) {
        const T single = fn(xs[i]); // NaN-safe scalar oracle
        // Same OOD/NaN classification (OOD-low + NaN/-Inf): the batch quantize
        // wraps OOD exactly where the scalar domain guard does.
        REQUIRE(std::isnan(batch[i]) == std::isnan(single));
        if (std::isnan(batch[i])) {
            ++n_ood;
            continue;
        }
        // In-domain: same leaf, so agree to SIMD-vs-scalar Horner rounding. A
        // counts/perm sort bug routes a slot to a different point's value (a
        // large error) or to garbage, which this catches.
        REQUIRE(std::abs(single - batch[i]) <= tol * (T{1} + std::abs(single)));
        ++n_in;
    }
    // Sanity on the in/out split (points are ~77% in-domain): a wholesale drop
    // or misroute would skew these hard.
    REQUIRE(n_in > (N * 7) / 10);
    REQUIRE(n_ood > N / 10);
}

// Large-n_leaves on the SoA tile body: 1D-in / 2D-out routes through
// eval_batch_soa -> partition_into_leaves (input_dim == 1, leaf table live),
// then the SoA per-leaf kernel. Covers the sort on the second tile layout (the
// scalar-output check_large_leaves above exercises the AoS body).
template <class T>
void check_large_leaves_soa() {
    constexpr int      depth = 13; // 2^13 = 8192 leaves
    treeweave::options opts;
    opts.max_memory_mib    = 0;
    opts.min_uniform_depth = depth;
    auto fn                = treeweave::fit<3>(
        [](std::array<T, 1> x) -> std::array<T, 2> { return {std::sin(T{3} * x[0]), T{1} / (T{1} + x[0] * x[0])}; },
        std::array<T, 1>{T{-1}}, std::array<T, 1>{T{1}}, /*tol=*/1e-4, opts);
    REQUIRE(fn.has_fast_quantize());
    REQUIRE(fn.num_leaves() > 4096); // large leaf count

    // Upper bound capped at `hi`: see check_large_leaves for why finite high-OOD
    // is excluded (closed-endpoint clamp vs. operator()'s inclusive guard diverge).
    constexpr std::size_t                  N = 200000;
    std::mt19937                           gen(515151);
    std::uniform_real_distribution<double> d(-1.3, 1.0); // ~13% land OOD-low
    std::vector<T>                         xs(N);
    for (auto &x : xs)
        x = static_cast<T>(d(gen));
    // +Inf omitted: arch-dependent under the closed-endpoint clamp (see
    // check_large_leaves). NaN and -Inf classify OOD -> NaN on every target.
    xs[5]     = std::numeric_limits<T>::quiet_NaN();
    xs[N - 2] = -std::numeric_limits<T>::infinity();

    std::vector<T>           o0(N), o1(N);
    std::array<T *, 2> const soa{o0.data(), o1.data()};
    fn(xs.data(), soa, N);

    const T     tol   = std::is_same_v<T, double> ? T(1e-9) : T(2e-4);
    std::size_t n_ood = 0, n_in = 0;
    for (std::size_t i = 0; i < N; ++i) {
        const std::array<T, 2> single = fn(std::array<T, 1>{xs[i]});
        // OOD/NaN classification agrees on every output component (both the
        // batch OOD bucket and the scalar OOD return NaN-fill all components).
        REQUIRE(std::isnan(o0[i]) == std::isnan(single[0]));
        REQUIRE(std::isnan(o1[i]) == std::isnan(single[1]));
        if (std::isnan(o0[i])) {
            ++n_ood;
            continue;
        }
        REQUIRE(std::abs(single[0] - o0[i]) <= tol * (T{1} + std::abs(single[0])));
        REQUIRE(std::abs(single[1] - o1[i]) <= tol * (T{1} + std::abs(single[1])));
        ++n_in;
    }
    REQUIRE(n_in > (N * 7) / 10);
    REQUIRE(n_ood > N / 10);
}

// Scalar operator() NaN/±Inf regression (was a latent OOB read: the old
// `xd < lo || xd >= hi` guard is false for NaN, letting it reach
// get_linear_bin(NaN) -> INT64_MIN index -> out-of-bounds subtrees_ read).
// The positive-logic guard classifies NaN/±Inf as out-of-domain -> NaN out.
template <class T>
void check_scalar_nan() {
    treeweave::options opts;
    opts.max_memory_mib    = 0;
    opts.min_uniform_depth = 8;
    auto fn = treeweave::fit<3>([](T x) { return T{1} / (T{1} + T{25} * x * x); }, T{-1}, T{1}, /*tol=*/1e-4, opts);
    REQUIRE(std::isnan(fn(std::numeric_limits<T>::quiet_NaN())));
    REQUIRE(std::isnan(fn(std::numeric_limits<T>::infinity())));
    REQUIRE(std::isnan(fn(-std::numeric_limits<T>::infinity())));
    // OOD-low -> NaN; finite high-OOD -> NaN via operator()'s inclusive guard;
    // in-domain interior is finite.
    REQUIRE(std::isnan(fn(T{-2})));
    REQUIRE(std::isnan(fn(T{2})));
    REQUIRE(std::isfinite(fn(T{0})));
    // Closed upper endpoint: x == hi returns the boundary value, not NaN.
    REQUIRE(std::isfinite(fn(T{1})));
}

} // namespace

TEST_CASE("Counting sort matches scalar at large n_leaves", "[treeweave][quantize][largeleaves]") {
    check_large_leaves<double>();
    check_large_leaves<float>();
    check_large_leaves_soa<double>();
    check_large_leaves_soa<float>();
}

TEST_CASE("Scalar operator() classifies NaN/Inf/OOD as out-of-domain", "[treeweave][quantize][nan]") {
    check_scalar_nan<double>();
    check_scalar_nan<float>();
}

TEST_CASE("Quantize parity: vectorized leaf_ids == scalar oracle (1D)", "[treeweave][quantize][parity]") {
    check_1d<double>();
    check_1d<float>();
}

TEST_CASE("Quantize parity: vectorized leaf_ids == scalar oracle (2D)", "[treeweave][quantize][parity][2d]") {
    check_2d<double>();
    check_2d<float>();
}
