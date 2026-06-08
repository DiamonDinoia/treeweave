#ifndef TREEWEAVE_DETAIL_FUNCTION_HPP
#define TREEWEAVE_DETAIL_FUNCTION_HPP

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <numeric>
#include <queue>
#include <type_traits>
#include <utility>
#include <vector>

#include <poet/poet.hpp>
#include <polyfit/polyfit.hpp>

#include <treeweave/detail/compiler_macros.hpp>
#include <treeweave/detail/errors.hpp>
#include <treeweave/detail/eval_policy.hpp>
#include <treeweave/detail/node.hpp>
#include <treeweave/detail/numerics.hpp>
#include <treeweave/detail/polytree.hpp>
#include <treeweave/detail/tol_kind.hpp>
#include <treeweave/detail/value.hpp>

// Benchmark-only: the Phase 0 bin-sort harness (examples/c++/treeweave_bench_binsort.cpp)
// defines TREEWEAVE_BENCH_PARTITION_HOOK to compile in `bench_partition_phases`,
// which times the quantize / histogram / scatter sub-phases separately via the
// x86 cycle counter (__rdtsc) — so it is inherently x86-only. Honour the request
// only on x86; TREEWEAVE_PARTITION_HOOK gates both the intrinsics include and the
// method below. Never defined by the library, tests, or other examples.
#if defined(TREEWEAVE_BENCH_PARTITION_HOOK) &&                                                                         \
    (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86))
#define TREEWEAVE_PARTITION_HOOK 1
#if defined(_MSC_VER)
#include <intrin.h> // MSVC: __rdtsc is in <intrin.h>
#else
#include <x86intrin.h> // GCC/Clang: __rdtsc via x86 intrinsics
#endif
#endif

namespace treeweave {

/// Adaptive piecewise-polynomial approximation of a user function. The fit
/// is materialized at construction (via `treeweave::fit`) into a flat array of
/// subtrees over a uniform top-level grid, each subtree built BFS to the
/// requested tolerance. After construction the object is immutable and its
/// evaluators are thread-safe — see header docstring on `treeweave/treeweave.hpp`.
template <std::size_t Degree, class Func, EvalPolicy Policy = EvalPolicy::Balanced>
class Function {
  public:
    using input_type  = std::remove_cvref_t<poly_eval::fitInput_t<Func>>;
    using output_type = poly_eval::fitOutput_t<Func>;
    using value_type  = poly_eval::detail::value_type_or_t<input_type>;
    // polyfit's 1D `FuncEval` Horner/FMA path assumes a scalar `OutputType`;
    // a scalar-input + array-output spelling hits an opaque template error
    // inside polyfit. Surface a directed message at the treeweave boundary so
    // users see the supported workaround without spelunking through polyfit.
    static_assert(poly_eval::detail::hasTupleSize_v<input_type> || !poly_eval::detail::hasTupleSize_v<output_type>,
                  "treeweave: 1D scalar-input + array-output fits are not "
                  "supported; spell the input as std::array<T, 1> to route "
                  "the vector-valued fit through polyfit's FuncEvalND.");
    using poly_eval_type         = detail::poly_eval_type_for<Func, Degree, Policy>;
    static constexpr auto degree = Degree;
    static constexpr auto policy = Policy;

    static constexpr std::size_t input_dim  = detail::value_dim_v<input_type>;
    static constexpr std::size_t output_dim = detail::value_dim_v<output_type>;
    static constexpr std::size_t n_child    = std::size_t{1} << input_dim;

    using node_t      = detail::Node<Func, Degree, Policy>;
    using box_t       = detail::Box<value_type, input_dim>;
    using dim_array_t = detail::Value<value_type, input_dim>;

    /// Approximate resident bytes — including subtree node arrays and the
    /// shared polyfit coefficient store. Useful for budget validation in
    /// callers that build many Functions.
    [[nodiscard]] auto memory_usage() const -> std::size_t {
        std::size_t mem = sizeof(*this);
        mem += polyfits_.capacity() * sizeof(poly_eval_type);
        for (const auto &subtree : subtrees_)
            mem += subtree.memory_usage();
        return mem;
    }

    /// Number of leaf panels — one Horner coefficient set per leaf, indexed
    /// by the leaf id the bin sort assigns. Equals the counting-sort bin count.
    [[nodiscard]] auto num_leaves() const noexcept -> std::size_t { return polyfits_.size(); }

    /// True when every subtree carries a live quantize-to-leaf table, i.e. the
    /// batch path takes the single-quantize fast bin sort rather than per-point
    /// tree descent.
    [[nodiscard]] auto has_fast_quantize() const noexcept -> bool {
        if (subtrees_.empty())
            return false;
        return std::ranges::all_of(subtrees_, [](const auto &st) -> bool { return st.has_leaf_table(); });
    }

    /// Leaf id the bin sort assigns to `x`: the index into the panel store
    /// (`< num_leaves()`) whose polynomial evaluates `x`, or the out-of-domain
    /// sentinel `num_leaves()`. Resolves through the leaf-table fast path when
    /// live, else tree descent. This is the scalar twin of one `leaf_ids` lane
    /// and shares its quantize/OOD-wrap semantics exactly — including flagging
    /// NaN/±Inf as OOD (the leaf-table wrap test catches them, where the bare
    /// `operator()(x)` domain pre-check would let a NaN through to a panel and
    /// evaluate to NaN). It is the parity oracle the quantize tests assert the
    /// vectorized `leaf_ids` stream against.
    [[nodiscard]] TREEWEAVE_ALWAYS_INLINE auto leaf_id(const input_type &x) const -> std::uint32_t {
        const auto                                 ood_id = static_cast<std::uint32_t>(polyfits_.size());
        const bool                                 table  = subtrees_.size() == 1 && subtrees_.front().has_leaf_table();
        const detail::Value<value_type, input_dim> xi(x);
        return leaf_id_of(xi, ood_id, table);
    }

    /// Batch leaf-id assignment: write each of the `n` points' `leaf_id` into
    /// `out`. Streams through the same vectorized quantize the batch evaluator
    /// uses (1D leaf-table fast path -> `for_each_leaf_id_batch`; otherwise the
    /// per-point resolve), so this is the binning stage of `operator()(xp,res,n)`
    /// exposed on its own. `xp` is AoS (`input_dim` coords per point).
    auto leaf_ids(const value_type *xp, std::uint32_t *out, std::size_t n) const -> void {
        const auto ood_id = static_cast<std::uint32_t>(polyfits_.size());
        const bool table  = subtrees_.size() == 1 && subtrees_.front().has_leaf_table();
        if constexpr (input_dim == 1) {
            if (table) {
                subtrees_.front().for_each_leaf_id_batch(xp, ood_id, n,
                                                         [&](std::size_t i, std::uint32_t id) -> void { out[i] = id; });
                return;
            }
        }
        for (std::size_t i = 0; i < n; ++i) {
            const detail::Value<value_type, input_dim> xi(xp + (input_dim * i));
            out[i] = leaf_id_of(xi, ood_id, table);
        }
    }

    /// Print a one-screen summary of the fit (node/leaf counts, depth, fit-
    /// time eval count, wall time, memory).
    auto print_stats() const -> void {
        std::size_t       n_nodes        = 0;
        std::size_t       n_leaves       = 0;
        const std::size_t n_subtrees     = subtrees_.size();
        std::size_t       max_tree_depth = 0;
        const std::size_t mem            = memory_usage();
        for (const auto &subtree : subtrees_) {
            n_nodes += subtree.size();
            max_tree_depth = std::max(max_tree_depth, subtree.max_depth());
            for (const auto &node : subtree.get_nodes())
                n_leaves += static_cast<std::size_t>(node.is_leaf());
        }

        std::cout << "Treeweave function mapping " << input_dim << " to " << output_dim << "\n";
        std::cout << "Tree represented by " << n_nodes << " nodes, of which " << n_leaves << " are leaves\n";
        std::cout << "Nodes are distributed across " << n_subtrees << " subtrees at an initial depth of "
                  << stats_.base_depth << " with a maximum subtree depth of " << max_tree_depth << "\n";
        std::cout << "Total function evaluations required for fit: "
                  << n_nodes * detail::powi<static_cast<int>(input_dim)>(Degree) + stats_.n_evals_root << "\n";
        std::cout << "Total time to create tree: " << stats_.t_elapsed << " milliseconds\n";
        std::cout << "Approximate memory usage of tree: " << static_cast<double>(mem) / (1024.0 * 1024.0) << " MiB\n";

        // Leaf-table fast path status. When every subtree has a live
        // table, `eval_batch_tile`'s scatter loop quantizes points
        // straight to leaf ids without touching the descent code. Off
        // means the eval path falls back to per-axis tree descent.
        std::size_t lt_entries = 0;
        bool        lt_all     = !subtrees_.empty();
        for (const auto &subtree : subtrees_) {
            if (subtree.has_leaf_table())
                lt_entries += subtree.leaf_table_size();
            else
                lt_all = false;
        }
        if (lt_all) {
            const std::size_t lt_bytes = lt_entries * sizeof(std::uint32_t);
            std::cout << "Leaf table: live (" << lt_entries << " entries, " << (static_cast<double>(lt_bytes) / 1024.0)
                      << " KiB)\n";
        } else {
            std::cout << "Leaf table: descent-only\n";
        }
    }

