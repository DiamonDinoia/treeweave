// treeweave_napi.cpp: Node-API (node-addon-api) native backend.
// JS exceptions from the fit callback are caught in the trampoline (not unwound through C ABI); eval batches are
// zero-copy through Node's ArrayBuffer backing store.

#include <napi.h>

#include <treeweave.h>

#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr double kNaN64 = std::numeric_limits<double>::quiet_NaN();
constexpr float  kNaN32 = std::numeric_limits<float>::quiet_NaN();

// Trampoline state (one per fit call; lives on the stack for the fit's span).
template <typename T>
struct FitState {
    Napi::Env      env;
    Napi::Function cb;
    int            input_dim;
    int            output_dim;
    bool           errored = false;
    std::string    errmsg;
};

// Read `od` outputs from the callback's return value into y[]. Accepts a bare
// number when od == 1, otherwise any indexable (Array / TypedArray).
template <typename T>
void read_outputs(Napi::Value v, T *y, int od) {
    if (od == 1 && v.IsNumber()) {
        y[0] = static_cast<T>(v.As<Napi::Number>().DoubleValue());
        return;
    }
    Napi::Object o = v.As<Napi::Object>();
    for (int i = 0; i < od; ++i)
        y[i] = static_cast<T>(o.Get(static_cast<uint32_t>(i)).As<Napi::Number>().DoubleValue());
}

template <typename T, typename Arr>
void trampoline(const T *x, T *y, void *ctx, T nan_value) {
    auto *st = static_cast<FitState<T> *>(ctx);
    if (st->errored) {
        for (int i = 0; i < st->output_dim; ++i)
            y[i] = nan_value;
        return;
    }
    Napi::Env         env = st->env;
    Napi::HandleScope scope(env); // release the per-call handles each probe
    try {
        // A fresh TypedArray for the (tiny, <=3-element) input point. Copying is
        // simpler and safer than an external view and costs nothing at this size.
        Arr xarr = Arr::New(env, st->input_dim);
        for (int i = 0; i < st->input_dim; ++i)
            xarr[i] = x[i];
        Napi::Value result = st->cb.Call({xarr});
        read_outputs<T>(result, y, st->output_dim);
    } catch (const Napi::Error &e) {
        st->errored = true;
        st->errmsg  = e.Message();
        for (int i = 0; i < st->output_dim; ++i)
            y[i] = nan_value;
    } catch (const std::exception &e) {
        st->errored = true;
        st->errmsg  = e.what();
        for (int i = 0; i < st->output_dim; ++i)
            y[i] = nan_value;
    }
}

extern "C" void trampoline_f64(const double *x, double *y, void *ctx) {
    trampoline<double, Napi::Float64Array>(x, y, ctx, kNaN64);
}
extern "C" void trampoline_f32(const float *x, float *y, void *ctx) {
    trampoline<float, Napi::Float32Array>(x, y, ctx, kNaN32);
}

// Fitted-function state: owns the handle; freed when the last closure dies.
struct FnState {
    treeweave_t h;
    int         input_dim;
    int         output_dim;
    bool        f32;
    FnState(treeweave_t handle, int in, int out, bool is_f32)
        : h(handle), input_dim(in), output_dim(out), f32(is_f32) {}
    // Owns the handle: must not be copied (a copy's destructor would free a
    // handle still in use: the bug of constructing via a temporary).
    FnState(const FnState &)            = delete;
    FnState &operator=(const FnState &) = delete;
    ~FnState() {
        if (h)
            h = treeweave_free(h);
    }
};

[[noreturn]] void throw_last_error(Napi::Env env, const char *fallback) {
    const char *msg = treeweave_last_error();
    throw Napi::Error::New(env, (msg && *msg) ? msg : fallback);
}

template <typename T, typename Arr>
Napi::Value eval_one_impl(const std::shared_ptr<FnState> &st, const Napi::CallbackInfo &info,
                          void (*eval)(treeweave_t, const T *, T *)) {
    Napi::Env      env = info.Env();
    Arr            x   = info[0].As<Arr>();
    std::vector<T> xv(st->input_dim), yv(st->output_dim);
    for (int i = 0; i < st->input_dim; ++i)
        xv[i] = x[i];
    eval(st->h, xv.data(), yv.data());
    if (st->output_dim == 1)
        return Napi::Number::New(env, static_cast<double>(yv[0]));
    Arr out = Arr::New(env, st->output_dim);
    for (int i = 0; i < st->output_dim; ++i)
        out[i] = yv[i];
    return out;
}

template <typename T, typename Arr>
Napi::Value batch_impl(const std::shared_ptr<FnState> &st, const Napi::CallbackInfo &info,
                       void (*eval)(treeweave_t, const T *, T *, size_t)) {
    Napi::Env    env = info.Env();
    Arr          x   = info[0].As<Arr>();
    const size_t n   = x.ElementLength() / static_cast<size_t>(st->input_dim);
    Arr          out;
    if (info.Length() > 1 && !info[1].IsUndefined() && !info[1].IsNull()) {
        out = info[1].As<Arr>();
        if (out.ElementLength() != n * static_cast<size_t>(st->output_dim))
            throw Napi::Error::New(env, "out has the wrong number of elements for this batch");
    } else {
        out = Arr::New(env, n * static_cast<size_t>(st->output_dim));
    }
    eval(st->h, x.Data(), out.Data(), n); // zero-copy through both TypedArrays
    return out;
}

