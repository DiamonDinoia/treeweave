// Compile-time correctness tests for treeweave's constexpr surface.
// Every static_assert is simultaneously a UB detector: the constant evaluator
// rejects undefined behaviour (overflow, OOB, uninitialized reads), so a
// passing build implies each expression is well-formed at compile time.
// An empty main means that successfully compiling this TU IS the test pass.

#include <array>
#include <cstddef>

#include <treeweave/detail/numerics.hpp>
#include <treeweave/detail/value.hpp>
#include <treeweave/treeweave.hpp>

// ---- powi -------------------------------------------------------------------
static_assert(treeweave::detail::powi<0>(2) == 1);
static_assert(treeweave::detail::powi<1>(3) == 3);
static_assert(treeweave::detail::powi<2>(4) == 16);
static_assert(treeweave::detail::powi<3>(2) == 8);
static_assert(treeweave::detail::powi<7>(2) == 128);
static_assert(treeweave::detail::powi<0>(0) == 1);

// ---- value_dim --------------------------------------------------------------
static_assert(treeweave::detail::value_dim<double>() == 1);
static_assert(treeweave::detail::value_dim<float>() == 1);
static_assert(treeweave::detail::value_dim<std::array<double, 2>>() == 2);
static_assert(treeweave::detail::value_dim<std::array<float, 3>>() == 3);
static_assert(treeweave::detail::value_dim_v<double> == 1);
static_assert(treeweave::detail::value_dim_v<std::array<double, 4>> == 4);

// ---- auto_memory_budget_mib -------------------------------------------------
static_assert(treeweave::detail::auto_memory_budget_mib(1) == 4);
static_assert(treeweave::detail::auto_memory_budget_mib(2) == 8);
static_assert(treeweave::detail::auto_memory_budget_mib(3) == 16);

// ---- version_at_least -------------------------------------------------------
static_assert(treeweave::version_at_least(0, 0, 0));
static_assert(!treeweave::version_at_least(9999, 0, 0));

// ---- midpoint / half_length (scalar) ----------------------------------------
static_assert(treeweave::detail::midpoint(0.0, 2.0) == 1.0);
static_assert(treeweave::detail::midpoint(1.0f, 3.0f) == 2.0f);
static_assert(treeweave::detail::half_length(0.0, 4.0) == 2.0);
static_assert(treeweave::detail::half_length(-1.0f, 1.0f) == 1.0f);

// ---- midpoint / half_length (array) -----------------------------------------
namespace {
constexpr std::array<double, 2> kA{0.0, 2.0};
constexpr std::array<double, 2> kB{4.0, 6.0};
constexpr auto                  kMid = treeweave::detail::midpoint(kA, kB);
constexpr auto                  kHL  = treeweave::detail::half_length(kA, kB);
static_assert(kMid[0] == 2.0 && kMid[1] == 4.0);
static_assert(kHL[0] == 2.0 && kHL[1] == 2.0);
} // namespace

// ---- Value::prod ------------------------------------------------------------
static_assert(treeweave::detail::Value<double, 1>{3.0}.prod() == 3.0);
static_assert(treeweave::detail::Value<int, 1>{5}.prod() == 5);
namespace {
constexpr treeweave::detail::Value<double, 2> kV2{std::array<double, 2>{2.0, 3.0}};
constexpr treeweave::detail::Value<double, 3> kV3{std::array<double, 3>{2.0, 3.0, 4.0}};
static_assert(kV2.prod() == 6.0);
static_assert(kV3.prod() == 24.0);
} // namespace

// ---- Value arithmetic (constexpr) -------------------------------------------
namespace {
constexpr treeweave::detail::Value<double, 1> kS1{2.0};
constexpr treeweave::detail::Value<double, 1> kS2{3.0};
static_assert((kS1 + kS2).prod() == 5.0);
static_assert((kS2 - kS1).prod() == 1.0);
static_assert((kS1 * kS2).prod() == 6.0);
} // namespace

int main() { return 0; }
