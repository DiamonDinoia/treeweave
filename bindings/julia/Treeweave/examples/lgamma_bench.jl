# lgamma_bench.jl — treeweave vs SpecialFunctions.loggamma.
#
# The Julia member of the cross-language lgamma benchmark family (see
# examples/c++/lgamma_bench.cpp for the rationale). log-Gamma is fit on [3, 50)
# — smooth, positive, monotone, so relative error is well defined — with
# treeweave's default RelativeMax tolerance, then compared to
# SpecialFunctions.loggamma on max relative error, throughput, and speedup.
#
# Requires SpecialFunctions:  julia> using Pkg; Pkg.add("SpecialFunctions")
using Treeweave
using SpecialFunctions: loggamma
using Printf

const a, b = 3.0, 50.0
approx = fit(loggamma, a, b, 1e-10)      # default tol_kind is RelativeMax
println(approx)

n  = 1_000_000
xs = a .+ (b - a) .* rand(n)

# --- accuracy vs the library -------------------------------------------------
yhat    = approx(xs)
yref    = loggamma.(xs)
max_rel = maximum(abs.(yhat .- yref) ./ abs.(yref))

# --- throughput: treeweave vs loggamma --------------------------------------
approx(xs)                                # warm-up (compile + caches)
t0 = time_ns(); R = approx(xs);     t1 = time_ns()
loggamma.(xs)                             # warm-up
t2 = time_ns(); L = loggamma.(xs); t3 = time_ns()

tw_s  = (t1 - t0) / 1e9
lib_s = (t3 - t2) / 1e9
# Reference R and L so the timed calls are not elided.
@assert isfinite(sum(R) + sum(L))

@printf("lgamma fit on [%.1f, %.1f), relative tol %.0e\n", a, b, 1e-10)
@printf("  max rel err: %.3e\n", max_rel)
@printf("  treeweave:  %.1f Mevals/s\n", n / (tw_s * 1e6))
@printf("  library: %.1f Mevals/s\n", n / (lib_s * 1e6))
@printf("  speedup: %.2fx\n", lib_s / tw_s)