template <typename T, typename Arr>
Napi::Value transposed_impl(const std::shared_ptr<FnState> &st, const Napi::CallbackInfo &info,
                            void (*eval)(treeweave_t, const T *, T *const *, size_t)) {
    Napi::Env                   env = info.Env();
    Arr                         x   = info[0].As<Arr>();
    const size_t                n   = x.ElementLength() / static_cast<size_t>(st->input_dim);
    const int                   od  = st->output_dim;
    std::vector<std::vector<T>> comps(od, std::vector<T>(n));
    std::vector<T *>            soa(od);
    for (int d = 0; d < od; ++d)
        soa[d] = comps[d].data();
    eval(st->h, x.Data(), soa.data(), n);
    Napi::Array result = Napi::Array::New(env, od);
    for (int d = 0; d < od; ++d) {
        Arr comp = Arr::New(env, n);
        for (size_t i = 0; i < n; ++i)
            comp[i] = comps[d][i];
        result.Set(static_cast<uint32_t>(d), comp);
    }
    return result;
}

// fit(callback, inputDim, outputDim, a, b, tol, optsInt32[5], dtype) -> object
Napi::Value Fit(const Napi::CallbackInfo &info) {
    Napi::Env        env        = info.Env();
    Napi::Function   cb         = info[0].As<Napi::Function>();
    const int        input_dim  = info[1].As<Napi::Number>().Int32Value();
    const int        output_dim = info[2].As<Napi::Number>().Int32Value();
    const double     tol        = info[5].As<Napi::Number>().DoubleValue();
    Napi::Int32Array o          = info[6].As<Napi::Int32Array>();
    const bool       f32        = info[7].As<Napi::String>().Utf8Value() == "f32";

    treeweave_opts opts;
    opts.tol_kind               = static_cast<treeweave_tol_kind_t>(o[0]);
    opts.max_depth              = o[1];
    opts.max_memory_mib         = o[2];
    opts.allow_max_depth_leaves = o[3];
    opts.min_uniform_depth      = o[4];

    treeweave_t handle = nullptr;
    if (f32) {
        FitState<float>    st{env, cb, input_dim, output_dim};
        Napi::Float32Array a = info[3].As<Napi::Float32Array>();
        Napi::Float32Array b = info[4].As<Napi::Float32Array>();
        handle = treeweavef_fit(trampoline_f32, input_dim, output_dim, a.Data(), b.Data(), tol, &st, &opts);
        if (st.errored) {
            if (handle)
                treeweave_free(handle);
            throw Napi::Error::New(env, st.errmsg.empty() ? "treeweave callback failed" : st.errmsg);
        }
    } else {
        FitState<double>   st{env, cb, input_dim, output_dim};
        Napi::Float64Array a = info[3].As<Napi::Float64Array>();
        Napi::Float64Array b = info[4].As<Napi::Float64Array>();
        handle = treeweave_fit(trampoline_f64, input_dim, output_dim, a.Data(), b.Data(), tol, &st, &opts);
        if (st.errored) {
            if (handle)
                treeweave_free(handle);
            throw Napi::Error::New(env, st.errmsg.empty() ? "treeweave callback failed" : st.errmsg);
        }
    }
    if (!handle)
        throw_last_error(env, "treeweave_fit returned NULL");

    auto st = std::make_shared<FnState>(handle, input_dim, output_dim, f32);

    Napi::Object obj = Napi::Object::New(env);
    obj.Set("inputDim", Napi::Number::New(env, input_dim));
    obj.Set("outputDim", Napi::Number::New(env, output_dim));
    obj.Set("dtype", Napi::String::New(env, f32 ? "f32" : "f64"));

    obj.Set("evalOne", Napi::Function::New(env, [st](const Napi::CallbackInfo &info) -> Napi::Value {
                return st->f32 ? eval_one_impl<float, Napi::Float32Array>(st, info, treeweavef_eval)
                               : eval_one_impl<double, Napi::Float64Array>(st, info, treeweave_eval);
            }));
    obj.Set("batch", Napi::Function::New(env, [st](const Napi::CallbackInfo &info) -> Napi::Value {
                return st->f32 ? batch_impl<float, Napi::Float32Array>(st, info, treeweavef_batch)
                               : batch_impl<double, Napi::Float64Array>(st, info, treeweave_batch);
            }));
    obj.Set("sorted", Napi::Function::New(env, [st](const Napi::CallbackInfo &info) -> Napi::Value {
                return st->f32 ? batch_impl<float, Napi::Float32Array>(st, info, treeweavef_sorted)
                               : batch_impl<double, Napi::Float64Array>(st, info, treeweave_sorted);
            }));
    obj.Set("transposed", Napi::Function::New(env, [st](const Napi::CallbackInfo &info) -> Napi::Value {
                return st->f32 ? transposed_impl<float, Napi::Float32Array>(st, info, treeweavef_transposed)
                               : transposed_impl<double, Napi::Float64Array>(st, info, treeweave_transposed);
            }));
    obj.Set("memoryUsage", Napi::Function::New(env, [st](const Napi::CallbackInfo &info) -> Napi::Value {
                return Napi::Number::New(info.Env(), static_cast<double>(treeweave_memory_usage(st->h)));
            }));
    obj.Set("printStats", Napi::Function::New(env, [st](const Napi::CallbackInfo &info) -> Napi::Value {
                treeweave_print_stats(st->h);
                return info.Env().Undefined();
            }));
    obj.Set("free", Napi::Function::New(env, [st](const Napi::CallbackInfo &info) -> Napi::Value {
                if (st->h)
                    st->h = treeweave_free(st->h);
                return info.Env().Undefined();
            }));
    return obj;
}

} // namespace

static Napi::Object Init(Napi::Env env, Napi::Object exports) {
    exports.Set("fit", Napi::Function::New(env, Fit));
    exports.Set("versionString", Napi::String::New(env, TREEWEAVE_VERSION_STRING));
    exports.Set("version", Napi::Number::New(env, TREEWEAVE_VERSION));
    return exports;
}

NODE_API_MODULE(treeweave, Init)
