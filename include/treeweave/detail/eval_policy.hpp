#ifndef TREEWEAVE_DETAIL_EVAL_POLICY_HPP
#define TREEWEAVE_DETAIL_EVAL_POLICY_HPP

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include <poet/poet.hpp>
#include <polyfit/polyfit.hpp>

namespace treeweave {

/// Compile-time eval-strategy knob on `Function<Degree, Func, Policy>`.
///
/// Three modes trading single-point latency against batch throughput. Today
/// all three route the scalar path through `ScalarKernel::Horner`; see the
/// rev-2 note below for why.
///
/// - `Latency`   : reserved for a future Hybrid mapping. The rev-1 K-sweep
///                 on Meteor Lake showed `HybridK<2>` (the formula's pick at
///                 Degree=8) is an 8–33 % regression on `operator()(x)` vs
///                 Horner. Until a measured (Degree, microarch) cell beats
///                 Horner on the true scalar path, this policy stays at
///                 `ScalarKernel::Horner` — equivalent to `Balanced`. See
///                 `bench/results.md` for the data.
/// - `Throughput`: maximise per-call ILP across SIMD lanes / outer loops.
///                 polyfit's `evalBatch` is hardwired to Horner-SIMD today;
///                 the scalar entry stays at `Horner` for the same reason.
/// - `Balanced`  : established no-regression default (`ScalarKernel::Horner`).
enum class EvalPolicy : std::uint8_t { Latency, Throughput, Balanced };

namespace detail {

/// Scalar kernel selection per policy. All three policies route to `Horner`
/// today (see the rev-2 note on `EvalPolicy::Latency`).
template <EvalPolicy /*P*/>
inline constexpr poly_eval::ScalarKernel scalar_kernel_for_policy_v = poly_eval::ScalarKernel::Horner;

/// Block-size override forwarded as the `HYBRID_K` non-type template arg on
/// polyfit's `FuncEval` / `FuncEvalND`. `0` means "use polyfit's heuristic";
/// meaningless under `ScalarKernel::Horner`, which is what every policy
/// resolves to today.
template <EvalPolicy /*P*/, std::size_t /*Degree*/>
inline constexpr std::size_t hybrid_k_for_policy_v = std::size_t{0};

/// Resolves `Func` + `Degree` + `Policy` to the right polyfit evaluator
/// type. 1D inputs use `FuncEval<…,1,…>`; tuple-like inputs go through
/// `FuncEvalND<…>`. Both forward the policy-selected scalar kernel and
/// (when applicable) the policy-chosen `HYBRID_K` block-size override.
template <class Func, std::size_t Degree, EvalPolicy Policy>
using poly_eval_type_for =
    std::conditional_t<poly_eval::detail::hasTupleSize_v<std::remove_cvref_t<poly_eval::fitInput_t<Func>>>,
                       poly_eval::FuncEvalND<Func, Degree, poly_eval::FusionMode::Never,
                                             scalar_kernel_for_policy_v<Policy>, hybrid_k_for_policy_v<Policy, Degree>>,
                       poly_eval::FuncEval<Func, Degree, 1, poly_eval::FusionMode::Never,
                                           scalar_kernel_for_policy_v<Policy>, hybrid_k_for_policy_v<Policy, Degree>>>;

} // namespace detail
} // namespace treeweave

#endif // TREEWEAVE_DETAIL_EVAL_POLICY_HPP
