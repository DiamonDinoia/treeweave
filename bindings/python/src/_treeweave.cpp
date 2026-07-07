/* _treeweave.cpp — nanobind bindings for the treeweave C ABI.
 * GIL/callback trampoline + TreeweaveFunction handle.
 */

#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h> // list/tuple <-> std::vector<double> for fit a/b

#include <treeweave.h>

#include <cmath>   // std::numeric_limits
#include <cstring> // std::memcpy
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace nb = nanobind;
using namespace nb::literals;

template <typename T>
struct Traits;

template <>
struct Traits<double> {
    using func_t                             = treeweave_func_t;
    static constexpr double      kNaN        = std::numeric_limits<double>::quiet_NaN();
    static constexpr const char *numpy_dtype = "float64";
    static constexpr const char *fit_name    = "treeweave_fit";

    static treeweave_t fit(func_t f, int id, int od, const double *a, const double *b, double tol, void *ctx,
                           const treeweave_opts *opts) {
        return treeweave_fit(f, id, od, a, b, tol, ctx, opts);
    }
    static void eval(treeweave_t h, const double *x, double *y) { treeweave_eval(h, x, y); }
    static void batch(treeweave_t h, const double *x, double *y, size_t n) { treeweave_batch(h, x, y, n); }
    static void sorted(treeweave_t h, const double *x, double *y, size_t n) { treeweave_sorted(h, x, y, n); }
    static void transposed(treeweave_t h, const double *x, double *const *soa, size_t n) {
        treeweave_transposed(h, x, soa, n);
    }
};

template <>
struct Traits<float> {
    using func_t                             = treeweavef_func_t;
    static constexpr float       kNaN        = std::numeric_limits<float>::quiet_NaN();
    static constexpr const char *numpy_dtype = "float32";
    static constexpr const char *fit_name    = "treeweavef_fit";

    static treeweave_t fit(func_t f, int id, int od, const float *a, const float *b, double tol, void *ctx,
                           const treeweave_opts *opts) {
        return treeweavef_fit(f, id, od, a, b, tol, ctx, opts);
    }
    static void eval(treeweave_t h, const float *x, float *y) { treeweavef_eval(h, x, y); }
    static void batch(treeweave_t h, const float *x, float *y, size_t n) { treeweavef_batch(h, x, y, n); }
    static void sorted(treeweave_t h, const float *x, float *y, size_t n) { treeweavef_sorted(h, x, y, n); }
    static void transposed(treeweave_t h, const float *x, float *const *soa, size_t n) {
        treeweavef_transposed(h, x, soa, n);
    }
};

// Trampoline state (one per treeweave_fit_* call)
template <typename T>
struct TrampolineState {
    nb::object callable;
    int        input_dim;
    int        output_dim;
    bool       errored = false;
    // Stash for the Python exception: saved/restored via PyErr_GetRaisedException
    // (Python 3.12+) or the older PyErr_Fetch/Restore on 3.9-3.11.
    PyObject *exc = nullptr; // owned reference
};

template <typename T>
static void stash_exception(TrampolineState<T> *st) {
#if PY_VERSION_HEX >= 0x030c0000
    st->exc = PyErr_GetRaisedException(); // steals reference
#else
    PyObject *tp = nullptr, *val = nullptr, *tb = nullptr;
    PyErr_Fetch(&tp, &val, &tb);
    PyErr_NormalizeException(&tp, &val, &tb);
    if (tb)
        PyException_SetTraceback(val, tb);
    Py_XDECREF(tp);
    Py_XDECREF(tb);
    st->exc = val; // own the ref
#endif
}

template <typename T>
static void trampoline(const T *x, T *y, void *data) {
    TrampolineState<T> *st = static_cast<TrampolineState<T> *>(data);

    if (st->errored) {
        for (int i = 0; i < st->output_dim; ++i)
            y[i] = Traits<T>::kNaN;
        return;
    }

    nb::gil_scoped_acquire gil;

    try {
        // Wrap x as a (input_dim,) NumPy view — zero-copy, read-only.
        nb::ndarray<const T, nb::numpy, nb::shape<-1>, nb::c_contig, nb::device::cpu> xarr(const_cast<T *>(x),
                                                                                           {(size_t)st->input_dim});
        nb::object                                                                    result = st->callable(xarr);

        if (st->output_dim == 1) {
            y[0] = nb::cast<T>(result);
        } else {
            nb::ndarray<const T, nb::numpy, nb::shape<-1>, nb::c_contig, nb::device::cpu> rarr =
                nb::cast<nb::ndarray<const T, nb::numpy, nb::shape<-1>, nb::c_contig, nb::device::cpu>>(result);
            if ((int)rarr.shape(0) != st->output_dim)
                throw std::runtime_error("callback returned wrong number of outputs");
            const T *rp = rarr.data();
            for (int i = 0; i < st->output_dim; ++i)
                y[i] = rp[i];
        }
    } catch (nb::python_error &e) {
        st->errored = true;
        e.restore(); // put exception back into interpreter state
        stash_exception(st);
        for (int i = 0; i < st->output_dim; ++i)
            y[i] = Traits<T>::kNaN;
    } catch (std::exception &e) {
        st->errored = true;
        // Convert to a Python RuntimeError and stash it.
        PyErr_SetString(PyExc_RuntimeError, e.what());
        stash_exception(st);
        for (int i = 0; i < st->output_dim; ++i)
            y[i] = Traits<T>::kNaN;
    }
}