    /// Build a Function object by recursively fitting the domain.
    /// @throws MaxDepthExceeded   if any subtree fails to converge at
    ///                            `input.max_depth` (and
    ///                            `allow_max_depth_leaves == false`).
    /// @throws MemoryBudgetExceeded  if accumulated leaf storage crosses
    ///                            `input.max_memory_mib` MiB.
    Function(const detail::TreeInput &input, const input_type center, const input_type half_width_in, const Func &func)
        : input_(input), box_(dim_array_t{center}, dim_array_t{half_width_in}),
          tol_(static_cast<value_type>(input.tol)) {
        const auto t_start = std::chrono::steady_clock::now();

        // Surface a coarse memory-cost guard when the user opts into a
        // deep uniform grid. `2^(K*D) * sizeof(uint32_t)` is the per-
        // subtree leaf-table budget *if* the table is built; we warn
        // when this crosses 1 MiB so a stray `min_uniform_depth = 20`
        // doesn't silently allocate gigabytes. (PolyTree caps the table
        // at 64 K entries / 256 KiB anyway — past that the table is
        // skipped, but the tree itself still has 2^(K*D) leaves.)
        if (input.min_uniform_depth > 0) {
            const std::size_t bits =
                static_cast<std::size_t>(input_dim) * static_cast<std::size_t>(input.min_uniform_depth);
            if (bits < std::numeric_limits<std::size_t>::digits) {
                const std::size_t leaves = std::size_t{1} << bits;
                const std::size_t bytes  = leaves * sizeof(std::uint32_t);
                if (bytes > (std::size_t{1} << 20)) // 1 MiB
                    std::cerr << "treeweave: warning: min_uniform_depth=" << input.min_uniform_depth << " in "
                              << input_dim << "D forces " << leaves << " uniform leaves (" << (bytes >> 20)
                              << " MiB of leaf-table memory if "
                              << "input_dim*depth <= 16; tree storage is "
                              << "additional). Tighten `tol` or lower "
                              << "`min_uniform_depth` if this was unintended.\n";
            }
        }

        dim_array_t       lvec{half_width_in};
        std::queue<box_t> q;

        const auto hlmin = *std::min_element(lvec.begin(), lvec.end());
        for (std::size_t i = 0; i < input_dim; ++i)
            n_subtrees_[i] = static_cast<std::size_t>(lvec[i] / hlmin);

        q.push(box_t(center, lvec));

        // Half-width of next children
        dim_array_t half_width = lvec * value_type{0.5};

        // Breadth-first search through the tree, testing each level; we exit
        // as soon as a level is not entirely parent nodes, so we can jump
        // straight to the subtree roots on evaluation.
        while (!q.empty()) {
            const std::size_t n_next = q.size();

            auto add_node_children_to_queue = [](std::queue<box_t> &theq, const dim_array_t &parent_center,
                                                 const dim_array_t &child_hw) -> void {
                for (std::size_t child = 0; child < n_child; ++child) {
                    detail::Value<value_type, input_dim> offset_center;

                    // Extract sign of each offset component from the bits of child.
                    for (std::size_t j = 0; j < input_dim; ++j) {
                        const std::array<value_type, 2> signed_hw{-child_hw[j], child_hw[j]};
                        offset_center[j] = parent_center[j] + signed_hw[(child >> j) & std::size_t{1}];
                    }

                    theq.push(box_t(offset_center, child_hw));
                }
            };

            std::vector<node_t> nodes;
            for (std::size_t i = 0; i < n_next; ++i) {
                box_t const current_box = q.front();
                q.pop();

                nodes.emplace_back();
                auto                       &node = nodes.back();
                std::vector<poly_eval_type> dummy;
                node.fit(input, func, current_box.center, current_box.half_length, {}, dummy);
                if (node.poly_eval_id() != 0u)
                    node.set_poly_eval_id(0);

                if (!node.is_leaf())
                    add_node_children_to_queue(q, current_box.center, half_width);
            }
            stats_.n_evals_root +=
                static_cast<std::uint64_t>(nodes.size() * detail::powi<static_cast<int>(input_dim)>(Degree));

            half_width                      = half_width * value_type{0.5};
            const std::size_t expected_full = std::size_t{1} << (input_dim * (stats_.base_depth + 1));
            if (expected_full == q.size()) {
                n_subtrees_ = n_subtrees_ * std::size_t{2};
                ++stats_.base_depth;
                if (stats_.base_depth > static_cast<std::size_t>(input.max_depth)) {
                    const auto &offender = q.front();
                    throw MaxDepthExceeded(stats_.base_depth, offender.center.as_array(),
                                           offender.half_length.as_array());
                }
            } else {
                break;
            }
        }

        dim_array_t bin_size;
        for (std::size_t j = 0; j < input_dim; ++j) {
            bin_size[j]      = value_type{2} * box_.half_length[j] / static_cast<value_type>(n_subtrees_[j]);
            inv_bin_size_[j] = value_type{0.5} * static_cast<value_type>(n_subtrees_[j]) / box_.half_length[j];
        }
        lower_left_  = box_.center - box_.half_length;
        upper_right_ = box_.center + box_.half_length;

        subtrees_.reserve(n_subtrees_.prod());

        auto input_local = input;
        input_local.max_depth -= static_cast<int>(stats_.base_depth);
        const std::size_t total_bins = n_subtrees_.prod();
        for (std::size_t i_bin = 0; i_bin < total_bins; ++i_bin) {
            const std::array<std::size_t, input_dim> bins = get_bins(i_bin);

            dim_array_t parent_center;
            for (std::size_t i = 0; i < input_dim; ++i)
                parent_center[i] = (static_cast<value_type>(bins[i]) + value_type{0.5}) * bin_size[i] + lower_left_[i];

            box_t const subtree_root = {parent_center, bin_size * value_type{0.5}};
            subtrees_.emplace_back(input_local, subtree_root, polyfits_, func);
        }

#ifndef NDEBUG
        // The Function box must equal the union of subtree boxes — both
        // layers carry the same domain (Function for the OOD pre-check,
        // subtrees as the descent invariant). If this fires, the bin
        // decomposition has drifted from the Function's stored bounds.
        {
            dim_array_t lo_min = subtrees_.front().lower();
            dim_array_t hi_max = subtrees_.front().upper();
            for (const auto &st : subtrees_) {
                for (std::size_t d = 0; d < input_dim; ++d) {
                    lo_min[d] = std::min(lo_min[d], st.lower()[d]);
                    hi_max[d] = std::max(hi_max[d], st.upper()[d]);
                }
            }
            for (std::size_t d = 0; d < input_dim; ++d) {
                const value_type span = upper_right_[d] - lower_left_[d];
                const value_type tol  = std::max<value_type>(1, std::abs(lower_left_[d]) + std::abs(upper_right_[d])) *
                                        std::numeric_limits<value_type>::epsilon() * 16;
                assert(std::abs(lo_min[d] - lower_left_[d]) <= tol);
                assert(std::abs(hi_max[d] - upper_right_[d]) <= tol);
                (void)span;
            }
        }
#endif

        // Aggregate any per-subtree non-converged panels. Default behaviour
        // prints them to cerr and throws; opt-in keeps the list on the
        // Function for `non_converged_panels()` introspection.
        for (const auto &subtree : subtrees_)
            for (const auto &p : subtree.non_converged_panels())
                non_converged_panels_.push_back(p);

        if (!non_converged_panels_.empty()) {
            if (!input.allow_max_depth_leaves) {
                std::cerr << "Treeweave fit warning: " << non_converged_panels_.size() << " panel"
                          << (non_converged_panels_.size() == 1 ? "" : "s")
                          << " failed to converge at max_depth=" << input.max_depth << ":\n";
                for (const auto &p : non_converged_panels_) {
                    std::cerr << "  [";
                    for (std::size_t k = 0; k < p.a.size(); ++k)
                        std::cerr << (k ? " x " : "") << "[" << p.a[k] << ", " << p.b[k] << ")";
                    std::cerr << "]\n";
                }
                throw MaxDepthExceeded(non_converged_panels_);
            }
        }

        const auto t_end = std::chrono::steady_clock::now();
        stats_.t_elapsed =
            static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start).count());
    }

    /// Convert linear bin index to [dim] bin vector.
    [[nodiscard]] auto get_bins(std::size_t i_bin) const -> std::array<std::size_t, input_dim> {
        std::array<std::size_t, input_dim> out{};
        poet::static_for<input_dim - 1>([&](auto D) -> void {
            constexpr std::size_t d = D;
            out[d]                  = i_bin % n_subtrees_[d];
            i_bin /= n_subtrees_[d];
        });
        out[input_dim - 1] = i_bin;
        return out;
    }

    /// Find linear index of bin at a point.
    [[nodiscard]] TREEWEAVE_ALWAYS_INLINE auto get_linear_bin(const input_type &x) const -> std::size_t {
        auto axis_bin = [&](auto I) -> std::size_t {
            constexpr std::size_t i  = I;
            const value_type      xi = [&]() -> value_type {
                if constexpr (poly_eval::detail::hasTupleSize_v<input_type>)
                    return x[i];
                else
                    return x;
            }();
            // Clamp closes the upper endpoint: `x == upper_right_[i]` quantizes
            // to `n_subtrees_[i]` (one past the last subtree); `min` folds it
            // back to the last subtree so its leaf extrapolates the boundary
            // value. Callers gate the OOD-low side before reaching here, so the
            // clamp is one-sided. Branchless.
            const auto bin = static_cast<std::size_t>((xi - lower_left_[i]) * inv_bin_size_[i]);
            return std::min(bin, n_subtrees_[i] - 1);
        };
        if constexpr (input_dim == 1) {
            return axis_bin(std::integral_constant<std::ptrdiff_t, 0>{});
        } else {
            std::size_t result =
                axis_bin(std::integral_constant<std::ptrdiff_t, static_cast<std::ptrdiff_t>(input_dim) - 1>{});
            // Horner-form: result accumulates from highest axis down.
            poet::static_for<static_cast<std::ptrdiff_t>(input_dim) - 1>([&](auto K) -> void {
                constexpr std::ptrdiff_t r = static_cast<std::ptrdiff_t>(input_dim) - 2 - K;
                result = result * n_subtrees_[r] + axis_bin(std::integral_constant<std::ptrdiff_t, r>{});
            });
            return result;
        }
    }

    [[nodiscard]] auto find_node(const input_type &x) const -> const node_t & {
        return subtrees_[get_linear_bin(x)].find_node(x);
    }

    /// Default per-call tile cap for the batch path. The adaptive floor
    /// (`n_leaves * kMinPtsPerLeaf`) raises this when low-leaf-count
    /// Functions would otherwise starve the polyfit batch kernel.
    static constexpr std::size_t kDefaultTileK = 65536;

  private:
    /// Internal scratch for the unsorted batch path. Constructed
    /// stack-local inside each batch call (the public API does not
    /// expose this type) and parametrised on the caller's allocator so
    /// arena / pool / pinned-memory allocators reuse storage without
    /// treeweave having to hold any state across calls.
    ///
    /// Leaf ids are not materialised — they are recomputed during
    /// scatter from the same SIMD quantize that drove the histogram
    /// (FINUFFT bin-sort recon, spread.hpp:421). One quantize per W
    /// points is cheaper than the u16/u32 read/write stream the
    /// materialised `leaf_ids[]` array would push through L1d.
    ///
    /// `counts` doubles as the histogram, the exclusive-scan output,
    /// and the scatter cursor — after scatter, `counts[k]` is the
    /// one-past-end of leaf k's packed slice (Reinecke's trick),
    /// removing the need for a separate `offsets` array.
    template <class Allocator = std::allocator<value_type>>
    class Scratch {
        using ATraits = std::allocator_traits<Allocator>;
        template <class U>
        using rebind_t = ATraits::template rebind_alloc<U>;
        template <class U>
        using rebind_traits = std::allocator_traits<rebind_t<U>>;

        /// Allocator-aware uninitialised buffer. Mirrors
        /// `std::make_unique_for_overwrite` semantics (no value-init on
        /// allocation) while routing alloc/dealloc through the
        /// caller-supplied allocator. Trivial types only — never used
        /// to hold non-trivial T.
        template <class T>
        class Buf {
          public:
            Buf() = default;
            explicit Buf(const Allocator &a) : alloc_(rebind_t<T>(a)) {}
            ~Buf() { reset(); }

            Buf(const Buf &)                     = delete;
            auto operator=(const Buf &) -> Buf & = delete;
            // Owns a raw allocation and is only ever used as a pinned Scratch
            // member (Scratch is thread_local, never moved), so moves are deleted
            // too rather than implemented.
            Buf(Buf &&)                     = delete;
            auto operator=(Buf &&) -> Buf & = delete;

            void ensure_size(std::size_t n) {
                if (size_ >= n)
                    return;
                reset();
                ptr_  = rebind_traits<T>::allocate(alloc_, n);
                size_ = n;
            }
            void reset() noexcept {
                if (ptr_) {
                    rebind_traits<T>::deallocate(alloc_, ptr_, size_);
                    ptr_  = nullptr;
                    size_ = 0;
                }
            }
            [[nodiscard]] auto data() noexcept -> T * { return ptr_; }
            [[nodiscard]] auto size() const noexcept -> std::size_t { return size_; }

          private:
            [[no_unique_address]] rebind_t<T> alloc_{};
            T                                *ptr_  = nullptr;
            std::size_t                       size_ = 0;
        };

      public:
        Scratch() = default;
        explicit Scratch(const Allocator &a) : perm_inv_(a), xp_packed_(a), out_packed_(a), counts_(a), leaf_ids_(a) {}

        void reserve(const Function &fn, std::size_t n_max) {
            const auto        n_leaves = static_cast<std::uint32_t>(fn.polyfits_.size());
            const std::size_t want     = std::min(n_max, std::max(kDefaultTileK, fn.polyfits_.size() * 32));
            // Grow-only on the per-tile cap. The SoA reinterpretation
            // of `out_packed_` indexes off `tile_cap_`, so quietly
            // lowering it would alias the per-component spans onto the
            // wrong half of the buffer on the next call.
            const std::size_t tile_cap = std::max(tile_cap_, want);
            perm_inv_.ensure_size(tile_cap);
            xp_packed_.ensure_size(input_dim * tile_cap);
            out_packed_.ensure_size(output_dim * tile_cap);
            counts_.ensure_size(std::size_t{n_leaves} + 1);
            // Materialised leaf-id buffer for the descent (`!table`) path only:
            // there pass 1 stores each point's id so pass 2 reads it back
            // instead of re-walking the tree (a full `get_node_index` descent
            // per point). The 1D leaf-table fast path keeps re-quantizing (one
            // cheap SIMD quantize per W points), so it never needs this buffer.
            const bool table = fn.subtrees_.size() == 1 && fn.subtrees_.front().has_leaf_table();
            if (!table)
                leaf_ids_.ensure_size(tile_cap);
            tile_cap_ = tile_cap;
        }

      private:
        friend class Function;

        Buf<std::uint32_t> perm_inv_;
        Buf<value_type>    xp_packed_;
        Buf<value_type>    out_packed_;
        Buf<std::uint32_t> counts_;
        Buf<std::uint32_t> leaf_ids_; // descent path only; empty otherwise
        std::size_t        tile_cap_ = 0;

        [[nodiscard]] auto perm_inv() noexcept { return perm_inv_.data(); }
        [[nodiscard]] auto xp_packed() noexcept { return xp_packed_.data(); }
        [[nodiscard]] auto out_packed() noexcept { return out_packed_.data(); }
        [[nodiscard]] auto counts() noexcept { return counts_.data(); }
        [[nodiscard]] auto leaf_ids() noexcept { return leaf_ids_.data(); }
        // SoA reinterpretation of `out_packed_`: OutputDim contiguous spans
        // of `tile_cap_` elements each. Each leaf's range [off, off+cnt)
        // in component d lives at out_soa_base(d) + off.
        [[nodiscard]] auto out_soa_base(std::size_t d) noexcept -> value_type * {
            return out_packed_.data() + d * tile_cap_;
        }
    };

  public:
    /// Batch evaluation: `n_trg` points written into `res`.
    ///
    /// Pipeline (unsorted, the general path):
    ///
    ///   1. **Leaf-id traversal.** For each input point, look up the owning
    ///      leaf index (the `polyfits_` slot that holds its Horner
    ///      coefficients). When the Function has a single subtree with a
    ///      precomputed leaf-table the lookup is a quantize + u32 load and
    ///      folds OOD detection into the same unsigned wrap test. Otherwise,
    ///      we descend the tree per point. Out-of-domain points are tagged
    ///      with the sentinel id `n_leaves` and counted in their own bucket.
    ///   2. **Counting sort + in-place exclusive scan.** Histogram leaf
    ///      populations into `counts[0..n_leaves]`, then exclusive-scan
    ///      `counts` in place. After scatter (next stage) consumes `counts`
    ///      as a cursor, `counts[k]` equals the one-past-end of leaf k's
    ///      slice — enough to recover `(off, cnt)` for the per-leaf dispatch
    ///      by walking ids with a running `prev_end` (Reinecke's trick).
    ///   3. **Scatter to packed layout.** Walk points in input order; for
    ///      each, append its coordinates to `xp_packed` at its leaf's cursor
    ///      (`counts[id]++`) and record the inverse mapping in
    ///      `perm[dst] = i`. Result: points sharing a leaf land contiguously
    ///      and in lock-step between `xp_packed` and the soon-to-be-filled
    ///      `out_packed`.
    ///   4. **Per-leaf SIMD batch eval.** For each non-empty leaf, hand its
    ///      contiguous slice of `xp_packed` to polyfit's SIMD batch kernel
    ///      once and write into the same slice of `out_packed`. This is the
    ///      whole point of the sort: one fixed coefficient set, one SIMD
    ///      Horner stream, no per-point branch on which leaf to evaluate.
    ///      The OOD bucket is filled with NaN instead of evaluated.
    ///   5. **Permute back to caller order.** For each `dst`, copy
    ///      `out_packed[dst]` into `res[perm[dst] * output_dim]`. The store
    ///      address is random in `res`, so we prefetch ahead by `LOOKAHEAD`
    ///      to hide the RFO latency that otherwise dominates 1D throughput.
    ///
    /// Tiny batches (`n_trg < kSortThreshold`) skip stages 2–5 and just
    /// loop point-at-a-time — the counting sort can't amortize its setup
    /// at that size. Large batches are tiled (`kDefaultTileK`, lifted by an
    /// adaptive floor for high-leaf-count Functions) so the packed buffers
    /// fit in L1d/L2.
    ///
    /// For 1D, callers who can promise sortedness should prefer
    /// `sorted(xp, res, n)` — it skips stages 2, 3, 5 entirely and
    /// runs ~3–4× faster.
    ///
    /// Thread-safe: a single Function may be called concurrently from
    /// multiple threads provided each call's `xp` and `res` slices do not
    /// overlap with another thread's. Scratch buffers are allocated
    /// (via `allocator`, default `std::allocator<value_type>`) on entry
    /// and freed on return — no state is carried between calls. Callers
    /// that want pooled reuse should pass a stateful allocator (e.g.
    /// `std::pmr::polymorphic_allocator` over a monotonic buffer).
    /// Pinned by `tests/test_threadsafe.cpp`.
    /// @param xp         `n_trg * input_dim` packed input coordinates.
    /// @param res        `n_trg * output_dim` output buffer.
    /// @param n_trg      number of points to evaluate.
    /// @param allocator  allocator for the per-call scratch (optional).
    template <class Allocator = std::allocator<value_type>>
    TREEWEAVE_FLATTEN auto operator()(const value_type *xp, value_type *res, std::size_t n_trg,
                                      const Allocator &allocator = {}) const -> void {
        Scratch<Allocator> s(allocator);
        eval_batch(xp, res, n_trg, s);
    }

    /// SoA-output batch evaluation. Same pipeline as the AoS overload but
    /// writes each output component into its own stride-1 buffer
    /// (`soa_out[d][k]` is point k's component d) instead of an interleaved
    /// `[c0 c1 ... cD-1 c0 c1 ...]` stream in a single buffer. Delegates
    /// to polyfit's SoA P2 overload (`FuncEvalND::operator()(pts, soa,
    /// count)`) so the per-leaf kernel writes SoA natively — no AoS
    /// materialise + deinterleave detour. Each `soa_out[d]` must hold
    /// `n_trg` elements.
    ///
    /// Only available when `output_dim > 1` — for scalar outputs the AoS
    /// and SoA layouts coincide, so the AoS overload is the canonical
    /// entry point.
    template <class Allocator = std::allocator<value_type>>
    TREEWEAVE_FLATTEN auto operator()(const value_type *xp, std::array<value_type *, output_dim> soa_out,
                                      std::size_t n_trg, const Allocator &allocator = {}) const -> void
        requires(output_dim > 1)
    {
        Scratch<Allocator> s(allocator);
        eval_batch_soa(xp, soa_out, n_trg, s);
    }

  private:
    template <class Allocator>
    TREEWEAVE_FLATTEN auto eval_batch_soa(const value_type *xp, std::array<value_type *, output_dim> soa_out,
                                          std::size_t n_trg, Scratch<Allocator> &s) const -> void {
        if (n_trg == 0) [[unlikely]]
            return;
        if (n_trg == 1) [[unlikely]] {
            const detail::Value<value_type, input_dim>  xi(xp);
            const detail::Value<value_type, output_dim> tmp = (*this)(xi);
            poet::static_for<output_dim>([&](auto D) -> void {
                constexpr std::size_t d = D;
                soa_out[d][0]           = tmp[d];
            });
            return;
        }

        constexpr std::size_t kSortThreshold = 32;
        if (n_trg < kSortThreshold) {
            for (std::size_t i_trg = 0; i_trg < n_trg; ++i_trg) {
                const detail::Value<value_type, input_dim>  xi(xp + (input_dim * i_trg));
                const detail::Value<value_type, output_dim> tmp = (*this)(xi);
                poet::static_for<output_dim>([&](auto D) -> void {
                    constexpr std::size_t d = D;
                    soa_out[d][i_trg]       = tmp[d];
                });
            }
            return;
        }

        constexpr std::size_t kMinPtsPerLeaf = 32;
        const std::size_t     tile_K         = std::max(kDefaultTileK, polyfits_.size() * kMinPtsPerLeaf);

        s.reserve(*this, std::min(n_trg, tile_K));

        if (n_trg > tile_K) {
            for (std::size_t tile_off = 0; tile_off < n_trg; tile_off += tile_K) {
                const std::size_t                    tile_n = std::min(tile_K, n_trg - tile_off);
                std::array<value_type *, output_dim> tile_soa{};
                poet::static_for<output_dim>([&](auto D) -> void {
                    constexpr std::size_t d = D;
                    tile_soa[d]             = soa_out[d] + tile_off;
                });
                eval_batch_tile_soa(xp + (input_dim * tile_off), tile_soa, tile_n, s);
            }
            return;
        }
        eval_batch_tile_soa(xp, soa_out, n_trg, s);
    }

    template <class Allocator>
    TREEWEAVE_FLATTEN auto eval_batch(const value_type *xp, value_type *res, std::size_t n_trg,
                                      Scratch<Allocator> &s) const -> void {
        if (n_trg == 0) [[unlikely]]
            return;
        if (n_trg == 1) [[unlikely]] {
            const detail::Value<value_type, input_dim>  xi(xp);
            const detail::Value<value_type, output_dim> tmp = (*this)(xi);
            std::copy(tmp.begin(), tmp.end(), res);
            return;
        }

        // Below this point the counting-sort overhead likely exceeds the
        // SIMD gain — fall through to scalar per-point.
        constexpr std::size_t kSortThreshold = 32;
        if (n_trg < kSortThreshold) {
            for (std::size_t i_trg = 0; i_trg < n_trg; ++i_trg) {
                const detail::Value<value_type, input_dim>  xi(xp + (input_dim * i_trg));
                const detail::Value<value_type, output_dim> tmp = (*this)(xi);
                std::copy(tmp.begin(), tmp.end(), res + (i_trg * output_dim));
            }
            return;
        }

        // Tile the batch path so the per-tile working set fits L1d/L2.
        // The adaptive floor `n_leaves * kMinPtsPerLeaf` keeps each tile
        // populated enough to amortise the polyfit batch kernel's
        // per-call setup — high-leaf-count Functions (e.g. 2D bump,
        // ~7700 leaves) regress sharply at a hard 64 K tile.
        constexpr std::size_t kMinPtsPerLeaf = 32;
        const std::size_t     tile_K         = std::max(kDefaultTileK, polyfits_.size() * kMinPtsPerLeaf);

        // Scratch is grown to one tile (idempotent if already sized) and
        // reused across tiles. Only `counts` needs zeroing between tiles;
        // the other buffers are write-before-read on every tile.
        s.reserve(*this, std::min(n_trg, tile_K));

        if (n_trg > tile_K) {
            for (std::size_t tile_off = 0; tile_off < n_trg; tile_off += tile_K) {
                const std::size_t tile_n = std::min(tile_K, n_trg - tile_off);
                eval_batch_tile(xp + (input_dim * tile_off), res + (output_dim * tile_off), tile_n, s);
            }
            return;
        }
        eval_batch_tile(xp, res, n_trg, s);
    }

  public:
    /// Sorted-input batch evaluation (1D).
    ///
    /// The caller promises `xp[i] <= xp[i+1]`. Under that promise the
    /// leaf-id sequence is monotone non-decreasing (1D leaves tile the
    /// domain in coordinate order), so points sharing a leaf are
    /// already contiguous in the input. That collapses the unsorted
    /// pipeline's five stages to two:
    ///
    ///   * find the leaf at `i`, scan forward until the leaf changes,
    ///   * dispatch the run `[i, j)` directly to polyfit's SIMD batch
    ///     kernel writing straight into `res + i`.
    ///
    /// No `leaf_ids` write, no counts/prefix-sum, no scatter into
    /// `xp_packed`, no permute back from `out_packed`, no scratch
    /// allocation at all — the input and output buffers themselves
    /// are the packed layout, and the run-length scan amortizes the
    /// per-leaf eval as well as the counting sort did.
    ///
    /// OOD points form a contiguous prefix and/or suffix (since the
    /// input is sorted) and are NaN-filled by two short guards around
    /// the main loop.
    ///
    /// On a paired bench (1D, presorted, `-O3 -march=native`) this path
    /// is ~3–4× faster than calling `operator()(xp, res, n)` on the
    /// same buffer: ~1.5 ns/eval vs ~5.5 ns/eval at N=1e6 across both
    /// the leaf-table fast path and the descent fallback. ins/eval drops
    /// from ~36 to ~14 — exactly the work removed by skipping stages
    /// 2, 3, 5 of the unsorted pipeline.
    ///
    /// `res` must hold `n * output_dim` elements. Restricted to
    /// `input_dim == 1`: 2D/3D leaf-id sequences are not monotone under
    /// single-axis sorting so the same trick does not apply.
    /// Sorted-input + SoA output (1D). Same monotone-leaf-id trick as
    /// the AoS sorted path, but each per-leaf run dispatches to
    /// polyfit's SoA P2 overload and writes straight into the caller's
    /// per-component buffers — no permute, no scatter. Only available
    /// when `output_dim > 1`; for scalar outputs the AoS `sorted(...)`
    /// is canonical.
    TREEWEAVE_FLATTEN auto sorted(const value_type *xp, std::array<value_type *, output_dim> soa_out,
                                  std::size_t n) const -> void
        requires(input_dim == 1 && output_dim > 1)
    {
        if (n == 0) [[unlikely]]
            return;

        constexpr value_type nan_v     = std::numeric_limits<value_type>::quiet_NaN();
        auto                 write_nan = [&](std::size_t i) -> void {
            poet::static_for<output_dim>([&](auto D) -> void {
                constexpr std::size_t d = D;
                soa_out[d][i]           = nan_v;
            });
        };

        const auto          n_leaves = static_cast<std::uint32_t>(polyfits_.size());
        const std::uint32_t ood_id   = n_leaves;
        const bool          fast     = subtrees_.size() == 1 && subtrees_.front().has_leaf_table();

        auto x_in = [&](std::size_t i) -> input_type {
            if constexpr (poly_eval::detail::hasTupleSize_v<input_type>)
                return input_type{xp[i]};
            else
                return xp[i];
        };

        auto leaf_id_at = [&](std::size_t i) -> std::uint32_t {
            if (fast)
                return subtrees_.front().find_leaf_id_with_ood(x_in(i), ood_id);
            const value_type xv = xp[i];
            // Inclusive high bound for the closed upper endpoint: `x == upper`
            // flows into the (clamped) leaf lookup; finite `x > upper` and
            // OOD-low fall to `ood_id`.
            if (xv < lower_left_[0] || xv > upper_right_[0])
                return ood_id;
            const auto x = x_in(i);
            return subtrees_[get_linear_bin(x)].find_leaf_id(x);
        };

        std::size_t i = 0;
        while (i < n && xp[i] < lower_left_[0]) {
            write_nan(i);
            ++i;
        }

        while (i < n) {
            // `>` not `>=`: the closed upper endpoint `x == upper` is evaluated
            // (clamped to the last leaf); the OOD-high suffix starts strictly
            // above it.
            if (xp[i] > upper_right_[0]) [[unlikely]] {
                do {
                    write_nan(i);
                    ++i;
                } while (i < n);
                break;
            }
            const std::uint32_t id = leaf_id_at(i);
            if (id == ood_id) [[unlikely]] {
                write_nan(i);
                ++i;
                continue;
            }
            std::size_t j = i + 1;
            while (j < n && leaf_id_at(j) == id)
                ++j;

            if constexpr (poly_eval::detail::hasTupleSize_v<input_type>) {
                using CI = poly_eval_type::CanonicalInput;
                std::array<value_type *, output_dim> run_soa{};
                poet::static_for<output_dim>([&](auto D) -> void {
                    constexpr std::size_t d = D;
                    run_soa[d]              = soa_out[d] + i;
                });
                polyfits_[id](reinterpret_cast<const CI *>(xp + i), run_soa, j - i);
            } else {
                static_assert(output_dim == 1, "scalar-input fits are 1-output by construction");
                polyfits_[id](xp + i, soa_out[0] + i, j - i);
            }
            i = j;
        }
    }

    TREEWEAVE_FLATTEN auto sorted(const value_type *xp, value_type *res, std::size_t n) const -> void
        requires(input_dim == 1)
    {
        if (n == 0) [[unlikely]]
            return;

        constexpr value_type nan_v     = std::numeric_limits<value_type>::quiet_NaN();
        auto                 write_nan = [&](std::size_t i) -> void {
            for (std::size_t j = 0; j < output_dim; ++j)
                res[i * output_dim + j] = nan_v;
        };

        const auto          n_leaves = static_cast<std::uint32_t>(polyfits_.size());
        const std::uint32_t ood_id   = n_leaves;
        const bool          fast     = subtrees_.size() == 1 && subtrees_.front().has_leaf_table();

        auto x_in = [&](std::size_t i) -> input_type {
            if constexpr (poly_eval::detail::hasTupleSize_v<input_type>)
                return input_type{xp[i]};
            else
                return xp[i];
        };

        auto leaf_id_at = [&](std::size_t i) -> std::uint32_t {
            if (fast)
                return subtrees_.front().find_leaf_id_with_ood(x_in(i), ood_id);
            const value_type xv = xp[i];
            // Inclusive high bound for the closed upper endpoint: `x == upper`
            // flows into the (clamped) leaf lookup; finite `x > upper` and
            // OOD-low fall to `ood_id`.
            if (xv < lower_left_[0] || xv > upper_right_[0])
                return ood_id;
            const auto x = x_in(i);
            return subtrees_[get_linear_bin(x)].find_leaf_id(x);
        };

        std::size_t i = 0;
        // OOD prefix: sorted input means anything below lower_left_[0]
        // is contiguous at the front.
        while (i < n && xp[i] < lower_left_[0]) {
            write_nan(i);
            ++i;
        }

        while (i < n) {
            // `>` not `>=`: the closed upper endpoint `x == upper` is evaluated
            // (clamped to the last leaf); the OOD-high suffix starts strictly
            // above it.
            if (xp[i] > upper_right_[0]) [[unlikely]] {
                // OOD suffix begins here (and continues to the end).
                do {
                    write_nan(i);
                    ++i;
                } while (i < n);
                break;
            }
            const std::uint32_t id = leaf_id_at(i);
            if (id == ood_id) [[unlikely]] {
                // Fast-path quantize wrap can flag points slightly above
                // the upper bound that survived the explicit prefix
                // guards (e.g. NaN). Fall back to a per-point NaN here.
                write_nan(i);
                ++i;
                continue;
            }
            std::size_t j = i + 1;
            while (j < n && leaf_id_at(j) == id)
                ++j;
            // Array-spelled 1D goes through polyfit's FuncEvalND, whose
            // batch overload takes (CanonicalInput*, CanonicalOutput*, n).
            // `array<value_type, 1>` and `array<value_type, OUT_DIM>` are
            // layout-equivalent to the packed scalar buffers, so the
            // reinterpret_cast is the same trick the unsorted tile uses
            // around the eval_batch_tile polyfit call.
            if constexpr (poly_eval::detail::hasTupleSize_v<input_type>) {
                using CI = poly_eval_type::CanonicalInput;
                using CO = poly_eval_type::CanonicalOutput;
                polyfits_[id](reinterpret_cast<const CI *>(xp + i), reinterpret_cast<CO *>(res + i * output_dim),
                              j - i);
            } else {
                polyfits_[id](xp + i, res + i, j - i);
            }
            i = j;
        }
    }

    /// Per-point leaf-id lookup with out-of-domain handling, shared by the
    /// AoS and SoA unsorted tile partitions (via `partition_into_leaves`).
    /// `table` is the hoisted `subtrees_.size() == 1 && front().has_leaf_table()`
    /// invariant, passed in so the per-point loop never re-derives it.
    /// ALWAYS_INLINE so every call site folds back to the open-coded lookup
    /// (codegen verified).
    ///
    /// The `sorted` paths deliberately do NOT call this: their bespoke scalar
    /// early-return OOD check compiles to a tighter 1D loop than this generic
    /// flag+ternary form, and unifying them was shown (objdump) to grow and
    /// reorder the `sorted` hot loop. Keep the two forms separate.
    [[nodiscard]] TREEWEAVE_ALWAYS_INLINE auto leaf_id_of(const detail::Value<value_type, input_dim> &xi,
                                                          std::uint32_t ood_id, bool table) const -> std::uint32_t {
        if (table)
            return subtrees_.front().find_leaf_id_with_ood(xi, ood_id);
        bool in_domain = true;
        poet::static_for<input_dim>([&](auto D) -> void {
            constexpr std::size_t d = D;
            // Inclusive high bound: `x == upper_right_` stays in-domain and
            // feeds the clamped `get_linear_bin` + `find_leaf_id` (closed upper
            // endpoint). OOD-low and finite `x > upper` fall to `ood_id`.
            if (xi[d] < lower_left_[d] || xi[d] > upper_right_[d])
                in_domain = false;
        });
        return in_domain ? subtrees_[get_linear_bin(xi)].find_leaf_id(xi) : ood_id;
    }

    /// Stages 1–3 of the unsorted tile pipeline, shared (byte-identical)
    /// between the AoS and SoA tile bodies: zero `counts`, histogram leaf
    /// populations, exclusive-scan into slice starts, then scatter each
    /// point's coords into `xp_packed` while recording the inverse
    /// permutation in `perm_inv`. On return `counts[k]` is leaf k's
    /// one-past-end cursor (Reinecke) — enough to recover (off, cnt) per leaf
    /// in the dispatch walk.
    ///
    /// Leaf-id materialisation is path-dependent. The 1D leaf-table fast path
    /// amortises the quantize over an xsimd batch (`for_each_leaf_id_batch`,
    /// FINUFFT bin-sort recon, spread.hpp:421) and re-quantizes in pass 2 —
    /// one cheap SIMD quantize per W points beats the L1d round trip a stored
    /// array would cost. The descent (`!table`) path is the opposite: each
    /// lookup is a full `get_node_index` tree walk, far dearer than a u32
    /// load, so pass 1 stores ids into `leaf_ids_` and pass 2 reads them back —
    /// halving the descents per point (measured ~1.6× full-throughput on deep
    /// no-leaf-table 1D fits, depth 17–18; see bench/binsort_phase0.md).
    template <class Allocator>
    TREEWEAVE_ALWAYS_INLINE auto partition_into_leaves(const value_type *xp, std::size_t n_trg, Scratch<Allocator> &s,
                                                       std::uint32_t ood_id) const -> void {
        const std::uint32_t n_leaves    = ood_id; // sentinel id == n_leaves
        auto               *perm_inv    = s.perm_inv();
        auto               *xp_packed   = s.xp_packed();
        auto               *counts      = s.counts();
        auto               *leaf_id_buf = s.leaf_ids(); // sized only on the descent path

        const bool table = subtrees_.size() == 1 && subtrees_.front().has_leaf_table();

        // `counts` is the only scratch buffer that needs reset between
        // tiles — the others are write-before-read.
        std::memset(counts, 0, (n_leaves + 1) * sizeof(std::uint32_t));

        // Pass 1 — histogram. On the descent (`!table`) path, also store each
        // point's id in `leaf_ids` so pass 2 skips the second tree walk.
        if constexpr (input_dim == 1) {
            if (table) {
                subtrees_.front().for_each_leaf_id_batch(
                    xp, ood_id, n_trg, [&](std::size_t /*i*/, std::uint32_t id) -> void { ++counts[id]; });
            } else {
                for (std::size_t i = 0; i < n_trg; ++i) {
                    const detail::Value<value_type, input_dim> xi(xp + i);
                    const std::uint32_t                        id = leaf_id_of(xi, ood_id, table);
                    leaf_id_buf[i]                                = id;
                    ++counts[id];
                }
            }
        } else if (table) {
            // ND with a leaf table: re-quantize is cheap, so do not materialise.
            for (std::size_t i = 0; i < n_trg; ++i) {
                const detail::Value<value_type, input_dim> xi(xp + (input_dim * i));
                ++counts[leaf_id_of(xi, ood_id, table)];
            }
        } else {
            // ND descent: store the id so pass 2 skips the second walk.
            for (std::size_t i = 0; i < n_trg; ++i) {
                const detail::Value<value_type, input_dim> xi(xp + (input_dim * i));
                const std::uint32_t                        id = leaf_id_of(xi, ood_id, table);
                leaf_id_buf[i]                                = id;
                ++counts[id];
            }
        }

        // Exclusive scan in place — counts[k] becomes the start of leaf k's
        // packed slice. After the scatter below consumes it as a cursor,
        // counts[k] will equal the one-past-end of that slice (Reinecke).
        std::exclusive_scan(counts, counts + n_leaves + 1, counts, std::uint32_t{0});

        // Pass 2 — scatter. The 1D leaf-table path re-quantizes (cheap); the
        // descent (`!table`) path reads the id stored in pass 1 (no re-walk).
        // Place each point's coords at `xp_packed[counts[id]++]` and record the
        // inverse mapping in `perm_inv`.
        if constexpr (input_dim == 1) {
            if (table) {
                subtrees_.front().for_each_leaf_id_batch(xp, ood_id, n_trg,
                                                         [&](std::size_t i, std::uint32_t id) -> void {
                                                             const std::uint32_t dst = counts[id]++;
                                                             perm_inv[i]             = dst;
                                                             xp_packed[dst]          = xp[i];
                                                         });
            } else {
                for (std::size_t i = 0; i < n_trg; ++i) {
                    const std::uint32_t id  = leaf_id_buf[i];
                    const std::uint32_t dst = counts[id]++;
                    perm_inv[i]             = dst;
                    xp_packed[dst]          = xp[i];
                }
            }
        } else {
            for (std::size_t i = 0; i < n_trg; ++i) {
                const detail::Value<value_type, input_dim> xi(xp + (input_dim * i));
                // ND-table re-quantizes (cheap); ND-descent reads the stored id.
                const std::uint32_t id  = table ? leaf_id_of(xi, ood_id, table) : leaf_id_buf[i];
                const std::uint32_t dst = counts[id]++;
                perm_inv[i]             = dst;
                value_type *dstp        = xp_packed + (input_dim * dst);
                poet::static_for<input_dim>([&](auto D) -> void {
                    constexpr std::size_t d = D;
                    dstp[d]                 = xi[d];
                });
            }
        }
    }

