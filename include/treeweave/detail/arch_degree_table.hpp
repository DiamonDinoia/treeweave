#ifndef TREEWEAVE_DETAIL_ARCH_DEGREE_TABLE_HPP
#define TREEWEAVE_DETAIL_ARCH_DEGREE_TABLE_HPP
#include <cstddef>
namespace treeweave::capi {
// Degree 7 wins or ties in all 24 (arch,dtype,input_dim) cells: dim1 within
// ~1%, dim2/dim3 by 2-10x, and 7 is the only spill-free degree in the
// register-pressured wide cells.
// The variable-template form keeps per-cell retuning a one-line
// specialization.
template <class Arch, class T, std::size_t IN>
inline constexpr int chosen_degree = 7;
} // namespace treeweave::capi
#endif