// Helper: raise the stashed exception (called after fit returns on error).
// The stashed exc object is consumed (reference stolen by PyErr_SetRaisedException
// / PyErr_Restore).
[[noreturn]] static void raise_stashed(PyObject *exc) {
#if PY_VERSION_HEX >= 0x030c0000
    PyErr_SetRaisedException(exc); // steals reference
#else
    // Restore the original exception preserving its exact type.
    // exc is already normalised and tb is already attached (see trampoline).
    PyObject *tp = reinterpret_cast<PyObject *>(Py_TYPE(exc));
    Py_INCREF(tp);
    PyErr_Restore(tp, exc, nullptr); // steals tp and exc
#endif
    throw nb::python_error();
}

class TreeweaveFunction {
  public:
    // Takes ownership of the handle; destructor calls treeweave_free.
    explicit TreeweaveFunction(treeweave_t h) : handle_(h) {}

    ~TreeweaveFunction() {
        if (handle_)
            handle_ = treeweave_free(handle_);
    }

    TreeweaveFunction(const TreeweaveFunction &)            = delete;
    TreeweaveFunction &operator=(const TreeweaveFunction &) = delete;

    int         input_dim() const { return treeweave_input_dim(handle_); }
    int         output_dim() const { return treeweave_output_dim(handle_); }
    size_t      memory_usage() const { return treeweave_memory_usage(handle_); }
    int         dtype_tag() const { return (int)treeweave_dtype(handle_); }
    std::string dtype_str() const { return (treeweave_dtype(handle_) == TREEWEAVE_F64) ? "f64" : "f32"; }
    void        print_stats() const { treeweave_print_stats(handle_); }

    nb::object eval_one(nb::object x_obj) const {
        const int id = input_dim(), od = output_dim();
        if (treeweave_dtype(handle_) == TREEWEAVE_F32)
            return _eval_one<float>(x_obj, id, od);
        return _eval_one<double>(x_obj, id, od);
    }

    // x: (N, input_dim) or (N,) for 1D. Returns (N, output_dim) or (N,) for
    // od==1. The result array is allocated up front (or supplied via `out`) and
    // the C eval writes straight into its buffer — no intermediate copy.
    nb::object eval_multi_py(nb::object x_obj, nb::object out_obj) const {
        const int id = input_dim(), od = output_dim();
        if (treeweave_dtype(handle_) == TREEWEAVE_F32)
            return _eval_batch<float, &Traits<float>::batch>(x_obj, out_obj, id, od);
        return _eval_batch<double, &Traits<double>::batch>(x_obj, out_obj, id, od);
    }

    nb::object eval_sorted_py(nb::object x_obj, nb::object out_obj) const {
        if (input_dim() != 1)
            throw std::runtime_error("eval_sorted requires input_dim == 1");
        const int od = output_dim();
        if (treeweave_dtype(handle_) == TREEWEAVE_F32)
            return _eval_batch<float, &Traits<float>::sorted>(x_obj, out_obj, 1, od);
        return _eval_batch<double, &Traits<double>::sorted>(x_obj, out_obj, 1, od);
    }

    nb::object eval_multi_soa_py(nb::object x_obj) const {
        if (output_dim() < 2)
            throw std::runtime_error("eval_multi_soa requires output_dim >= 2");
        const int id = input_dim(), od = output_dim();
        if (treeweave_dtype(handle_) == TREEWEAVE_F32)
            return _eval_soa<float>(x_obj, id, od);
        return _eval_soa<double>(x_obj, id, od);
    }

  private:
    treeweave_t handle_;

    template <typename T>
    using Array = nb::ndarray<const T, nb::numpy, nb::c_contig, nb::device::cpu>;
    template <typename T>
    using ArrayMut = nb::ndarray<T, nb::numpy, nb::c_contig, nb::device::cpu>;

    // Accept any array-like; ndim may be 1 (for id==1) or 2 (for id>1).
    template <typename T>
    static Array<T> _coerce_input(nb::object x_obj) {
        auto np = nb::module_::import_("numpy");
        return nb::cast<Array<T>>(np.attr("ascontiguousarray")(x_obj, np.attr(Traits<T>::numpy_dtype)));
    }

