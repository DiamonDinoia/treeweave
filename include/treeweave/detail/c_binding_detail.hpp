// c_binding_detail.hpp: heavy, internal-linkage half of the C binding.
//
// NO include guard / #includes / namespace by design: textually included
// inside an anonymous namespace in each baseline variant TU so EvalImpl /
// make_eval_impl / wrap_callback — and every treeweave/polyfit template they
// instantiate — get internal linkage. The hot loops are NOT here: EvalImpl
// threads a kernel policy through the `Function` pipeline, either
// `InlineKernels` (single-arch build: full inlining at TREEWEAVE_ARCH) or a
// `KernelSet` of function pointers into the per-ISA kernel TUs (multi-arch
// build, TREEWEAVE_C_KERNELSET defined; see kernels_arch.cpp).

// ---- C-callback wrappers ----------------------------------------------------

/// Scalar (1D→1D) callable: T(T).
template <class T>
struct CFuncScalar {
    c_func_t<T> f;
    void       *data;
    auto        operator()(T x) const -> T {
        T y;
        f(&x, &y, data);
        return y;
    }
};

/// ND callable: array<T,OUT>(array<T,IN>).
template <class T, std::size_t IN, std::size_t OUT>
struct CFuncND {
    c_func_t<T> f;
    void       *data;
    auto        operator()(std::array<T, IN> x) const -> std::array<T, OUT> {
        std::array<T, OUT> y{};
        f(x.data(), y.data(), data);
        return y;
    }
};

/// The callable type polyfit sees for shape (T, IN, OUT):
///   * scalar 1D->1D uses CFuncScalar for polyfit's FuncEval fast path;
///   * everything else uses CFuncND, routing through FuncEvalND.
template <class T, std::size_t IN, std::size_t OUT>
using fit_func_t = std::conditional_t<(IN == 1 && OUT == 1), CFuncScalar<T>, CFuncND<T, IN, OUT>>;

/// Wrap the C callback into the fit callable.
template <class T, std::size_t IN, std::size_t OUT>
auto wrap_callback(c_func_t<T> f, void *data) -> fit_func_t<T, IN, OUT> {
    return {f, data};
}

/// Concrete evaluator owning a fitted `treeweave::Function` of one fixed
/// shape plus the kernel policy `K` threaded through every eval entry.
template <class T, int Deg, std::size_t IN, std::size_t OUT, EvalPolicy Policy, class K>
struct EvalImpl final : IEval<T> {
    static constexpr bool scalar = (IN == 1 && OUT == 1);
    using func_t                 = fit_func_t<T, IN, OUT>;
    using domain_t               = std::conditional_t<scalar, T, std::array<T, IN>>;
    using fn_t                   = treeweave::Function<static_cast<std::size_t>(Deg), func_t, Policy>;

    EvalImpl(fn_t fn, K ks) : fn_(std::move(fn)), ks_(ks) {}

    /// Fit and wrap. May throw treeweave fit exceptions. The caller, the
    /// extern "C" shim, catches them and converts to NULL + last_error.
    static auto create(c_func_t<T> f, void *data, const T *a, const T *b, double tol, const treeweave::options &opts,
                       K ks) -> IEval<T> * {
        domain_t lo{};
        domain_t hi{};
        if constexpr (scalar) {
            lo = a[0];
            hi = b[0];
        } else {
            for (std::size_t i = 0; i < IN; ++i) {
                lo[i] = a[i];
                hi[i] = b[i];
            }
        }
        return new EvalImpl(
            treeweave::fit<static_cast<std::size_t>(Deg), Policy>(wrap_callback<T, IN, OUT>(f, data), lo, hi, tol,
                                                                  opts),
            ks);
    }

    void eval(const T *x, T *y) const override {
        typename fn_t::input_type xi{};
        if constexpr (scalar) {
            xi = x[0];
        } else {
            for (std::size_t i = 0; i < IN; ++i)
                xi[i] = x[i];
        }
        const auto out = fn_(xi, ks_);
        if constexpr (scalar) {
            y[0] = out;
        } else {
            for (std::size_t j = 0; j < OUT; ++j)
                y[j] = out[j];
        }
    }

    void eval_multi(const T *x, T *res, std::size_t n) const override { fn_(x, res, n, {}, ks_); }

    void eval_sorted(const T *x, T *res, std::size_t n) const override {
        if constexpr (IN == 1) {
            fn_.sorted(x, res, n, ks_);
        } else {
            (void)x;
            (void)res;
            (void)n;
            set_last_error("treeweave_sorted: only input_dim == 1 is supported");
        }
    }

    void eval_multi_soa(const T *x, T *const *soa, std::size_t n) const override {
        if constexpr (OUT > 1) {
            std::array<T *, OUT> bufs{};
            for (std::size_t j = 0; j < OUT; ++j)
                bufs[j] = soa[j];
            fn_(x, bufs, n, {}, ks_);
        } else {
            (void)x;
            (void)soa;
            (void)n;
            set_last_error("treeweave_transposed: only output_dim > 1 is supported");
        }
    }

    [[nodiscard]] auto memory_usage() const -> std::size_t override { return fn_.memory_usage(); }

    void print_stats() const override { fn_.print_stats(); }

  private:
    fn_t fn_;
    K    ks_;
};

/// Selects the kernel policy and builds the concrete `EvalImpl` for one fixed
/// shape (value_type, input_dim, degree, output_dim). A free function, not a
/// functor: cppcheck misparses an explicit template argument list on
/// `operator()` and reports constStatement on the call.
template <class T, std::size_t IN, int Deg, int Out>
auto make_eval_impl(c_func_t<T> f, void *data, const T *a, const T *b, double tol,
                    const treeweave::options &opts) -> IEval<T> * {
#ifdef TREEWEAVE_C_KERNELSET
    using K =
        detail::KernelSet<T, IN, static_cast<std::size_t>(Deg), static_cast<std::size_t>(Out)>;
    const K ks = select_kernels<T, IN, static_cast<std::size_t>(Deg), static_cast<std::size_t>(Out)>();
#else
    using K = detail::InlineKernels;
    const K ks{};
#endif
    return EvalImpl<T, Deg, IN, static_cast<std::size_t>(Out), EvalPolicy::Balanced, K>::create(f, data, a, b, tol,
                                                                                                opts, ks);
}
