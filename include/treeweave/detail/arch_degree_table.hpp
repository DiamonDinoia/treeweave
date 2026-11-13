#ifndef TREEWEAVE_DETAIL_ARCH_DEGREE_TABLE_HPP
#define TREEWEAVE_DETAIL_ARCH_DEGREE_TABLE_HPP
#include <cstddef>
namespace treeweave::detail {
// Default leaf degree (coefficients per axis) by input dimension. fit uses it,
// and every C ABI cell bakes it in. A native sweep of degrees 4 to 16 on
// x86-64-v3, x86-64-v4 and Apple M2 found 5 fastest in 1D, but 5 needs more
// than 3x the memory of 6 on long oscillatory domains. 2D and 3D use 7: a lower
// degree is faster there but uses more memory than the auto budget allows.
constexpr auto default_degree(std::size_t input_dim) noexcept -> std::size_t { return input_dim == 1 ? 6 : 7; }
} // namespace treeweave::detail
#endif
