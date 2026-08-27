#ifndef TREEWEAVE_DETAIL_ARCH_DEGREE_TABLE_HPP
#define TREEWEAVE_DETAIL_ARCH_DEGREE_TABLE_HPP
namespace treeweave::capi {
// Fit degree for every C ABI cell; 7 is the only spill-free degree in the
// register-pressured wide cells.
inline constexpr int chosen_degree = 7;
} // namespace treeweave::capi
#endif