#ifdef TREEWEAVE_PARTITION_HOOK
    /// Benchmark-only per-phase split of the 1D leaf-table bin sort. Runs the
    /// three sub-phases of `partition_into_leaves` `reps` times over the same
    /// `n` points, timing each separately with `__rdtsc`, and returns the
    /// accumulated TSC cycles `{quantize_only, histogram, scatter}`. The
    /// histogram and scatter passes each re-run the quantize, so the caller
    /// recovers the histogram/scatter-specific cost by subtracting the
    /// quantize-only figure. Compiled only when the harness defines
    /// `TREEWEAVE_BENCH_PARTITION_HOOK`; see the include note at file top.
    auto bench_partition_phases(const value_type *xp, std::size_t n, std::size_t reps) const
        -> std::array<std::uint64_t, 3>
        requires(input_dim == 1)
    {
        const auto          n_leaves = static_cast<std::uint32_t>(polyfits_.size());
        const std::uint32_t ood_id   = n_leaves;
        Scratch<>           s;
        s.reserve(*this, n);
        auto       *perm_inv  = s.perm_inv();
        auto       *xp_packed = s.xp_packed();
        auto       *counts    = s.counts();
        const auto &st        = subtrees_.front();

        std::array<std::uint64_t, 3> acc{0, 0, 0};
        for (std::size_t r = 0; r < reps; ++r) {
            // (1) Quantize only — store the id into perm_inv so the loop is
            //     not dead-code-eliminated.
            const std::uint64_t t0 = __rdtsc();
            st.for_each_leaf_id_batch(xp, ood_id, n, [&](std::size_t i, std::uint32_t id) { perm_inv[i] = id; });
            const std::uint64_t t1 = __rdtsc();

            // (2) Histogram (includes the quantize).
            std::memset(counts, 0, (std::size_t{n_leaves} + 1) * sizeof(std::uint32_t));
            const std::uint64_t t2 = __rdtsc();
            st.for_each_leaf_id_batch(xp, ood_id, n, [&](std::size_t /*i*/, std::uint32_t id) { ++counts[id]; });
            const std::uint64_t t3 = __rdtsc();

            std::exclusive_scan(counts, counts + n_leaves + 1, counts, std::uint32_t{0});

            // (3) Scatter (includes the quantize).
            const std::uint64_t t4 = __rdtsc();
            st.for_each_leaf_id_batch(xp, ood_id, n, [&](std::size_t i, std::uint32_t id) {
                const std::uint32_t dst = counts[id]++;
                perm_inv[i]             = dst;
                xp_packed[dst]          = xp[i];
            });
            const std::uint64_t t5 = __rdtsc();

            acc[0] += t1 - t0;
            acc[1] += t3 - t2;
            acc[2] += t5 - t4;
        }
        return acc;
    }
