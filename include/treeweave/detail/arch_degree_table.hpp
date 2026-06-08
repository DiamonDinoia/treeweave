#ifndef TREEWEAVE_DETAIL_ARCH_DEGREE_TABLE_HPP
#define TREEWEAVE_DETAIL_ARCH_DEGREE_TABLE_HPP
#include <cstddef>
namespace treeweave::capi {
// Campaign (benchmark eval-throughput, forcing each arch on a Sapphire-Rapids
// host, + asm spill analysis): degree 7 wins or ties in every one of the 24
// (arch,dtype,input_dim) cells — dim1 within ~1% (7 by tiebreak; trades a
// little memory for speed), dim2/dim3 favor 7 by 2-10x, and 7 is the only
// spill-free degree in the register-pressured wide cells. User directive:
// "7 is best but benchmark; ties -> 7."
// Regen: /tmp/campaign.py + the degree benchmark (/tmp/degree_bench.jsonl,
// /tmp/campaign_results.jsonl). Variable-template form kept so future per-cell
// retuning is a one-line specialization.
template <class Arch, class T, std::size_t IN>
inline constexpr int chosen_degree = 7;
} // namespace treeweave::capi
#endif
