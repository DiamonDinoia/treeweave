// The guru recipe end to end: two fits over the regimes of a function that
// one fit cannot cover (a mild over-articulation of sin(50x)/x — fitted on
// (0, 5) it would need a deep tree; split at 1 it converges as two shallow
// fits plus a wall-power rescale — artificial but structurally faithful).
// Evaluate one shuffled batch with a combined-key counting sort, per-run
// evaluation with the regime's fixup fused, and one gather-back writeback.
//
//   classify every point once -> counting_sort -> for_each_run eval+fixup ->
//   gather.
//
// Compile: same flags as the other C++ examples (see make.inc).

#include <treeweave/guru.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

int main() {
    // Two regimes of the same target: sin(50 x) / x, split at 1.
    const auto body  = [](double x) { return std::sin(50.0 * x) / x; };
    auto       fit_a = treeweave::fit(body, 0.01, 1.0, 1e-8);
    auto       fit_b = treeweave::fit(body, 1.0, 5.0, 1e-8);

    // Key space: fit_a leaves | fit_b leaves | one shared out-of-domain (OOD)
    // bucket. fit_a's sentinel bucket lives at La; unused after the fold.
    const auto          La     = static_cast<std::uint32_t>(fit_a.num_leaves());
    const auto          Lb     = static_cast<std::uint32_t>(fit_b.num_leaves());
    const std::uint32_t base_b = La + 1;
    const std::uint32_t ood    = base_b + Lb;
    const std::uint32_t nbins  = ood + 1;

    // Input: the batch, arbitrary order.
    const std::size_t   n = 2000;
    std::vector<double> xs(n);
    double              s = 0.12345;
    for (auto& x : xs) {
        // deterministic pseudo-random in [0.01, 5) plus a few adversarial edges
        s = std::fmod(16807.0 * s, 4.99 + 0.0) + 0.01;
        x = s;
    }
    xs[0] = 1.0;          // exactly the split: regime b's first point
    xs[1] = 0.005;        // OOD low
    xs[2] = 7.0;          // OOD high
    xs[3] = std::numeric_limits<double>::quiet_NaN();

    // Buffers (caller-owned; a hot path keeps them persistent).
    std::vector<std::uint32_t> keys(n), rank(n), counts(nbins);
    std::vector<double>        packed(n), out_packed(n), out(n);

    // 1. classify into combined keys. The fold rule: compare against the
    //    fit's OWN out_of_domain_id() BEFORE the regime offset.
    for (std::size_t i = 0; i < n; ++i) {
        const double x = xs[i];
        if (x < 1.0) {
            const std::uint32_t id = fit_a.leaf_id(x);
            keys[i] = (id == fit_a.out_of_domain_id()) ? ood : id;
        } else {
            const std::uint32_t id = fit_b.leaf_id(x);
            keys[i] = (id == fit_b.out_of_domain_id()) ? ood : base_b + id;
        }
    }

    // 2. one counting sort.
    treeweave::guru::counting_sort(keys, xs, packed, counts, rank);

    // 3. per-run eval with the regime's work fused (here the regimes are the
    //    fits themselves; a real fusion applies the regime's elementwise
    //    post-processing here, e.g. multiply phase back — see hank105).
    treeweave::guru::for_each_run(counts, [&](std::uint32_t b, std::size_t begin, std::size_t count) {
        if (b == ood) {
            treeweave::guru::fill_out_of_domain(fit_a, out_packed.data() + begin, count);
        } else if (b < base_b) {
            treeweave::guru::eval_leaf_aos(fit_a, b, packed.data() + begin, out_packed.data() + begin, count);
        } else {
            treeweave::guru::eval_leaf_aos(fit_b, b - base_b, packed.data() + begin, out_packed.data() + begin, count);
        }
    });

    // 4. gather back to caller order (prefetch included).
    treeweave::guru::gather(rank, out_packed, out);

    // Cross-check against the library's scalar path (per-regime fit call —
    // the same computation the batch stages stand in for, OOD incl.).
    double worst = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double ref = xs[i] < 1.0 ? fit_a(xs[i]) : fit_b(xs[i]);
        const double got = out[i];
        if (std::isnan(ref) || std::isnan(got)) {
            if (std::isnan(ref) != std::isnan(got)) {
                std::printf("NaN map mismatch at x=%.17g\n", xs[i]);
                return 1;
            }
            continue;
        }
        worst = std::max(worst, std::abs(got - ref) / std::max(1.0, std::abs(ref)));
    }
    std::printf("two-fit guru batch: n=%zu, max rel vs scalar = %.2e\n", n, worst);
    return worst < 1e-10 ? 0 : 1;
}
