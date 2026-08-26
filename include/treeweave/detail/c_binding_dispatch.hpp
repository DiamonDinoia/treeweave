// c_binding_dispatch.hpp: definition of make_eval_one<T, IN, OUT>.
//
// No include guard / namespace: included at treeweave::capi scope (NOT inside
// the anonymous namespace) so the instantiated symbol has external linkage
// while still naming the anon-ns EvalFactory. Degree baked to chosen_degree.

template <class T, std::size_t IN, std::size_t OUT>
auto make_eval_one(c_func_t<T> f, void *data, const T *a, const T *b, double tol,
                   const treeweave::options &opts) -> IEval<T> * {
    return EvalFactory<T, IN, chosen_degree>{f, data, a, b, tol, opts}.template operator()<static_cast<int>(OUT)>();
}
