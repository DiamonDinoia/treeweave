// c_binding_detail.hpp: heavy, internal-linkage half of the C binding.
//
// NO include guard / #includes / namespace by design: textually included
// inside an anonymous namespace in each per-arch variant TU so EvalImpl /
// EvalFactory / wrap_callback get internal linkage and avoid COMDAT folding.
// ArchTagged* carry Arch as a phantom param to make every poly_eval type
// arch-distinct in its mangled name.

// ---- Arch-tagged callable wrappers -----------------------------------------
// Arch phantom → arch-distinct poly_eval mangled names; exactly one non-template operator().

/// Scalar (1D→1D) arch-tagged callable: T(T).
template <class Arch, class T>
struct ArchTaggedScalar {
    c_func_t<T> f;
    void       *data;
    auto        operator()(T x) const -> T {
        T y;
        f(&x, &y, data);
        return y;
    }
};

/// ND arch-tagged callable: array<T,OUT>(array<T,IN>).
template <class Arch, class T, std::size_t IN, std::size_t OUT>
struct ArchTaggedND {
    c_func_t<T> f;
    void       *data;
    auto        operator()(std::array<T, IN> x) const -> std::array<T, OUT> {
        std::array<T, OUT> y{};
        f(x.data(), y.data(), data);
        return y;
    }
};

/// The callable type polyfit sees for shape (Arch, T, IN, OUT):
///   * scalar 1D->1D uses ArchTaggedScalar for polyfit's FuncEval fast path;
///   * everything else uses ArchTaggedND, routing through FuncEvalND.
template <class Arch, class T, std::size_t IN, std::size_t OUT>
using fit_func_t = std::conditional_t<(IN == 1 && OUT == 1), ArchTaggedScalar<Arch, T>, ArchTaggedND<Arch, T, IN, OUT>>;

/// Wrap the C callback into an arch-tagged callable.
template <class Arch, class T, std::size_t IN, std::size_t OUT>
auto wrap_callback(c_func_t<T> f, void *data) -> fit_func_t<Arch, T, IN, OUT> {
    return {f, data};
}

/// Concrete evaluator owning a fitted `treeweave::Function` of one fixed shape.
/// `Arch` phantom makes every poly_eval type inside fn_t arch-distinct.
template <class Arch, class T, int Deg, std::size_t IN, std::size_t OUT, EvalPolicy Policy>
struct EvalImpl final : IEval<T> {
    static constexpr bool scalar = (IN == 1 && OUT == 1);
    using func_t                 = fit_func_t<Arch, T, IN, OUT>;
    using domain_t               = std::conditional_t<scalar, T, std::array<T, IN>>;
    using fn_t                   = treeweave::Function<static_cast<std::size_t>(Deg), func_t, Policy>;

    explicit EvalImpl(fn_t fn) : fn_(std::move(fn)) {}

    /// Fit and wrap. May throw treeweave fit exceptions. The caller, the
    /// extern "C" shim, catches them and converts to NULL + last_error.
    static auto create(c_func_t<T> f, void *data, const T *a, const T *b, double tol, const treeweave::options &opts)
        -> IEval<T> * {
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
        return new EvalImpl(treeweave::fit<static_cast<std::size_t>(Deg), Policy>(
            wrap_callback<Arch, T, IN, OUT>(f, data), lo, hi, tol, opts));
    }

    void eval(const T *x, T *y) const override {
        typename fn_t::input_type xi{};
        if constexpr (scalar) {
            xi = x[0];
        } else {
            for (std::size_t i = 0; i < IN; ++i)
                xi[i] = x[i];
        }
        const auto out = fn_(xi);
        if constexpr (scalar) {
            y[0] = out;
        } else {
            for (std::size_t j = 0; j < OUT; ++j)
                y[j] = out[j];
        }
    }

    void eval_multi(const T *x, T *res, std::size_t n) const override { fn_(x, res, n); }

    void eval_sorted(const T *x, T *res, std::size_t n) const override {
        if constexpr (IN == 1) {
            fn_.sorted(x, res, n);
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
            fn_(x, bufs, n);
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
};

/// poet::dispatch functor for the output_dim switch within one fixed
/// (arch, value_type, input_dim, degree). `operator()<Out>()` builds the
/// concrete `EvalImpl` for the matched output_dim; poet returns nullptr for
/// any output_dim outside the instantiated range.
template <class Arch, class T, std::size_t IN, int Deg>
struct EvalFactory {
    c_func_t<T>               f;
    void                     *data;
    const T                  *a;
    const T                  *b;
    double                    tol;
    const treeweave::options &opts;

    template <int Out>
    auto operator()() const -> IEval<T> * {
        return EvalImpl<Arch, T, Deg, IN, static_cast<std::size_t>(Out), EvalPolicy::Balanced>::create(f, data, a, b,
                                                                                                       tol, opts);
    }
};
