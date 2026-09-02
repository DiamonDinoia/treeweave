# Example: 2D -> 3D vector approximation.
using Treeweave

println("Fitting f: R^2 -> R^3 on [0,1]^2 ...")
# Fit f(x, y) on [0, 1]^2 syntax is fit(callback, lower_bound, upper_bound, tolerance).
# BEGIN DOCS_MULTIDIM
# 2-D input -> 3-D vector output (out_dim inferred by probing the midpoint)
approx = fit((x, y) -> (sin(x) * cos(y), x + y, x * y), [0.0, 0.0], [1.0, 1.0], 1e-8)

pt = approx([0.3, 0.7])            # single point -> length-3 Vector
X  = rand(100, 2)
R  = approx(X)                     # batch -> 100x3 Matrix
Rt = approx(X; transposed = true)  # batch -> 3x100 Matrix
# END DOCS_MULTIDIM
println("  ", approx)

f = (x, y) -> (sin(x) * cos(y), x + y, x * y)
ref = collect(f(0.3, 0.7))
println("\nSingle point [0.3, 0.7]:")
println("  approx = $pt")
println("  exact  = $ref")
pt_err = maximum(abs.(pt .- ref))
println("  error  = $(pt_err)")
@assert pt_err < 1e-7

# Batch eval: the AoS and struct-of-arrays layouts hold the same numbers.
@assert R == permutedims(Rt)
errs = [maximum(abs.(R[i, :] .- collect(f(X[i, 1], X[i, 2])))) for i in 1:size(X, 1)]
println("\nBatch eval ($(size(X, 1)) points), max component error: $(maximum(errs))")
@assert maximum(errs) < 1e-7
println("OK")