    template <typename T>
    static void _fill_x(nb::object x, T *out, int id) {
        if (id == 1) {
            out[0] = nb::cast<T>(x);
        } else {
            auto np  = nb::module_::import_("numpy");
            auto arr = nb::cast<Array<T>>(np.attr("asarray")(x, np.attr(Traits<T>::numpy_dtype)));
            for (int i = 0; i < id; ++i)
                out[i] = arr.data()[i];
        }
    }

    template <typename T>
    static nb::object _make_array(const T *data, int n) {
        auto       np  = nb::module_::import_("numpy");
        nb::object arr = np.attr("empty")(n, "dtype"_a = np.attr(Traits<T>::numpy_dtype));
        T         *dst = nb::cast<ArrayMut<T>>(arr).data();
        std::memcpy(dst, data, n * sizeof(T));
        return arr;
    }

    // Resolve the output array for a batch/sorted eval: allocate a fresh (N,) /
    // (N, od) array, or validate and reuse a caller-supplied `out`. Returns the
    // array (kept alive by the caller) and a raw pointer the C eval writes into
    // directly — no intermediate buffer or copy.
    template <typename T>
    static std::pair<nb::object, T *> _output_buffer(nb::object out_obj, size_t n, int od) {
        if (out_obj.is_none()) {
            auto       np  = nb::module_::import_("numpy");
            nb::object arr = (od == 1) ? np.attr("empty")(n, "dtype"_a = np.attr(Traits<T>::numpy_dtype))
                                       : np.attr("empty")(nb::make_tuple(static_cast<int>(n), od),
                                                          "dtype"_a = np.attr(Traits<T>::numpy_dtype));
            return {arr, nb::cast<ArrayMut<T>>(arr).data()};
        }
        // out= must already be the right type/layout: convert=false so we never
        // silently write into a throwaway converted copy instead of the caller's.
        ArrayMut<T> arr;
        if (!nb::try_cast<ArrayMut<T>>(out_obj, arr, /*convert=*/false))
            throw std::invalid_argument(std::string("out= must be a contiguous, C-ordered ") + Traits<T>::numpy_dtype +
                                        " NumPy array");
        if (arr.size() != n * static_cast<size_t>(od))
            throw std::invalid_argument("out= has the wrong number of elements for this batch");
        return {out_obj, arr.data()};
    }

    template <typename T>
    nb::object _eval_one(nb::object x_obj, int id, int od) const {
        std::vector<T> xv(id), yv(od);
        _fill_x<T>(x_obj, xv.data(), id);
        Traits<T>::eval(handle_, xv.data(), yv.data());
        if (od == 1)
            return nb::cast(yv[0]);
        return _make_array<T>(yv.data(), od);
    }

    // Shared impl for batch and sorted (they share the same signature).
    template <typename T, void (*BatchFn)(treeweave_t, const T *, T *, size_t)>
    nb::object _eval_batch(nb::object x_obj, nb::object out_obj, int /*id*/, int od) const {
        auto         x_arr = _coerce_input<T>(x_obj);
        const size_t n     = x_arr.shape(0);
        auto [arr, dst]    = _output_buffer<T>(out_obj, n, od);
        { // pure-C eval: release the GIL so other threads can run
            nb::gil_scoped_release nogil;
            BatchFn(handle_, x_arr.data(), dst, n);
        }
        return arr;
    }

    template <typename T>
    nb::object _eval_soa(nb::object x_obj, int id, int od) const {
        auto                        x_arr = _coerce_input<T>(x_obj);
        size_t                      n     = x_arr.shape(0);
        std::vector<std::vector<T>> comps(od, std::vector<T>(n));
        std::vector<T *>            soa(od);
        for (int d = 0; d < od; ++d)
            soa[d] = comps[d].data();
        {
            nb::gil_scoped_release nogil;
            Traits<T>::transposed(handle_, x_arr.data(), soa.data(), n);
        }
        nb::list out;
        for (int d = 0; d < od; ++d)
            out.append(_make_array<T>(comps[d].data(), (int)n));
        return out;
    }
};

