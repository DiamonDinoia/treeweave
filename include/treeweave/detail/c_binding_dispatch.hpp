// c_binding_dispatch.hpp: definition of make_eval_one<T, IN, OUT>.
//
// No include guard / namespace: included at treeweave::capi scope (NOT inside
// the anonymous namespace) so the instantiated symbol has external linkage
// while still naming the anon-ns make_eval_impl. Degree baked to chosen_degree.

template <class T, std::size_t IN, std::size_t OUT>
auto make_eval_one(c_func_t<T> f, void *data, const T *a, const T *b, double tol,
                   const treeweave::options &opts) -> IEval<T> * {
    return make_eval_impl<T, IN, chosen_degree, static_cast<int>(OUT)>(f, data, a, b, tol, opts);
}
