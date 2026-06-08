// c_binding_dispatch.hpp — definition of the per-(arch, dtype, dim, degree)
// factory `make_eval_for`.
//
// No include guard and no namespace of its own: each variant TU includes this
// at `treeweave::capi` scope (NOT inside the anonymous namespace) so the
// instantiated `make_eval_for<Arch, …>` keeps *external* linkage, while its
// body still names the anonymous-namespace `EvalFactory` (visible from the
// enclosing namespace). The variant TU then explicitly instantiates exactly
// one (best_arch, dtype, dim, degree) here.
//
// This is the single site of the output_dim dispatch: an O(1) contiguous poet
// table over {1, 2, 3}. `Arch` is passed to `EvalFactory<Arch,...>` so that
// every poly_eval template instantiation inside EvalImpl carries `Arch` as a
// phantom type, giving each `-march` variant a distinct mangled COMDAT key
// and preventing the linker from folding the four per-arch kernel copies.

template <class Arch, class T, std::size_t IN, int Deg>
auto make_eval_for(int output_dim, c_func_t<T> f, void *data, const T *a, const T *b, double tol,
                   const treeweave::options &opts) -> IEval<T> * {
    return poet::dispatch(EvalFactory<Arch, T, IN, Deg>{f, data, a, b, tol, opts},
                          poet::DispatchParam<poet::make_range<1, 3>>{output_dim});
}
