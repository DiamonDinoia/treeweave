// c_binding_dispatch.hpp — definition of make_eval_for<Arch, T, IN, Deg>.
//
// No include guard / namespace: included at treeweave::capi scope (NOT inside
// the anonymous namespace) so the instantiated symbol has external linkage
// while still naming the anon-ns EvalFactory. poet::dispatch over output_dim
// {1,2,3}; Arch phantom keeps each arch's COMDAT key distinct.

template <class Arch, class T, std::size_t IN, int Deg>
auto make_eval_for(int output_dim, c_func_t<T> f, void *data, const T *a, const T *b, double tol,
                   const treeweave::options &opts) -> IEval<T> * {
    return poet::dispatch(EvalFactory<Arch, T, IN, Deg>{f, data, a, b, tol, opts},
                          poet::dispatch_param<poet::inclusive_range<1, 3>>{output_dim});
}