#endif // TREEWEAVE_BENCH_PARTITION_HOOK

    /// Stage 4 skeleton, shared between the tile bodies. Walk the packed leaf
    /// slices in id order (recovering each `(off, cnt)` from the Reinecke
    /// cursor in `counts`), speculatively prefetch the next non-empty leaf's
    /// coefficient store, and invoke `eval_run(id, off, cnt)` on every
    /// non-empty leaf. Returns the one-past-end of the last real leaf's slice
    /// (== the OOD bucket's offset). Only the per-run polyfit dispatch (the
    /// `eval_run` callable) differs between the AoS and SoA layouts.
    template <class EvalRun>
    TREEWEAVE_ALWAYS_INLINE auto dispatch_packed_leaves(const std::uint32_t *counts, std::uint32_t n_leaves,
                                                        EvalRun eval_run) const -> std::uint32_t {
        std::uint32_t prev_end = 0;
        for (std::uint32_t id = 0; id < n_leaves; ++id) {
            const std::uint32_t end = counts[id];
            const std::uint32_t cnt = end - prev_end;
            const std::uint32_t off = prev_end;
            prev_end                = end;
            if (cnt == 0)
                continue;
#if defined(__GNUC__) || defined(__clang__)
            std::uint32_t next_id = id + 1;
            while (next_id < n_leaves && counts[next_id] == end)
                ++next_id;
            if (next_id < n_leaves) {
                if constexpr (poly_eval::detail::hasTupleSize_v<input_type>) {
                    // ND: polyfit doesn't expose a coefficient pointer; the
                    // evaluator object's first cacheline contains domain
                    // params and (on default layouts) the start of coeffsFlat.
                    __builtin_prefetch(&polyfits_[next_id]);
                } else {
                    __builtin_prefetch(polyfits_[next_id].coeffs().data());
                }
            }
#endif
            eval_run(id, off, cnt);
        }
        return prev_end;
    }

    /// Per-tile body of the unsorted batch pipeline (stages 1–5, see the
    /// `operator()(xp, res, n)` doc above). The caller ensures
    /// `n_trg >= kSortThreshold` and `n_trg <= tile_K`, and owns the
    /// scratch buffers (sized to one tile, reused across tiles). Stages 1–3
    /// run in `partition_into_leaves` and stage 4 in `dispatch_packed_leaves`,
    /// both shared with the SoA twin; only the interleaved AoS write-back
    /// (OOD-fill + permute) is spelled here.
    template <class Allocator>
    auto eval_batch_tile(const value_type *xp, value_type *res, std::size_t n_trg, Scratch<Allocator> &s) const
        -> void {
        const auto          n_leaves = static_cast<std::uint32_t>(polyfits_.size());
        const std::uint32_t ood_id   = n_leaves; // sentinel bucket for out-of-domain

        partition_into_leaves(xp, n_trg, s, ood_id);

        auto *perm_inv   = s.perm_inv();
        auto *xp_packed  = s.xp_packed();
        auto *out_packed = s.out_packed();
        auto *counts     = s.counts();

        // Stage 4 — per-leaf SIMD batch eval into the interleaved out_packed.
        const std::uint32_t ood_off = dispatch_packed_leaves(
            counts, n_leaves, [&](std::uint32_t id, std::uint32_t off, std::uint32_t cnt) -> void {
                if constexpr (poly_eval::detail::hasTupleSize_v<input_type>) {
                    using CI       = poly_eval_type::CanonicalInput;
                    using CO       = poly_eval_type::CanonicalOutput;
                    const CI *pts  = reinterpret_cast<const CI *>(xp_packed + (input_dim * off));
                    CO       *outs = reinterpret_cast<CO *>(out_packed + (output_dim * off));
                    polyfits_[id](pts, outs, static_cast<std::size_t>(cnt));
                } else {
                    polyfits_[id](xp_packed + off, out_packed + off, static_cast<std::size_t>(cnt));
                }
            });

        // Fill OOD slots with NaN.
        const std::uint32_t ood_cnt = counts[ood_id] - ood_off;
        if (ood_cnt) {
            constexpr value_type nan_v = std::numeric_limits<value_type>::quiet_NaN();
            for (std::uint32_t k = 0; k < ood_cnt; ++k)
                for (std::size_t j = 0; j < output_dim; ++j)
                    out_packed[output_dim * (ood_off + k) + j] = nan_v;
        }

        // Permute outputs back to caller order.
        // Inverse-permutation form: sequential write to `res`, random read
        // from `out_packed`. Sequential stores coalesce (no RFO); random
        // loads are easily prefetched.
        [[maybe_unused]] constexpr std::size_t LOOKAHEAD = 32;
        for (std::size_t i = 0; i < n_trg; ++i) {
#if defined(__GNUC__) || defined(__clang__)
            if (i + LOOKAHEAD < n_trg) {
                const std::uint32_t pf = perm_inv[i + LOOKAHEAD];
                __builtin_prefetch(out_packed + (output_dim * pf),
                                   /*rw=*/0, /*locality=*/0);
            }
#endif
            const std::uint32_t src  = perm_inv[i];
            const value_type   *srcp = out_packed + (output_dim * src);
            value_type         *dstp = res + (output_dim * i);
            poet::static_for<output_dim>([&](auto J) -> void {
                constexpr std::size_t j = J;
                dstp[j]                 = srcp[j];
            });
        }
    }

    /// SoA-output twin of `eval_batch_tile`. Same five-stage pipeline; the
    /// only deltas are (a) the per-leaf polyfit call routes through the
    /// SoA P2 overload (or `FuncEval`'s AoS path when output_dim == 1,
    /// where SoA and AoS coincide), and (b) the permute-back stage writes
    /// `soa_out[d][perm_inv_idx]` for each component d instead of an
    /// interleaved store. The packed working set lives in `out_packed_`
    /// reinterpreted as OutputDim contiguous spans of `tile_cap()`
    /// elements (see `Scratch::out_soa_base`).
    template <class Allocator>
    auto eval_batch_tile_soa(const value_type *xp, std::array<value_type *, output_dim> soa_out, std::size_t n_trg,
                             Scratch<Allocator> &s) const -> void {
        const auto          n_leaves = static_cast<std::uint32_t>(polyfits_.size());
        const std::uint32_t ood_id   = n_leaves;

        partition_into_leaves(xp, n_trg, s, ood_id);

        auto *perm_inv  = s.perm_inv();
        auto *xp_packed = s.xp_packed();
        auto *counts    = s.counts();

        // Per-component packed output bases. The SoA reinterpretation of
        // `out_packed_` — see Scratch::out_soa_base.
        std::array<value_type *, output_dim> out_soa_packed{};
        poet::static_for<output_dim>([&](auto D) -> void {
            constexpr std::size_t d = D;
            out_soa_packed[d]       = s.out_soa_base(d);
        });

        // Stage 4 — per-leaf SIMD batch eval via the SoA P2 overload
        // (FuncEvalND) for array-spelled inputs; FuncEval's AoS-batch overload
        // for scalar 1D where output_dim == 1 collapses SoA and AoS.
        const std::uint32_t ood_off = dispatch_packed_leaves(
            counts, n_leaves, [&](std::uint32_t id, std::uint32_t off, std::uint32_t cnt) -> void {
                if constexpr (poly_eval::detail::hasTupleSize_v<input_type>) {
                    using CI      = poly_eval_type::CanonicalInput;
                    const CI *pts = reinterpret_cast<const CI *>(xp_packed + (input_dim * off));
                    std::array<value_type *, output_dim> leaf_soa{};
                    poet::static_for<output_dim>([&](auto D) -> void {
                        constexpr std::size_t d = D;
                        leaf_soa[d]             = out_soa_packed[d] + off;
                    });
                    polyfits_[id](pts, leaf_soa, static_cast<std::size_t>(cnt));
                } else {
                    // Scalar 1D input: output_dim == 1 by construction, so the
                    // single SoA span IS the packed output buffer.
                    static_assert(output_dim == 1, "scalar-input fits are 1-output by construction");
                    polyfits_[id](xp_packed + off, out_soa_packed[0] + off, static_cast<std::size_t>(cnt));
                }
            });

        // Fill OOD slots with NaN — per-component stride-1 stores.
        const std::uint32_t ood_cnt = counts[ood_id] - ood_off;
        if (ood_cnt) {
            constexpr value_type nan_v = std::numeric_limits<value_type>::quiet_NaN();
            poet::static_for<output_dim>([&](auto D) -> void {
                constexpr std::size_t d = D;
                value_type           *p = out_soa_packed[d] + ood_off;
                for (std::uint32_t k = 0; k < ood_cnt; ++k)
                    p[k] = nan_v;
            });
        }

        // Permute outputs back to caller order — per-component stride-1
        // stores into `soa_out[d]`.
        [[maybe_unused]] constexpr std::size_t LOOKAHEAD = 32;
        for (std::size_t i = 0; i < n_trg; ++i) {
#if defined(__GNUC__) || defined(__clang__)
            if (i + LOOKAHEAD < n_trg) {
                const std::uint32_t pf = perm_inv[i + LOOKAHEAD];
                poet::static_for<output_dim>([&](auto D) -> void {
                    constexpr std::size_t d = D;
                    __builtin_prefetch(out_soa_packed[d] + pf,
                                       /*rw=*/0, /*locality=*/0);
                });
            }
#endif
            const std::uint32_t src = perm_inv[i];
            poet::static_for<output_dim>([&](auto D) -> void {
                constexpr std::size_t d = D;
                soa_out[d][i]           = out_soa_packed[d][src];
            });
        }
    }

    /// Point evaluation. Returns NaN for out-of-domain inputs.
    ///
    /// `TREEWEAVE_ALWAYS_INLINE` is load-bearing: without it clang keeps
    /// this out-of-line at -O3 and every scalar evaluator pays a per-
    /// point `callq` + xmm spill (verified via objdump).
    ///
    /// ND residual: `polyfit::FuncEvalND::evalCanonical` is not
    /// always-inlined upstream, so leaf eval keeps one `callq` per ND
    /// point. `TREEWEAVE_FLATTEN` here doesn't fix it — the durable fix is
    /// `PF_ALWAYS_INLINE` on `evalCanonical`.
    [[nodiscard]] TREEWEAVE_ALWAYS_INLINE auto operator()(const input_type &x) const -> output_type {
        bool ood = false;
        poet::static_for<input_dim>([&](auto D) -> void {
            constexpr std::size_t d  = D;
            const value_type      xd = [&]() -> value_type {
                if constexpr (poly_eval::detail::hasTupleSize_v<input_type>)
                    return x[d];
                else
                    return x;
            }();
            // Positive-logic guard: NaN fails both comparisons, so the
            // negation flags it OOD. A `xd < lo || xd > hi` test is *false*
            // for NaN (all NaN compares are false), letting NaN reach
            // `get_linear_bin(NaN)` -> `cvttsd2si(NaN)` = INT64_MIN -> a huge
            // index -> out-of-bounds `subtrees_` read. The high bound is
            // inclusive (`<= upper_right_`) so the closed upper endpoint
            // `x == upper_right_` passes the guard and `get_linear_bin`'s clamp
            // routes it to the last subtree; OOD-low and finite `x > upper`
            // stay NaN. Semantics for interior x are unchanged.
            if (!(xd >= lower_left_[d] && xd <= upper_right_[d]))
                ood = true;
        });
        if (ood) [[unlikely]] {
            // NaN-fill every output component — matches the batch path's OOD
            // bucket. A bare `output_type{nan}` aggregate-initialises only the
            // first element of a multi-output `std::array`, leaving the rest
            // zero (a scalar/batch disagreement caught by the SoA batch test).
            constexpr value_type nan_v = std::numeric_limits<value_type>::quiet_NaN();
            if constexpr (poly_eval::detail::hasTupleSize_v<output_type>) {
                output_type out{};
                for (std::size_t d = 0; d < output_dim; ++d)
                    out[d] = nan_v;
                return out;
            } else {
                return output_type{nan_v};
            }
        }
        return polyfits_[subtrees_[get_linear_bin(x)].find_leaf_id(x)](x);
    }

    /// Panels where adaptive paneling failed at `max_depth`. Always empty
    /// unless `options.allow_max_depth_leaves == true` was set; the default
    /// path throws `MaxDepthExceeded` (which carries the same list) instead.
    [[nodiscard]] auto non_converged_panels() const -> const std::vector<NonConvergedPanel> & {
        return non_converged_panels_;
    }

    [[nodiscard]] auto get_bounds() const -> std::pair<dim_array_t, dim_array_t> {
        return std::make_pair(lower_left_, upper_right_);
    }

    /// True iff every subtree built a leaf-id lookup table. When false,
    /// at least one subtree falls through to the descent path inside
    /// `find_leaf_id` — measurably slower per eval. Exposed so tests can
    /// pin the leaf-table threshold behavior.
    [[nodiscard]] auto all_subtrees_have_leaf_table() const noexcept -> bool {
        for (const auto &st : subtrees_)
            if (!st.has_leaf_table())
                return false;
        return !subtrees_.empty();
    }

    /// Read-only access to the subtree array. Useful for tests and
    /// downstream introspection (leaf counts, table state, max depth).
    [[nodiscard]] auto get_subtrees() const noexcept -> const std::vector<detail::PolyTree<Degree, Func, Policy>> & {
        return subtrees_;
    }

    /// Compile-time-N batch point evaluation (1D scalar inputs/outputs).
    ///
    /// Provided so consumers with a small fixed-size pack of evaluations
    /// can express intent at the call site without an ad-hoc loop. Three
    /// regimes by `N`:
    ///
    ///   * `N <= 16` (small): poet::static_for fully unrolls the
    ///     scalar fan-out. operator() is TREEWEAVE_ALWAYS_INLINE so the
    ///     N FMA chains run on independent registers — best ILP at
    ///     small N.
    ///   * `16 < N < kBatchPathFloor` (medium): plain for-loop. The
    ///     compile-time N still lets the compiler unroll partially,
    ///     and each operator() is inlined, so this matches the
    ///     hand-rolled scalar_loop baseline. Avoids both poet's
    ///     fully-unrolled code bloat at large N and the batch path's
    ///     fixed setup overhead (counts/perm scratch).
    ///   * `N >= kBatchPathFloor` (large): delegate to the SIMD-batched
    ///     `operator()(xp, ys, n)`. The batch-path leaf dispatch
    ///     amortises its fixed overhead only at high N.
    ///
    /// kBatchPathFloor = 1024 picked from the bench_pack_scatter
    /// crossover sweep (1d_runge deg=8, SPR, taskset -c 2). The
    /// 32→1024 bump is commit `3939d75 perf(eval_pack): raise
    /// batch-path floor from 32 to 1024`.
    template <std::size_t N>
    [[nodiscard]] TREEWEAVE_FLATTEN auto eval_pack(const std::array<value_type, N> &xs) const
        -> std::array<value_type, N>
        requires(input_dim == 1 && output_dim == 1)
    {
        constexpr std::size_t     kBatchPathFloor = 1024;
        std::array<value_type, N> ys{};
        if constexpr (N == 0) {
            return ys;
        } else if constexpr (N >= kBatchPathFloor) {
            (*this)(xs.data(), ys.data(), N);
        } else if constexpr (N <= 16) {
            poet::static_for<N>([&](auto I) -> void {
                constexpr std::size_t i = I;
                ys[i]                   = (*this)(xs[i]);
            });
        } else {
            for (std::size_t i = 0; i < N; ++i)
                ys[i] = (*this)(xs[i]);
        }
        return ys;
    }

  private:
    detail::TreeInput input_;
    box_t             box_;
    value_type        tol_;
    dim_array_t       lower_left_{};
    dim_array_t       upper_right_{};

    std::vector<detail::PolyTree<Degree, Func, Policy>> subtrees_;
    detail::Value<std::size_t, input_dim>               n_subtrees_{};
    dim_array_t                                         inv_bin_size_{};

    std::vector<poly_eval_type> polyfits_;

    std::vector<NonConvergedPanel> non_converged_panels_;

    /// Construction-time stats reported via `print_stats()`.
    struct {
        std::size_t   base_depth   = 0;
        std::uint64_t n_evals_root = 0;
        std::uint32_t t_elapsed    = 0;
    } stats_;
};

} // namespace treeweave

#endif // TREEWEAVE_DETAIL_FUNCTION_HPP
