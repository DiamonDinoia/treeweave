// Allocator plumbing: the batch path forwards the caller's allocator
// through to the per-call Scratch. Verified by passing a counting
// allocator and observing the buffer allocations land on it.

#include <new>
#include <treeweave/treeweave.hpp>

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <random>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {
inline std::atomic<std::size_t> g_alloc_bytes{0};
inline std::atomic<std::size_t> g_alloc_calls{0};

template <class T>
struct CountingAllocator {
    using value_type = T;

    CountingAllocator() = default;
    template <class U>
    CountingAllocator(const CountingAllocator<U> &) {}

    auto allocate(std::size_t n) -> T * {
        g_alloc_bytes.fetch_add(n * sizeof(T), std::memory_order_relaxed);
        g_alloc_calls.fetch_add(1, std::memory_order_relaxed);
        return static_cast<T *>(::operator new(n * sizeof(T)));
    }
    void deallocate(T *p, std::size_t) noexcept { ::operator delete(p); }

    template <class U>
    auto operator==(const CountingAllocator<U> &) const noexcept -> bool {
        return true;
    }
};
} // namespace

TEST_CASE("batch path routes through caller-supplied allocator", "[treeweave][batch][allocator]") {
    auto fn = treeweave::fit<8>([](double x) { return std::sin(4.0 * x); }, 0.0, 1.0, 1e-10);

    constexpr std::size_t                  N = 4096;
    std::mt19937                           gen(7);
    std::uniform_real_distribution<double> d(1e-3, 1.0 - 1e-3);
    std::vector<double>                    xs(N), out(N), ref(N);
    for (auto &x : xs)
        x = d(gen);

    fn(xs.data(), ref.data(), N); // default allocator → ref output

    g_alloc_bytes = 0;
    g_alloc_calls = 0;
    fn(xs.data(), out.data(), N, CountingAllocator<double>{});

    // Custom allocator saw the scratch allocations.
    REQUIRE(g_alloc_calls.load() > 0);
    REQUIRE(g_alloc_bytes.load() > 0);

    // And produced identical output.
    for (std::size_t i = 0; i < N; ++i)
        REQUIRE(out[i] == ref[i]);
}

TEST_CASE("default-allocator batch path matches custom-allocator path", "[treeweave][batch][allocator]") {
    // Vector output: exercises the SoA overload too.
    auto fn = treeweave::fit<8>(
        [](std::array<double, 2> x) -> std::array<double, 2> {
            return {std::sin(x[0]) * std::cos(x[1]), std::cos(x[0]) * std::sin(x[1])};
        },
        std::array<double, 2>{0.0, 0.0}, std::array<double, 2>{1.0, 1.0}, 1e-10);

    constexpr std::size_t                  N = 2048;
    std::mt19937                           gen(11);
    std::uniform_real_distribution<double> d(1e-3, 1.0 - 1e-3);
    std::vector<double>                    xp(2 * N), aos_default(2 * N), aos_custom(2 * N);
    for (auto &x : xp)
        x = d(gen);

    fn(xp.data(), aos_default.data(), N);
    fn(xp.data(), aos_custom.data(), N, CountingAllocator<double>{});
    for (std::size_t i = 0; i < 2 * N; ++i)
        REQUIRE(aos_default[i] == aos_custom[i]);

    std::vector<double>           c0(N), c1(N);
    std::array<double *, 2> const soa{c0.data(), c1.data()};
    fn(xp.data(), soa, N, CountingAllocator<double>{});
    for (std::size_t i = 0; i < N; ++i) {
        REQUIRE(c0[i] == aos_default[2 * i + 0]);
        REQUIRE(c1[i] == aos_default[2 * i + 1]);
    }
}
