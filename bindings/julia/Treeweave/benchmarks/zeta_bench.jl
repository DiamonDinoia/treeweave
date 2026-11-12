# zeta_bench.jl: treeweave vs a fair brute-force Riemann-zeta eval.
# See benchmarks/zeta_bench.cpp for the rationale. ζ(s) = Σ_k k^-s summed until
# the tail is negligible (rel 1e-10, ≤160 terms) yet smooth on [2,10]: fit once.
# Times single/multi/sorted; the native rate is sampled over n_native and reused.
# TREEWEAVE_BENCH_YAML=path emits YAML.
using Treeweave
using Printf
using FastChebInterp  # baseline only; not a Treeweave dependency (see benchmark-showcase.yml)

# Fair baseline: sum k^-s until a term is below ZETA_EPS relative to the running
# total, capped at ZETA_MAX_TERMS: a competent zeta stops early.
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

# Fit target: all ZETA_MAX_TERMS terms. The early stop above makes zeta_partial
# jump by up to ZETA_EPS relative where the term count changes, and a global
# Chebyshev series then needs degree 177 instead of 26 on [2, 10]. Every
# interpolator, treeweave included, fits this smooth sum; the native rate uses
# zeta_partial.
function zeta_smooth(s)
    acc = 0.0
    for k in 1:ZETA_MAX_TERMS
        acc += Float64(k)^(-s)
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
fn = fit(zeta_smooth, a, b, 1e-10)  # default tol_kind is RelativeMax
println(fn)

n        = 1_000_000   # batch / sorted points
n_scalar = 100_000     # scalar-API points
n_native = 256         # brute-force sample (<=160 powers each)
xs        = a .+ (b - a) .* rand(n)
xs_sorted = sort(xs)

xs_native = xs[1:n_native]

mevals(count, seconds) = count / (seconds * 1e6)

scalar_sum(zeta_partial, xs_native)                                # warm-up (compile)
t0 = time_ns(); s_nat = scalar_sum(zeta_partial, xs_native); t1 = time_ns()
nat_s    = (t1 - t0) / 1e9
nat_rate = mevals(n_native, nat_s)                                 # Mevals/s, all modes
@assert isfinite(s_nat)

xs_scalar = xs[1:n_scalar]
scalar_sum(fn, xs_scalar)                                      # warm-up (compile)
t0 = time_ns(); s_tw = scalar_sum(fn, xs_scalar); t1 = time_ns()
tw_single_s = (t1 - t0) / 1e9
@assert isfinite(s_tw)

tw_buf = similar(xs)
fn(xs; out=tw_buf)                                             # warm-up
t0 = time_ns(); fn(xs; out=tw_buf); t1 = time_ns()
tw_multi_s = (t1 - t0) / 1e9
@assert isfinite(sum(tw_buf))

fn(xs_sorted; sorted=true, out=tw_buf)                        # warm-up
t0 = time_ns(); fn(xs_sorted; sorted=true, out=tw_buf); t1 = time_ns()
tw_sorted_s = (t1 - t0) / 1e9
@assert isfinite(sum(tw_buf))

# Accuracy of every method, treeweave included, on one check sample. The
# criterion is treeweave's default RelativeMax, max|p - f| <= tol * max|f|; the
# report also prints the pointwise max |p - f| / |f| for every method.
const tol = 1e-10
xs_check  = xs[1:4096]
ref_check = zeta_smooth.(xs_check)

function rel_errors(p)
    err = abs.(p.(xs_check) .- ref_check)
    return maximum(err) / maximum(abs.(ref_check)), maximum(err ./ abs.(ref_check))
end

tw_relmax, max_rel = rel_errors(fn)

# single: the scalar call per point; multi/sorted: one broadcast over the batch.
function time_scalar_api(p)
    scalar_sum(p, xs_scalar)                                   # warm-up (compile)
    t0 = time_ns(); s = scalar_sum(p, xs_scalar); t1 = time_ns()
    @assert isfinite(s)
    rates = [mevals(n_scalar, (t1 - t0) / 1e9)]
    buf = similar(xs)
    for pts in (xs, xs_sorted)
        buf .= p.(pts)                                         # warm-up
        t0 = time_ns(); buf .= p.(pts); t1 = time_ns()
        @assert isfinite(sum(buf))
        push!(rates, mevals(n, (t1 - t0) / 1e9))
    end
    return rates
end

baselines = []
# FastChebInterp: degree-d series from d + 1 Chebyshev points, tol=0 so no
# coefficient is dropped. Grow d by one until it meets tol: the smallest that does.
let found = false
    for deg in 2:1000
        cheb = chebinterp(zeta_smooth.(chebpoints(deg, a, b)), a, b; tol=0)
        crit, err = rel_errors(cheb)
        if crit <= tol
            push!(baselines, ("FastChebInterp.jl", "degree $deg", crit, err, time_scalar_api(cheb)...))
            found = true
            break
        end
    end
    found || error("FastChebInterp never met tol $tol")
end

tw_single = mevals(n_scalar, tw_single_s)
tw_multi  = mevals(n, tw_multi_s)
tw_sorted = mevals(n, tw_sorted_s)

@printf("zeta(s) = sum_k k^-s (<=%d terms, stop at %.0e rel), fit on [%.1f, %.1f], relative tol %.0e\n",
        ZETA_MAX_TERMS, ZETA_EPS, a, b, 1e-10)
@printf("  max rel err: %.3e\n", max_rel)
@printf("  single-eval  treeweave %.1f  native %.4f Mevals/s  speedup %.1fx\n", tw_single, nat_rate, tw_single / nat_rate)
@printf("  multi-eval   treeweave %.1f  native %.4f Mevals/s  speedup %.1fx\n", tw_multi, nat_rate, tw_multi / nat_rate)
@printf("  sorted-eval  treeweave %.1f  native %.4f Mevals/s  speedup %.1fx\n", tw_sorted, nat_rate, tw_sorted / nat_rate)
for (name, size, relmax, err, single, multi, srt) in baselines
    @printf("  %s (%s, max rel err %.3e)  single %.2f  multi %.2f  sorted %.2f Mevals/s\n", name, size, err, single, multi, srt)
end

yaml_path = get(ENV, "TREEWEAVE_BENCH_YAML", "")
if !isempty(yaml_path)
    open(yaml_path, "w") do io
        @printf(io, "language: \"julia\"\n")
        @printf(io, "domain: [%.17e, %.17e]\n", a, b)
        @printf(io, "tol: %.17e\n", 1e-10)
        @printf(io, "n_pts: %d\n", n)
        @printf(io, "relmax_err: %.17e\n", tw_relmax)
        @printf(io, "max_rel_err: %.17e\n", max_rel)
        emit_block(io, "single_eval", tw_single, nat_rate)
        emit_block(io, "multi_eval",  tw_multi,  nat_rate)
        emit_block(io, "sorted_eval", tw_sorted, nat_rate)
        println(io, "interp:")
        for (name, size, relmax, err, single, multi, srt) in baselines
            @printf(io, "  - name: \"%s\"\n    size: \"%s\"\n", name, size)
            @printf(io, "    relmax_err: %.17e\n    max_rel_err: %.17e\n", relmax, err)
            @printf(io, "    single_eval: %.17e\n", single)
            @printf(io, "    multi_eval: %.17e\n    sorted_eval: %.17e\n", multi, srt)
        end
    end
end
