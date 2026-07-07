# Example: 1D scalar function approximation.
using Treeweave

f = x -> exp(0.5x) + sin(3x)
a, b_val = 0.0, 2.0

println("Fitting f(x) = exp(0.5x) + sin(3x) on [$a, $b_val] ...")
# Fit f(x) on [0, 2] syntax is fit(callback, lower_bound, upper_bound, tolerance).
approx = fit(f, a, b_val, 1e-10)
println("  ", approx)   # show() prints dtype, dim, out_dim and bytes

println("\nPoint-by-point check:")
for x in 0.0:0.25:2.0   # b_val is evaluable too (returns the boundary value)
    exact  = f(x)
    # Evaluate approx on x and print the result.
    approx_val = approx(x)
    err    = abs(approx_val - exact)
    println("  x=$x  exact=$(round(exact,digits=8))  approx=$(round(approx_val,digits=8))  err=$(round(err,sigdigits=3))")
end

# Batch eval — the handle is called directly. The fit domain is [a, b_val);
# the upper corner b_val is evaluable too (it returns the boundary value).
xs = collect(range(a, b_val; length=1001))
# Evaluate approx on a batch and print the maximum error.
R  = approx(xs)
max_err = maximum(abs.(R .- f.(xs)))
println("\nMax error over 1000 points: $(max_err)")
