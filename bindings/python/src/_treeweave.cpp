/* _treeweave.cpp — nanobind bindings for the treeweave C ABI.
 *
 * Architecture:
 *   - A typed trampoline (f64 and f32 variants) bridges the C callback into
 *     a Python callable. The trampoline is always called on the thread that
 *     called Python, which already holds the GIL; we re-acquire it
 *     defensively with nb::gil_scoped_acquire (re-entrant, effectively a
 *     no-op when the GIL is already held by this thread).
 *   - On callback failure we latch the error flag, stash the live Python
 *     exception, fill y[] with NaN, and short-circuit all subsequent
 *     invocations so the C fit drains quickly without touching Python again.
 *   - After treeweave_fit_* returns we re-raise the stashed exception (via
 *     nb::raise_python_error) so the original Python exception propagates
 *     intact to the caller.
 *   - TreeweaveFunction wraps a treeweave_t handle and exposes eval / eval_multi /
 *     sorted / eval_multi_soa methods plus introspection properties. The C++
 *     destructor calls treeweave_free, so Python GC suffices for cleanup.
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
#include <vector>

namespace nb = nanobind;
using namespace nb::literals;

// ---------------------------------------------------------------------------
// Trampoline state (one per treeweave_fit_* call)
// ---------------------------------------------------------------------------

struct TrampolineState64 {
    nb::object callable;
    int        input_dim;
    int        output_dim;
    bool       errored = false;
    // Stash for the Python exception: we save/restore via PyErr_GetRaisedException
    // (Python 3.12+) or the older PyErr_Fetch/Restore on 3.9-3.11.
    PyObject *exc = nullptr; // borrowed reference stored as owned
};

struct TrampolineState32 {
    nb::object callable;
    int        input_dim;
    int        output_dim;
    bool       errored = false;
    PyObject  *exc     = nullptr;
};

static constexpr double kNaN64 = std::numeric_limits<double>::quiet_NaN();
static constexpr float  kNaN32 = std::numeric_limits<float>::quiet_NaN();

// ---------------------------------------------------------------------------
// f64 trampoline
// ---------------------------------------------------------------------------
static void trampoline_f64(const double *x, double *y, void *data) {
    TrampolineState64 *st = static_cast<TrampolineState64 *>(data);

    // Short-circuit: a previous callback already failed.
    if (st->errored) {
        for (int i = 0; i < st->output_dim; ++i)
            y[i] = kNaN64;
        return;
    }

    nb::gil_scoped_acquire gil;

    try {
        // Wrap x as a (input_dim,) NumPy view — zero-copy, read-only.
        // nb::ndarray with shape: the array does NOT own the data.
        nb::ndarray<const double, nb::numpy, nb::shape<-1>, nb::c_contig, nb::device::cpu> xarr(
            const_cast<double *>(x), {(size_t)st->input_dim});

        nb::object result = st->callable(xarr);

        if (st->output_dim == 1) {
            // Scalar or 0-d array
            y[0] = nb::cast<double>(result);
        } else {
            // Expect a sequence of length output_dim
            nb::ndarray<const double, nb::numpy, nb::shape<-1>, nb::c_contig, nb::device::cpu> rarr =
                nb::cast<nb::ndarray<const double, nb::numpy, nb::shape<-1>, nb::c_contig, nb::device::cpu>>(result);
            if ((int)rarr.shape(0) != st->output_dim)
                throw std::runtime_error("callback returned wrong number of outputs");
            const double *rp = rarr.data();
            for (int i = 0; i < st->output_dim; ++i)
                y[i] = rp[i];
        }
    } catch (nb::python_error &e) {
        st->errored = true;
        e.restore(); // put exception back into interpreter state
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
        for (int i = 0; i < st->output_dim; ++i)
            y[i] = kNaN64;
    } catch (std::exception &e) {
        st->errored = true;
        // Convert to a Python RuntimeError and stash it.
        PyErr_SetString(PyExc_RuntimeError, e.what());
#if PY_VERSION_HEX >= 0x030c0000
        st->exc = PyErr_GetRaisedException();
#else
        PyObject *tp = nullptr, *val = nullptr, *tb = nullptr;
        PyErr_Fetch(&tp, &val, &tb);
        PyErr_NormalizeException(&tp, &val, &tb);
        Py_XDECREF(tp);
        Py_XDECREF(tb);
        st->exc = val;
#endif
        for (int i = 0; i < st->output_dim; ++i)
            y[i] = kNaN64;
    }
}

// ---------------------------------------------------------------------------
// f32 trampoline
// ---------------------------------------------------------------------------
static void trampoline_f32(const float *x, float *y, void *data) {
    TrampolineState32 *st = static_cast<TrampolineState32 *>(data);

    if (st->errored) {
        for (int i = 0; i < st->output_dim; ++i)
            y[i] = kNaN32;
        return;
    }

    nb::gil_scoped_acquire gil;

    try {
        nb::ndarray<const float, nb::numpy, nb::shape<-1>, nb::c_contig, nb::device::cpu> xarr(const_cast<float *>(x),
                                                                                               {(size_t)st->input_dim});

        nb::object result = st->callable(xarr);

        if (st->output_dim == 1) {
            y[0] = nb::cast<float>(result);
        } else {
            nb::ndarray<const float, nb::numpy, nb::shape<-1>, nb::c_contig, nb::device::cpu> rarr =
                nb::cast<nb::ndarray<const float, nb::numpy, nb::shape<-1>, nb::c_contig, nb::device::cpu>>(result);
            if ((int)rarr.shape(0) != st->output_dim)
                throw std::runtime_error("callback returned wrong number of outputs");
            const float *rp = rarr.data();
            for (int i = 0; i < st->output_dim; ++i)
                y[i] = rp[i];
        }
    } catch (nb::python_error &e) {
        st->errored = true;
        e.restore();
#if PY_VERSION_HEX >= 0x030c0000
        st->exc = PyErr_GetRaisedException();
#else
        PyObject *tp = nullptr, *val = nullptr, *tb = nullptr;
        PyErr_Fetch(&tp, &val, &tb);
        PyErr_NormalizeException(&tp, &val, &tb);
        if (tb)
            PyException_SetTraceback(val, tb);
        Py_XDECREF(tp);
        Py_XDECREF(tb);
        st->exc = val;
#endif
        for (int i = 0; i < st->output_dim; ++i)
            y[i] = kNaN32;
    } catch (std::exception &e) {
        st->errored = true;
        PyErr_SetString(PyExc_RuntimeError, e.what());
#if PY_VERSION_HEX >= 0x030c0000
        st->exc = PyErr_GetRaisedException();
#else
        PyObject *tp = nullptr, *val = nullptr, *tb = nullptr;
        PyErr_Fetch(&tp, &val, &tb);
        PyErr_NormalizeException(&tp, &val, &tb);
        Py_XDECREF(tp);
        Py_XDECREF(tb);
        st->exc = val;
#endif
        for (int i = 0; i < st->output_dim; ++i)
            y[i] = kNaN32;
    }
}

// ---------------------------------------------------------------------------
// Helper: raise the stashed exception (called after fit returns on error).
// The stashed exc object is consumed (reference stolen by PyErr_SetRaisedException
// / PyErr_Restore).
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// TreeweaveFunction — bound C++ class holding a treeweave_t handle.
// ---------------------------------------------------------------------------
class TreeweaveFunction {
  public:
    // Takes ownership of the handle; destructor calls treeweave_free.
    explicit TreeweaveFunction(treeweave_t h) : handle_(h) {}

    ~TreeweaveFunction() {
        if (handle_)
            handle_ = treeweave_free(handle_);
    }

    // Non-copyable, moveable.
    TreeweaveFunction(const TreeweaveFunction &)            = delete;
    TreeweaveFunction &operator=(const TreeweaveFunction &) = delete;

    // ---- introspection ---------------------------------------------------
    int         input_dim() const { return treeweave_input_dim(handle_); }
    int         output_dim() const { return treeweave_output_dim(handle_); }
    size_t      memory_usage() const { return treeweave_memory_usage(handle_); }
    int         dtype_tag() const { return (int)treeweave_dtype(handle_); }
    std::string dtype_str() const { return (treeweave_dtype(handle_) == TREEWEAVE_F64) ? "f64" : "f32"; }
    void        print_stats() const { treeweave_print_stats(handle_); }

    // ---- scalar eval (1 point) ------------------------------------------
    nb::object eval_one(nb::object x_obj) const {
        const int id     = input_dim();
        const int od     = output_dim();
        bool      is_f32 = (treeweave_dtype(handle_) == TREEWEAVE_F32);

        if (is_f32) {
            std::vector<float> xv(id), yv(od);
            _fill_x_f32(x_obj, xv.data(), id);
            treeweavef_eval(handle_, xv.data(), yv.data());
            if (od == 1)
                return nb::cast(yv[0]);
            return _make_f32_array(yv.data(), od);
        } else {
            std::vector<double> xv(id), yv(od);
            _fill_x_f64(x_obj, xv.data(), id);
            treeweave_eval(handle_, xv.data(), yv.data());
            if (od == 1)
                return nb::cast(yv[0]);
            return _make_f64_array(yv.data(), od);
        }
    }

    // ---- AoS batch eval --------------------------------------------------
    // x: (N, input_dim) or (N,) for 1D. Returns (N, output_dim) or (N,) for od==1.
    nb::object eval_multi_py(nb::object x_obj) const {
        const int id     = input_dim();
        const int od     = output_dim();
        bool      is_f32 = (treeweave_dtype(handle_) == TREEWEAVE_F32);

        if (is_f32) {
            auto               x_arr = _coerce_input_f32(x_obj, id);
            size_t             n     = x_arr.shape(0);
            std::vector<float> res(n * od);
            { // pure-C eval: release the GIL so other threads can run
                nb::gil_scoped_release nogil;
                treeweavef_batch(handle_, x_arr.data(), res.data(), n);
            }
            return _make_output_f32(res.data(), n, od);
        } else {
            auto                x_arr = _coerce_input_f64(x_obj, id);
            size_t              n     = x_arr.shape(0);
            std::vector<double> res(n * od);
            {
                nb::gil_scoped_release nogil;
                treeweave_batch(handle_, x_arr.data(), res.data(), n);
            }
            return _make_output_f64(res.data(), n, od);
        }
    }

    // ---- sorted 1D eval --------------------------------------------------
    nb::object eval_sorted_py(nb::object x_obj) const {
        const int id = input_dim();
        const int od = output_dim();
        if (id != 1)
            throw std::runtime_error("eval_sorted requires input_dim == 1");

        bool is_f32 = (treeweave_dtype(handle_) == TREEWEAVE_F32);

        if (is_f32) {
            auto               x_arr = _coerce_input_f32(x_obj, 1);
            size_t             n     = x_arr.shape(0);
            std::vector<float> res(n * od);
            {
                nb::gil_scoped_release nogil;
                treeweavef_sorted(handle_, x_arr.data(), res.data(), n);
            }
            return _make_output_f32(res.data(), n, od);
        } else {
            auto                x_arr = _coerce_input_f64(x_obj, 1);
            size_t              n     = x_arr.shape(0);
            std::vector<double> res(n * od);
            {
                nb::gil_scoped_release nogil;
                treeweave_sorted(handle_, x_arr.data(), res.data(), n);
            }
            return _make_output_f64(res.data(), n, od);
        }
    }

    // ---- SoA batch eval --------------------------------------------------
    // Returns a list of (N,) arrays, one per output component.
    nb::object eval_multi_soa_py(nb::object x_obj) const {
        const int id = input_dim();
        const int od = output_dim();
        if (od < 2)
            throw std::runtime_error("eval_multi_soa requires output_dim >= 2");

        bool is_f32 = (treeweave_dtype(handle_) == TREEWEAVE_F32);

        if (is_f32) {
            auto   x_arr = _coerce_input_f32(x_obj, id);
            size_t n     = x_arr.shape(0);
            // Allocate component buffers
            std::vector<std::vector<float>> comps(od, std::vector<float>(n));
            std::vector<float *>            soa(od);
            for (int d = 0; d < od; ++d)
                soa[d] = comps[d].data();
            {
                nb::gil_scoped_release nogil;
                treeweavef_transposed(handle_, x_arr.data(), soa.data(), n);
            }
            nb::list out;
            for (int d = 0; d < od; ++d)
                out.append(_make_f32_array(comps[d].data(), (int)n));
            return out;
        } else {
            auto                             x_arr = _coerce_input_f64(x_obj, id);
            size_t                           n     = x_arr.shape(0);
            std::vector<std::vector<double>> comps(od, std::vector<double>(n));
            std::vector<double *>            soa(od);
            for (int d = 0; d < od; ++d)
                soa[d] = comps[d].data();
            {
                nb::gil_scoped_release nogil;
                treeweave_transposed(handle_, x_arr.data(), soa.data(), n);
            }
            nb::list out;
            for (int d = 0; d < od; ++d)
                out.append(_make_f64_array(comps[d].data(), (int)n));
            return out;
        }
    }

  private:
    treeweave_t handle_;

    // ---- helpers: coerce input to contiguous C-order float/double array --

    using F64Array = nb::ndarray<const double, nb::numpy, nb::c_contig, nb::device::cpu>;
    using F32Array = nb::ndarray<const float, nb::numpy, nb::c_contig, nb::device::cpu>;
    // Mutable views, used when writing into a freshly-allocated output array.
    using F64ArrayMut = nb::ndarray<double, nb::numpy, nb::c_contig, nb::device::cpu>;
    using F32ArrayMut = nb::ndarray<float, nb::numpy, nb::c_contig, nb::device::cpu>;

    static F64Array _coerce_input_f64(nb::object x_obj, int id) {
        // Accept any array-like; ndim may be 1 (for id==1) or 2 (for id>1).
        return nb::cast<F64Array>(nb::module_::import_("numpy").attr("ascontiguousarray")(
            x_obj, nb::module_::import_("numpy").attr("float64")));
    }

    static F32Array _coerce_input_f32(nb::object x_obj, int id) {
        return nb::cast<F32Array>(nb::module_::import_("numpy").attr("ascontiguousarray")(
            x_obj, nb::module_::import_("numpy").attr("float32")));
    }

    // Fill a plain C array of `id` doubles from a Python scalar or 1-D sequence.
    static void _fill_x_f64(nb::object x, double *out, int id) {
        if (id == 1) {
            out[0] = nb::cast<double>(x);
        } else {
            auto arr = nb::cast<F64Array>(
                nb::module_::import_("numpy").attr("asarray")(x, nb::module_::import_("numpy").attr("float64")));
            for (int i = 0; i < id; ++i)
                out[i] = arr.data()[i];
        }
    }
    static void _fill_x_f32(nb::object x, float *out, int id) {
        if (id == 1) {
            out[0] = nb::cast<float>(x);
        } else {
            auto arr = nb::cast<F32Array>(
                nb::module_::import_("numpy").attr("asarray")(x, nb::module_::import_("numpy").attr("float32")));
            for (int i = 0; i < id; ++i)
                out[i] = arr.data()[i];
        }
    }

    // ---- helpers: wrap raw buffer as a NumPy array ----------------------

    // 1-D array of n doubles (owned copy).
    static nb::object _make_f64_array(const double *data, int n) {
        auto       np  = nb::module_::import_("numpy");
        nb::object arr = np.attr("empty")(n, "dtype"_a = np.attr("float64"));
        double    *dst = nb::cast<F64ArrayMut>(arr).data();
        std::memcpy(dst, data, n * sizeof(double));
        return arr;
    }
    static nb::object _make_f32_array(const float *data, int n) {
        auto       np  = nb::module_::import_("numpy");
        nb::object arr = np.attr("empty")(n, "dtype"_a = np.attr("float32"));
        float     *dst = nb::cast<F32ArrayMut>(arr).data();
        std::memcpy(dst, data, n * sizeof(float));
        return arr;
    }

    // Output array: (N,) for od==1, (N, od) for od>1.
    static nb::object _make_output_f64(const double *data, size_t n, int od) {
        auto np = nb::module_::import_("numpy");
        if (od == 1) {
            nb::object arr = np.attr("empty")(n, "dtype"_a = np.attr("float64"));
            double    *dst = nb::cast<F64ArrayMut>(arr).data();
            std::memcpy(dst, data, n * sizeof(double));
            return arr;
        } else {
            nb::object shape = nb::make_tuple((int)n, od);
            nb::object arr   = np.attr("empty")(shape, "dtype"_a = np.attr("float64"));
            double    *dst   = nb::cast<F64ArrayMut>(arr).data();
            std::memcpy(dst, data, n * od * sizeof(double));
            return arr;
        }
    }
    static nb::object _make_output_f32(const float *data, size_t n, int od) {
        auto np = nb::module_::import_("numpy");
        if (od == 1) {
            nb::object arr = np.attr("empty")(n, "dtype"_a = np.attr("float32"));
            float     *dst = nb::cast<F32ArrayMut>(arr).data();
            std::memcpy(dst, data, n * sizeof(float));
            return arr;
        } else {
            nb::object shape = nb::make_tuple((int)n, od);
            nb::object arr   = np.attr("empty")(shape, "dtype"_a = np.attr("float32"));
            float     *dst   = nb::cast<F32ArrayMut>(arr).data();
            std::memcpy(dst, data, n * od * sizeof(float));
            return arr;
        }
    }
};

// ---------------------------------------------------------------------------
// fit_f64 / fit_f32 — called from Python's treeweave.fit()
// ---------------------------------------------------------------------------

static nb::object fit_f64(nb::object callable, int input_dim, int output_dim, std::vector<double> a,
                          std::vector<double> b, double tol, int tol_kind_int, int max_depth, int max_memory_mib,
                          int allow_max_depth_leaves, int min_uniform_depth) {
    TrampolineState64 st;
    st.callable   = callable;
    st.input_dim  = input_dim;
    st.output_dim = output_dim;

    treeweave_opts opts         = treeweave_default_opts;
    opts.tol_kind               = (treeweave_tol_kind_t)tol_kind_int;
    opts.max_depth              = max_depth;
    opts.max_memory_mib         = max_memory_mib;
    opts.allow_max_depth_leaves = allow_max_depth_leaves;
    opts.min_uniform_depth      = min_uniform_depth;

    // Python carries state in the trampoline's &st context; opts trails last.
    treeweave_t handle = treeweave_fit(trampoline_f64, input_dim, output_dim, a.data(), b.data(), tol, &st, &opts);

    // Handle callback exception first (takes priority).
    if (st.errored) {
        if (handle)
            handle = treeweave_free(handle); // shouldn't happen but be safe
        if (st.exc)
            raise_stashed(st.exc);
        throw std::runtime_error("treeweave callback failed (no exception stashed)");
    }

    if (!handle) {
        const char *msg = treeweave_last_error();
        throw std::runtime_error(msg && *msg ? msg : "treeweave_fit returned NULL");
    }

    return nb::cast(new TreeweaveFunction(handle), nb::rv_policy::take_ownership);
}

static nb::object fit_f32(nb::object callable, int input_dim, int output_dim, std::vector<float> a,
                          std::vector<float> b, double tol, int tol_kind_int, int max_depth, int max_memory_mib,
                          int allow_max_depth_leaves, int min_uniform_depth) {
    TrampolineState32 st;
    st.callable   = callable;
    st.input_dim  = input_dim;
    st.output_dim = output_dim;

    treeweave_opts opts         = treeweave_default_opts;
    opts.tol_kind               = (treeweave_tol_kind_t)tol_kind_int;
    opts.max_depth              = max_depth;
    opts.max_memory_mib         = max_memory_mib;
    opts.allow_max_depth_leaves = allow_max_depth_leaves;
    opts.min_uniform_depth      = min_uniform_depth;

    // Python carries state in the trampoline's &st context; opts trails last.
    // a and b are already std::vector<float> — use them directly.
    treeweave_t handle = treeweavef_fit(trampoline_f32, input_dim, output_dim, a.data(), b.data(), tol, &st, &opts);

    if (st.errored) {
        if (handle)
            handle = treeweave_free(handle);
        if (st.exc)
            raise_stashed(st.exc);
        throw std::runtime_error("treeweave callback failed (no exception stashed)");
    }

    if (!handle) {
        const char *msg = treeweave_last_error();
        throw std::runtime_error(msg && *msg ? msg : "treeweavef_fit returned NULL");
    }

    return nb::cast(new TreeweaveFunction(handle), nb::rv_policy::take_ownership);
}

// ---------------------------------------------------------------------------
// Module definition
// ---------------------------------------------------------------------------
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
        .def("eval_multi", &TreeweaveFunction::eval_multi_py, "AoS batch eval. x: (N, dim) or (N,) for 1D.", "x"_a)
        .def("sorted", &TreeweaveFunction::eval_sorted_py, "Sorted-1D batch eval (input_dim==1).", "x"_a)
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