template <typename T>
static nb::object fit_impl(nb::object callable, int input_dim, int output_dim, std::vector<T> a, std::vector<T> b,
                           double tol, int tol_kind_int, int max_depth, int max_memory_mib, int allow_max_depth_leaves,
                           int min_uniform_depth) {
    TrampolineState<T> st;
    st.callable   = callable;
    st.input_dim  = input_dim;
    st.output_dim = output_dim;

    treeweave_opts opts;
    treeweave_default_opts(&opts);
    opts.tol_kind               = (treeweave_tol_kind_t)tol_kind_int;
    opts.max_depth              = max_depth;
    opts.max_memory_mib         = max_memory_mib;
    opts.allow_max_depth_leaves = allow_max_depth_leaves;
    opts.min_uniform_depth      = min_uniform_depth;

    // Python carries state in the trampoline's &st context; opts trails last.
    treeweave_t handle = Traits<T>::fit(&trampoline<T>, input_dim, output_dim, a.data(), b.data(), tol, &st, &opts);

    if (st.errored) {
        if (handle)
            handle = treeweave_free(handle); // shouldn't happen but be safe
        if (st.exc)
            raise_stashed(st.exc);
        throw std::runtime_error("treeweave callback failed (no exception stashed)");
    }

    if (!handle) {
        const char *msg = treeweave_last_error();
        throw std::runtime_error(msg && *msg ? msg : std::string(Traits<T>::fit_name) + " returned NULL");
    }

    return nb::cast(new TreeweaveFunction(handle), nb::rv_policy::take_ownership);
}

static nb::object fit_f64(nb::object callable, int input_dim, int output_dim, std::vector<double> a,
                          std::vector<double> b, double tol, int tol_kind_int, int max_depth, int max_memory_mib,
                          int allow_max_depth_leaves, int min_uniform_depth) {
    return fit_impl<double>(callable, input_dim, output_dim, std::move(a), std::move(b), tol, tol_kind_int, max_depth,
                            max_memory_mib, allow_max_depth_leaves, min_uniform_depth);
}

static nb::object fit_f32(nb::object callable, int input_dim, int output_dim, std::vector<float> a,
                          std::vector<float> b, double tol, int tol_kind_int, int max_depth, int max_memory_mib,
                          int allow_max_depth_leaves, int min_uniform_depth) {
    return fit_impl<float>(callable, input_dim, output_dim, std::move(a), std::move(b), tol, tol_kind_int, max_depth,
                           max_memory_mib, allow_max_depth_leaves, min_uniform_depth);
}

NB_MODULE(_treeweave, m) {
    m.doc() = "Low-level nanobind bindings for the treeweave C ABI.";

    // Enum mirrors
    nb::enum_<treeweave_tol_kind_t>(m, "TolKind")
        .value("RELATIVE_TAIL", TREEWEAVE_RELATIVE_TAIL)
        .value("ABSOLUTE_TAIL", TREEWEAVE_ABSOLUTE_TAIL)
        .value("RELATIVE_MAX", TREEWEAVE_RELATIVE_MAX)
        .value("ABSOLUTE_MAX", TREEWEAVE_ABSOLUTE_MAX)
        .value("RELATIVE_L2", TREEWEAVE_RELATIVE_L2)
        .value("ABSOLUTE_L2", TREEWEAVE_ABSOLUTE_L2);

    nb::enum_<treeweave_dtype_t>(m, "DType").value("F64", TREEWEAVE_F64).value("F32", TREEWEAVE_F32);

    nb::class_<TreeweaveFunction>(m, "TreeweaveFunction",
                                  "Opaque handle to a fitted treeweave function. Do not construct directly;"
                                  " use _treeweave.fit_f64() or _treeweave.fit_f32().")
        .def("eval_one", &TreeweaveFunction::eval_one, "Evaluate at a single point (scalar or length-dim sequence).",
             "x"_a)
        .def("eval_multi", &TreeweaveFunction::eval_multi_py, "AoS batch eval. x: (N, dim) or (N,) for 1D.", "x"_a,
             "out"_a = nb::none())
        .def("sorted", &TreeweaveFunction::eval_sorted_py, "Sorted-1D batch eval (input_dim==1).", "x"_a,
             "out"_a = nb::none())
        .def("eval_multi_soa", &TreeweaveFunction::eval_multi_soa_py,
             "SoA batch eval. Returns list of (N,) arrays, one per output component.", "x"_a)
        .def("print_stats", &TreeweaveFunction::print_stats)
        .def_prop_ro("input_dim", &TreeweaveFunction::input_dim)
        .def_prop_ro("output_dim", &TreeweaveFunction::output_dim)
        .def_prop_ro("memory_usage", &TreeweaveFunction::memory_usage)
        .def_prop_ro("dtype", &TreeweaveFunction::dtype_str);

    m.def("fit_f64", &fit_f64, "callable"_a, "input_dim"_a, "output_dim"_a, "a"_a, "b"_a, "tol"_a, "tol_kind"_a,
          "max_depth"_a, "max_memory_mib"_a, "allow_max_depth_leaves"_a, "min_uniform_depth"_a);

    m.def("fit_f32", &fit_f32, "callable"_a, "input_dim"_a, "output_dim"_a, "a"_a, "b"_a, "tol"_a, "tol_kind"_a,
          "max_depth"_a, "max_memory_mib"_a, "allow_max_depth_leaves"_a, "min_uniform_depth"_a);
}
