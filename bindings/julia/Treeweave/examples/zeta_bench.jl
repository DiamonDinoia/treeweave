# zeta_bench.jl — treeweave vs a fair brute-force Riemann-zeta eval.
# See examples/c++/zeta_bench.cpp for the rationale. ζ(s) = Σ_k k^-s summed until
# the tail is negligible (rel 1e-10, ≤160 terms) yet smooth on [2,10]: fit once.
# Times single/multi/sorted; the native rate is sampled over n_native and reused.
# TREEWEAVE_BENCH_YAML=path emits YAML.
using Treeweave
using Printf

# Fair baseline: sum k^-s until a term is below ZETA_EPS relative to the running
# total, capped at ZETA_MAX_TERMS — a competent zeta stops early.
const ZETA_EPS = 1e-10
const ZETA_MAX_TERMS = 160

# ζ(s) ≈ Σ_k k^-s (early stop). Both the fit callback and the native baseline.
function zeta_partial(s)
    acc = 0.0
    for k in 1:ZETA_MAX_TERMS
        term = Float64(k)^(-s)
        acc += term
        term < ZETA_EPS * acc && break
    end
    return acc
end

# Scalar accumulation in a function (not global scope) so boxing doesn't dominate.
function scalar_sum(f, xs)
    s = 0.0
    for x in xs
        s += f(x)
    end
    return s
end

# One eval-mode YAML block. "%.17e" carries a dot, so YAML 1.1 reads a float.
function emit_block(io, name, tw, nat)
    @printf(io, "%s:\n", name)
    @printf(io, "  treeweave_mevals_s: %.17e\n", tw)
    @printf(io, "  native_mevals_s: %.17e\n", nat)
    @printf(io, "  speedup: %.17e\n", tw / nat)
end

const a, b = 2.0, 10.0
approx = fit(zeta_partial, a, b, 1e-10)  # default tol_kind is RelativeMax
println(approx)

n        = 1_000_000   # batch / sorted points
n_scalar = 100_000     # scalar-API points
n_native = 256         # brute-force sample (<=160 powers each)
xs        = a .+ (b - a) .* rand(n)
xs_sorted = sort(xs)

# --- accuracy vs the brute-force sum, on the n_native sample -----------------
xs_native = xs[1:n_native]
yhat    = approx(xs_native)
yref    = zeta_partial.(xs_native)
max_rel = maximum(abs.(yhat .- yref) ./ abs.(yref))

mevals(count, seconds) = count / (seconds * 1e6)

# --- native rate: brute-force sum over the small sample (mode-independent) ---
scalar_sum(zeta_partial, xs_native)                                # warm-up (compile)
t0 = time_ns(); s_nat = scalar_sum(zeta_partial, xs_native); t1 = time_ns()
nat_s    = (t1 - t0) / 1e9
nat_rate = mevals(n_native, nat_s)                                 # Mevals/s, all modes
@assert isfinite(s_nat)

# --- single-eval: the scalar API, one point at a time ------------------------
xs_scalar = xs[1:n_scalar]
scalar_sum(approx, xs_scalar)                                      # warm-up (compile)
t0 = time_ns(); s_tw = scalar_sum(approx, xs_scalar); t1 = time_ns()
tw_single_s = (t1 - t0) / 1e9
@assert isfinite(s_tw)

# --- multi-eval: the unsorted batch (in place, allocation-free) --------------
tw_buf = similar(xs)
approx(xs; out=tw_buf)                                             # warm-up
t0 = time_ns(); approx(xs; out=tw_buf); t1 = time_ns()
tw_multi_s = (t1 - t0) / 1e9
@assert isfinite(sum(tw_buf))

# --- sorted-eval: the 1-D ascending fast path --------------------------------
approx(xs_sorted; sorted=true, out=tw_buf)                        # warm-up
t0 = time_ns(); approx(xs_sorted; sorted=true, out=tw_buf); t1 = time_ns()
tw_sorted_s = (t1 - t0) / 1e9
@assert isfinite(sum(tw_buf))

# --- throughput (Mevals/s) and speedup per mode ------------------------------
tw_single = mevals(n_scalar, tw_single_s)
tw_multi  = mevals(n, tw_multi_s)
tw_sorted = mevals(n, tw_sorted_s)

@printf("zeta(s) = sum_k k^-s (<=%d terms, stop at %.0e rel), fit on [%.1f, %.1f], relative tol %.0e\n",
        ZETA_MAX_TERMS, ZETA_EPS, a, b, 1e-10)
@printf("  max rel err: %.3e\n", max_rel)
@printf("  single-eval  treeweave %.1f  native %.4f Mevals/s  speedup %.1fx\n", tw_single, nat_rate, tw_single / nat_rate)
@printf("  multi-eval   treeweave %.1f  native %.4f Mevals/s  speedup %.1fx\n", tw_multi, nat_rate, tw_multi / nat_rate)
@printf("  sorted-eval  treeweave %.1f  native %.4f Mevals/s  speedup %.1fx\n", tw_sorted, nat_rate, tw_sorted / nat_rate)

# --- machine-readable YAML (optional) ----------------------------------------
yaml_path = get(ENV, "TREEWEAVE_BENCH_YAML", "")
if !isempty(yaml_path)
    open(yaml_path, "w") do io
        @printf(io, "language: \"julia\"\n")
        @printf(io, "domain: [%.17e, %.17e]\n", a, b)
        @printf(io, "tol: %.17e\n", 1e-10)
        @printf(io, "n_pts: %d\n", n)
        @printf(io, "max_rel_err: %.17e\n", max_rel)
        emit_block(io, "single_eval", tw_single, nat_rate)
        emit_block(io, "multi_eval",  tw_multi,  nat_rate)
        emit_block(io, "sorted_eval", tw_sorted, nat_rate)
    end
end
